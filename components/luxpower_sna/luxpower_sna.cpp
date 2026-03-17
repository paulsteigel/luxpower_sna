#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#include <errno.h>
#include <cstring>

namespace esphome {
namespace luxpower_sna {

// ---------------------------------------------------------------------------
// Status text tables
// ---------------------------------------------------------------------------
const char *LuxpowerSNAComponent::STATUS_TEXTS[193] = {
    "Standby", "Error", "Inverting", "",
    "Solar > Load - Surplus > Grid", "Float", "", "Charger Off",
    "Supporting", "Selling", "Pass Through", "Offsetting",
    "Solar > Battery Charging", "", "", "",
    "Battery Discharging > Load - Surplus > Grid",
    "Temperature Over Range", "", "",
    "Solar + Battery Discharging > Load - Surplus > Grid",
    "", "", "", "", "", "", "",
    "AC Battery Charging", "", "", "", "", "",
    "Solar + Grid > Battery Charging",
    "", "", "", "", "", "", "", "", "",
    "No Grid : Battery > EPS",
    "", "", "", "", "", "", "", "",
    "No Grid : Solar > EPS - Surplus > Battery Charging",
    "", "", "", "",
    "No Grid : Solar + Battery Discharging > EPS"
    // remaining slots stay nullptr (zero-init)
};

const char *LuxpowerSNAComponent::BAT_STATUS_TEXTS[17] = {
    "Charge Forbidden & Discharge Forbidden",
    "Unknown",
    "Charge Forbidden & Discharge Allowed",
    "Charge Allowed & Discharge Allowed",
    "", "", "", "", "", "", "", "", "", "", "", "",
    "Charge Allowed & Discharge Forbidden"
};

// ---------------------------------------------------------------------------
// CRC-16/Modbus
// ---------------------------------------------------------------------------
uint16_t LuxpowerSNAComponent::crc16_(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Component lifecycle
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::setup() {
    ESP_LOGCONFIG(TAG, "LuxPower SNA setup…");
    memset(recv_buf_, 0, sizeof(recv_buf_));
    recv_buf_len_ = 0;
    last_input_poll_ms_ = millis();
    last_hold_poll_ms_  = millis();
    // Load persisted host from NVS — runs before MQTT can overwrite it
    load_host_prefs_();
}

void LuxpowerSNAComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "LuxPower SNA:");
    ESP_LOGCONFIG(TAG, "  Host: %s:%u", host_.c_str(), port_);
    ESP_LOGCONFIG(TAG, "  Dongle:   %s", dongle_serial_.c_str());
    ESP_LOGCONFIG(TAG, "  Inverter: %s", inverter_serial_.c_str());
    ESP_LOGCONFIG(TAG, "  Poll interval: %ums, Hold interval: %ums",
                  update_interval_ms_, hold_interval_ms_);
    ESP_LOGCONFIG(TAG, "  Switches: %d, Numbers: %d",
                  (int)switches_.size(), (int)numbers_.size());
}

// ---------------------------------------------------------------------------
// Main loop – non-blocking state machine
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::loop() {
    uint32_t now = esphome::millis();

    // ── Watchdog: unstick scanning_ if task died without setting scan_result_pending_ ──
    if (scanning_ && !scan_result_pending_ && (now - scan_start_ms_ > SCAN_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Scan watchdog: task silent >30s, resetting scanning_");
        scanning_ = false;
        pub(scan_status_text_, "Error: scan timeout");
    }

    // ── Step 1: pick up raw result flag set by FreeRTOS scan task ────────────────
    if (scan_result_pending_) {
        scan_result_pending_ = false;
        scanning_            = false;
        if (scan_found_) {
            deferred_ip_    = std::string(found_ip_buf_);
            deferred_apply_ = true;
            pub(scan_status_text_, "Found: " + deferred_ip_);
            ESP_LOGI(TAG, "Scan queued for deferred apply: %s", deferred_ip_.c_str());
        } else {
            pub(scan_status_text_, "Not found");
        }
        // Return here so deferred_apply_ fires on NEXT tick.
        // This keeps apply_scanned_host_() (which triggers on_value lambda
        // → reconnect()) out of the scan_result_pending_ call stack.
        return;
    }

    // ── Step 2: deferred apply – one tick after scan_result_pending_ fired ───────
    if (deferred_apply_) {
        deferred_apply_ = false;
        ESP_LOGI(TAG, "Deferred apply: host = %s", deferred_ip_.c_str());
        apply_scanned_host_(deferred_ip_);
        deferred_ip_.clear();
        return;
    }

    // ── Guard: skip normal polling while scan task is running ──────────────
    if (scanning_) return;

    // ── Guard: do nothing until config is complete ───────────────────────
    if (!is_config_ready()) {
        if (now - last_connect_ms_ >= 10000) {
            last_connect_ms_ = now;
            ESP_LOGW(TAG, "Config incomplete – waiting for host/dongle/inverter serial via HA");
        }
        return;
    }

    // ── Handle disconnection / reconnect ──────────────────────────────────
    if (state_ == State::DISCONNECTED) {
        if (now - last_connect_ms_ >= 10000) {
            last_connect_ms_ = now;
            ESP_LOGI(TAG, "Connecting to %s:%u…", host_.c_str(), port_);
            if (start_connect_()) {
                state_ = State::CONNECTING;
            }
        }
        return;
    }

    // ── Wait for async connect to complete ────────────────────────────────
    if (state_ == State::CONNECTING) {
        if (!check_connect_()) {
            if (now - last_connect_ms_ > 10000) {
                ESP_LOGW(TAG, "Connect timed out");
                close_socket_();
                state_ = State::DISCONNECTED;
            }
        }
        return;
    }

    // ── Connected – receive data first (always) ──────────────────────────────
    try_recv_();
    while (try_process_packet_()) {}

    // ── Response timeout guard ────────────────────────────────────────────
    if (awaiting_ && (now - req_sent_ms_ > RESPONSE_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Response timeout (bank %u)", bank_idx_);
        awaiting_ = false;
        bank_idx_++;
        if (state_ == State::POLLING_INPUT && bank_idx_ >= 5) {
            state_ = State::IDLE;
        } else if (state_ == State::POLLING_HOLD && bank_idx_ >= 6) {
            state_ = State::IDLE;
            initial_hold_done_ = true;
        } else if (state_ == State::WRITING) {
            state_ = State::IDLE;
        }
    }

    if (awaiting_) return;

    // ── State transitions ────────────────────────────────────────────────────
    switch (state_) {
        case State::IDLE: {
            if (!write_queue_.empty()) {
                auto cmd = write_queue_.front();
                write_queue_.pop();
                send_write_single_(cmd.reg, cmd.value);
                state_ = State::WRITING;
                awaiting_ = true;
                req_sent_ms_ = now;
                return;
            }
            if (!initial_hold_done_) {
                bank_idx_ = 0;
                state_ = State::POLLING_HOLD;
            } else if (now - last_input_poll_ms_ >= update_interval_ms_) {
                last_input_poll_ms_ = now;
                bank_idx_ = 0;
                state_ = State::POLLING_INPUT;
            } else if (now - last_hold_poll_ms_ >= hold_interval_ms_) {
                last_hold_poll_ms_ = now;
                bank_idx_ = 0;
                state_ = State::POLLING_HOLD;
            }
            break;
        }

        case State::POLLING_INPUT: {
            static const uint16_t INPUT_BANKS[5] = {0, 40, 80, 120, 160};
            if (bank_idx_ < 5) {
                send_read_input_(INPUT_BANKS[bank_idx_]);
                awaiting_ = true;
                req_sent_ms_ = now;
            } else {
                ESP_LOGI(TAG, "Input poll cycle complete.");
                state_ = State::IDLE;
            }
            break;
        }

        case State::POLLING_HOLD: {
            if (bank_idx_ < 6) {
                send_read_hold_(bank_idx_ * 40);
                awaiting_ = true;
                req_sent_ms_ = now;
            } else {
                ESP_LOGI(TAG, "Hold poll cycle complete.");
                initial_hold_done_ = true;
                last_hold_poll_ms_ = now;
                notify_hold_listeners_();
                state_ = State::IDLE;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Socket helpers
// ---------------------------------------------------------------------------
bool LuxpowerSNAComponent::start_connect_() {
    close_socket_();
    sock_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd_ < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        return false;
    }

    int non_blocking = 1;
    ioctl(sock_fd_, FIONBIO, &non_blocking);

    int yes = 1;
    setsockopt(sock_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    int gai = getaddrinfo(host_.c_str(), nullptr, &hints, &res);
    if (gai != 0 || res == nullptr) {
        ESP_LOGE(TAG, "DNS lookup failed for %s (err=%d)", host_.c_str(), gai);
        close_socket_();
        return false;
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    addr.sin_addr   = reinterpret_cast<struct sockaddr_in *>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    int ret = connect(sock_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (ret == 0) {
        ESP_LOGI(TAG, "Connected immediately to %s:%u", host_.c_str(), port_);
        state_ = State::IDLE;
        return true;
    }
    if (errno == EINPROGRESS) {
        return true;
    }
    ESP_LOGE(TAG, "connect() failed: errno=%d", errno);
    close_socket_();
    return false;
}

bool LuxpowerSNAComponent::check_connect_() {
    fd_set wfds, efds;
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_SET(sock_fd_, &wfds);
    FD_SET(sock_fd_, &efds);
    struct timeval tv{0, 0};

    int ret = select(sock_fd_ + 1, nullptr, &wfds, &efds, &tv);
    if (ret < 0) {
        ESP_LOGE(TAG, "select() error %d during connect check", errno);
        close_socket_();
        return false;
    }
    if (ret == 0) return false;

    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) {
        ESP_LOGW(TAG, "Connect failed with SO_ERROR=%d", err);
        close_socket_();
        state_ = State::DISCONNECTED;
        return false;
    }

    ESP_LOGI(TAG, "Connected to %s:%u", host_.c_str(), port_);
    state_ = State::IDLE;
    return true;
}

void LuxpowerSNAComponent::close_socket_() {
    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
    recv_buf_len_ = 0;
    awaiting_ = false;
    state_ = State::DISCONNECTED;
}

int LuxpowerSNAComponent::send_bytes_(const uint8_t *data, size_t len) {
    if (sock_fd_ < 0) return -1;
    int ret = send(sock_fd_, data, len, 0);
    if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "send() failed: %d – reconnecting", errno);
        close_socket_();
    }
    return ret;
}

void LuxpowerSNAComponent::try_recv_() {
    if (sock_fd_ < 0) return;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_fd_, &rfds);
    struct timeval tv{0, 0};
    if (select(sock_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return;

    uint8_t tmp[256];
    int n = recv(sock_fd_, tmp, sizeof(tmp), 0);
    if (n > 0) {
        size_t space = sizeof(recv_buf_) - recv_buf_len_;
        size_t copy_n = (size_t)n < space ? (size_t)n : space;
        memcpy(recv_buf_ + recv_buf_len_, tmp, copy_n);
        recv_buf_len_ += copy_n;
    } else if (n == 0) {
        ESP_LOGW(TAG, "Connection closed by remote");
        close_socket_();
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "recv() error %d – reconnecting", errno);
        close_socket_();
    }
}

bool LuxpowerSNAComponent::try_process_packet_() {
    if (recv_buf_len_ < 6) return false;

    if (recv_buf_[0] != 0xA1 || recv_buf_[1] != 0x1A) {
        for (size_t i = 1; i + 1 < recv_buf_len_; i++) {
            if (recv_buf_[i] == 0xA1 && recv_buf_[i+1] == 0x1A) {
                memmove(recv_buf_, recv_buf_ + i, recv_buf_len_ - i);
                recv_buf_len_ -= i;
                return false;
            }
        }
        recv_buf_len_ = 0;
        return false;
    }

    uint16_t frame_length = (uint16_t)(recv_buf_[4] | (recv_buf_[5] << 8));
    size_t total = frame_length + 6;
    if (total > sizeof(recv_buf_)) {
        ESP_LOGE(TAG, "Packet too large (%u), discarding buffer", (unsigned)total);
        recv_buf_len_ = 0;
        return false;
    }
    if (recv_buf_len_ < total) return false;

    process_packet_(recv_buf_, total);

    memmove(recv_buf_, recv_buf_ + total, recv_buf_len_ - total);
    recv_buf_len_ -= total;
    return true;
}

void LuxpowerSNAComponent::process_packet_(const uint8_t *buf, size_t len) {
    if (len < 20) return;

    uint8_t tcp_fn = buf[7];
    ESP_LOGV(TAG, "Packet tcp_fn=0x%02X len=%u", tcp_fn, (unsigned)len);

    if (tcp_fn == LUX_TCP_HEARTBEAT) {
        ESP_LOGD(TAG, "Heartbeat – echoing back");
        send_heartbeat_response_(buf, len);
        return;
    }

    if (tcp_fn != LUX_TCP_TRANSLATED_DATA) {
        ESP_LOGV(TAG, "Unknown tcp_function 0x%02X, ignoring", tcp_fn);
        return;
    }

    if (len < 22) return;

    const uint8_t *df = buf + 20;
    size_t df_len     = len - 20 - 2;

    uint16_t crc_calc = crc16_(df, df_len);
    uint16_t crc_recv = (uint16_t)(buf[len-2] | (buf[len-1] << 8));
    if (crc_calc != crc_recv) {
        ESP_LOGE(TAG, "CRC mismatch calc=0x%04X recv=0x%04X", crc_calc, crc_recv);
        return;
    }

    if (df_len < 14) return;
    uint8_t  dev_fn = df[1];
    uint16_t reg    = (uint16_t)(df[12] | (df[13] << 8));

    awaiting_ = false;

    switch (dev_fn) {
        case LUX_FN_READ_INPUT: {
            if (df_len < 15) return;
            uint8_t  vlen = df[14];
            const uint8_t *data = df + 15;
            if (df_len < (size_t)(15 + vlen)) return;
            process_read_input_(reg, data, vlen);
            bank_idx_++;
            break;
        }
        case LUX_FN_READ_HOLD: {
            if (df_len < 15) return;
            uint8_t vlen = df[14];
            const uint8_t *data = df + 15;
            if (df_len < (size_t)(15 + vlen)) return;
            process_read_hold_(reg, data, vlen / 2);
            bank_idx_++;
            break;
        }
        case LUX_FN_WRITE_SINGLE: {
            if (df_len < 16) return;
            uint16_t val = (uint16_t)(df[14] | (df[15] << 8));
            process_write_single_(reg, val);
            break;
        }
        default:
            ESP_LOGV(TAG, "Unhandled device_function 0x%02X", dev_fn);
            break;
    }
}

void LuxpowerSNAComponent::send_heartbeat_response_(const uint8_t *pkt, size_t len) {
    send_bytes_(pkt, len);
}

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------
static void build_header_(uint8_t *buf, const char *dongle, uint16_t data_length) {
    uint16_t fl = (uint16_t)(data_length + 14);
    buf[0] = 0xA1; buf[1] = 0x1A;
    buf[2] = 0x02; buf[3] = 0x00;
    buf[4] = fl & 0xFF; buf[5] = fl >> 8;
    buf[6] = 0x01;
    buf[7] = LUX_TCP_TRANSLATED_DATA;
    memcpy(buf + 8, dongle, 10);
    buf[18] = data_length & 0xFF;
    buf[19] = data_length >> 8;
}

void LuxpowerSNAComponent::send_read_input_(uint16_t start_reg, uint16_t count) {
    uint8_t pkt[38];
    build_header_(pkt, dongle_serial_.c_str(), 18);
    uint8_t *df = pkt + 20;
    df[0] = LUX_ACTION_WRITE;
    df[1] = LUX_FN_READ_INPUT;
    memcpy(df + 2, inverter_serial_.c_str(), 10);
    df[12] = start_reg & 0xFF; df[13] = start_reg >> 8;
    df[14] = count & 0xFF;     df[15] = count >> 8;
    uint16_t crc = crc16_(df, 16);
    pkt[36] = crc & 0xFF; pkt[37] = crc >> 8;
    ESP_LOGD(TAG, "READ_INPUT reg=%u count=%u", start_reg, count);
    send_bytes_(pkt, 38);
}

void LuxpowerSNAComponent::send_read_hold_(uint16_t start_reg, uint16_t count) {
    uint8_t pkt[38];
    build_header_(pkt, dongle_serial_.c_str(), 18);
    uint8_t *df = pkt + 20;
    df[0] = LUX_ACTION_WRITE;
    df[1] = LUX_FN_READ_HOLD;
    memcpy(df + 2, inverter_serial_.c_str(), 10);
    df[12] = start_reg & 0xFF; df[13] = start_reg >> 8;
    df[14] = count & 0xFF;     df[15] = count >> 8;
    uint16_t crc = crc16_(df, 16);
    pkt[36] = crc & 0xFF; pkt[37] = crc >> 8;
    ESP_LOGD(TAG, "READ_HOLD reg=%u count=%u", start_reg, count);
    send_bytes_(pkt, 38);
}

void LuxpowerSNAComponent::send_write_single_(uint16_t reg, uint16_t value) {
    uint8_t pkt[38];
    build_header_(pkt, dongle_serial_.c_str(), 18);
    uint8_t *df = pkt + 20;
    df[0] = LUX_ACTION_WRITE;
    df[1] = LUX_FN_WRITE_SINGLE;
    memcpy(df + 2, inverter_serial_.c_str(), 10);
    df[12] = reg & 0xFF;   df[13] = reg >> 8;
    df[14] = value & 0xFF; df[15] = value >> 8;
    uint16_t crc = crc16_(df, 16);
    pkt[36] = crc & 0xFF; pkt[37] = crc >> 8;
    ESP_LOGI(TAG, "WRITE_SINGLE reg=%u value=%u", reg, value);
    send_bytes_(pkt, 38);
}

void LuxpowerSNAComponent::queue_write(uint16_t reg, uint16_t value) {
    ESP_LOGD(TAG, "queue_write reg=%u value=%u", reg, value);
    write_queue_.push(WriteCmd{reg, value});
}

// ---------------------------------------------------------------------------
// Process READ_INPUT response
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::process_read_input_(uint16_t start_reg,
                                               const uint8_t *data, size_t data_len) {
    if (data_len < 80) {
        ESP_LOGW(TAG, "READ_INPUT start=%u too small (%u)", start_reg, (unsigned)data_len);
        return;
    }
    switch (start_reg) {
        case 0:   process_bank0_(*reinterpret_cast<const Bank0*>(data)); break;
        case 40:  process_bank1_(*reinterpret_cast<const Bank1*>(data)); break;
        case 80:  process_bank2_(*reinterpret_cast<const Bank2*>(data)); break;
        case 120: process_bank3_(*reinterpret_cast<const Bank3*>(data)); break;
        case 160: process_bank4_(*reinterpret_cast<const Bank4*>(data)); break;
        default:
            ESP_LOGW(TAG, "Unknown INPUT bank start_reg=%u", start_reg);
            break;
    }
}

void LuxpowerSNAComponent::process_read_hold_(uint16_t start_reg,
                                              const uint8_t *data, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        uint16_t reg = start_reg + i;
        if (reg >= 240) break;
        hold_regs_[reg] = (uint16_t)(data[i*2] | (data[i*2+1] << 8));
    }
    ESP_LOGD(TAG, "READ_HOLD reg=%u count=%u cached", start_reg, count);
}

void LuxpowerSNAComponent::process_write_single_(uint16_t reg, uint16_t value) {
    ESP_LOGI(TAG, "WRITE_SINGLE confirmed reg=%u value=%u", reg, value);
    if (reg < 240) {
        hold_regs_[reg] = value;
        notify_hold_listeners_();
    }
    state_ = State::IDLE;
}

void LuxpowerSNAComponent::notify_hold_listeners_() {
    for (auto *sw  : switches_) sw->on_hold_update(hold_regs_);
    for (auto *num : numbers_)  num->on_hold_update(hold_regs_);
    for (auto *t   : times_)    t->on_hold_update(hold_regs_);
}

// ---------------------------------------------------------------------------
// Bank 0 processing
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::process_bank0_(const Bank0 &d) {
    pub(pv_v1_, d.v_pv_1 / 10.0f);
    pub(pv_v2_, d.v_pv_2 / 10.0f);
    pub(pv_v3_, d.v_pv_3 / 10.0f);
    pub(bat_v_,    d.v_bat / 10.0f);
    pub(bat_soc_,  d.soc);
    pub(bat_soh_,  d.soh);
    pub(internal_fault_, (float)d.internal_fault);
    pub(pv_p1_, d.p_pv_1);
    pub(pv_p2_, d.p_pv_2);
    pub(pv_p3_, d.p_pv_3);
    pub(pv_total_, (float)(d.p_pv_1 + d.p_pv_2 + d.p_pv_3));
    pub(bat_chg_,    d.p_charge);
    pub(bat_dischg_, d.p_discharge);
    pub(grid_v_r_,    d.v_ac_r / 10.0f);
    pub(grid_v_s_,    d.v_ac_s / 10.0f);
    pub(grid_v_t_,    d.v_ac_t / 10.0f);
    pub(grid_v_live_, d.v_ac_r / 10.0f);
    pub(grid_freq_,   d.f_ac / 100.0f);
    pub(p_inv_,        d.p_inv);
    pub(p_rec_,        d.p_rec);
    pub(rms_current_,  d.rms_current / 100.0f);
    pub(pf_,           d.pf / 1000.0f);
    pub(eps_v_r_,  d.v_eps_r / 10.0f);
    pub(eps_v_s_,  d.v_eps_s / 10.0f);
    pub(eps_v_t_,  d.v_eps_t / 10.0f);
    pub(eps_freq_, d.f_eps / 100.0f);
    pub(p_to_eps_, d.p_to_eps);
    pub(p_to_grid_, d.p_to_grid);
    pub(p_to_user_, d.p_to_user);
    pub(e_pv1_day_,     d.e_pv_1_day / 10.0f);
    pub(e_pv2_day_,     d.e_pv_2_day / 10.0f);
    pub(e_pv3_day_,     d.e_pv_3_day / 10.0f);
    pub(e_pv_day_total_, (d.e_pv_1_day + d.e_pv_2_day + d.e_pv_3_day) / 10.0f);
    pub(e_inv_day_,     d.e_inv_day / 10.0f);
    pub(e_rec_day_,     d.e_rec_day / 10.0f);
    pub(e_chg_day_,     d.e_chg_day / 10.0f);
    pub(e_dischg_day_,  d.e_dischg_day / 10.0f);
    pub(e_eps_day_,     d.e_eps_day / 10.0f);
    pub(e_to_grid_day_, d.e_to_grid_day / 10.0f);
    pub(e_to_user_day_, d.e_to_user_day / 10.0f);
    pub(v_bus1_, d.v_bus_1 / 10.0f);
    pub(v_bus2_, d.v_bus_2 / 10.0f);
    float home_live = (float)d.p_to_user - d.p_rec + d.p_inv - d.p_to_grid;
    float home_day  = (d.e_to_user_day - d.e_rec_day + d.e_inv_day - d.e_to_grid_day) / 10.0f;
    pub(p_home_,   (float)(d.p_to_user - d.p_rec));
    pub(bat_flow_, (d.p_discharge > 0) ? -(float)d.p_discharge : (float)d.p_charge);
    pub(grid_flow_,(d.p_to_user > 0)   ? -(float)d.p_to_user   : (float)d.p_to_grid);
    pub(home_live_, home_live);
    pub(home_day_,  home_day);
    if (d.status < 193 && STATUS_TEXTS[d.status] && strlen(STATUS_TEXTS[d.status]) > 0) {
        pub(lux_status_text_, STATUS_TEXTS[d.status]);
    } else {
        pub(lux_status_text_, "Unknown Status");
    }
}

// ---------------------------------------------------------------------------
// Bank 1 processing
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::process_bank1_(const Bank1 &d) {
    pub(e_pv1_all_,      d.e_pv_1_all / 10.0f);
    pub(e_pv2_all_,      d.e_pv_2_all / 10.0f);
    pub(e_pv3_all_,      d.e_pv_3_all / 10.0f);
    pub(e_pv_all_total_, (d.e_pv_1_all + d.e_pv_2_all + d.e_pv_3_all) / 10.0f);
    pub(e_inv_all_,      d.e_inv_all / 10.0f);
    pub(e_rec_all_,      d.e_rec_all / 10.0f);
    pub(e_chg_all_,      d.e_chg_all / 10.0f);
    pub(e_dischg_all_,   d.e_dischg_all / 10.0f);
    pub(e_eps_all_,      d.e_eps_all / 10.0f);
    pub(e_to_grid_all_,  d.e_to_grid_all / 10.0f);
    pub(e_to_user_all_,  d.e_to_user_all / 10.0f);
    pub(fault_code_,   (float)d.fault_code);
    pub(warning_code_, (float)d.warning_code);
    pub(t_inner_, (float)d.t_inner);
    pub(t_rad1_,  (float)d.t_rad_1);
    pub(t_rad2_,  (float)d.t_rad_2);
    pub(t_bat_,   (float)d.t_bat);
    pub(uptime_,  (float)d.uptime);
    float home_total = (d.e_to_user_all - d.e_rec_all + d.e_inv_all - d.e_to_grid_all) / 10.0f;
    pub(home_total_, home_total);
}

// ---------------------------------------------------------------------------
// Bank 2 processing
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::process_bank2_(const Bank2 &d) {
    pub(bms_max_chg_,   d.max_chg_curr / 10.0f);
    pub(bms_max_dischg_,d.max_dischg_curr / 10.0f);
    pub(chg_volt_ref_,  d.charge_volt_ref / 10.0f);
    pub(dischg_cut_v_,  d.dischg_cut_volt / 10.0f);
    pub(bat_status_inv_,(float)d.bat_status_inv);
    pub(bat_count_,     (float)d.bat_count);
    pub(bat_cap_ah_,    (float)d.bat_capacity);
    int16_t bc = d.bat_current;
    pub(bat_curr_, bc / 10.0f);
    pub(max_cell_v_, d.max_cell_volt / 1000.0f);
    pub(min_cell_v_, d.min_cell_volt / 1000.0f);
    int16_t max_t = d.max_cell_temp;
    int16_t min_t = d.min_cell_temp;
    if (max_t & 0x8000) max_t -= 0x10000;
    if (min_t & 0x8000) min_t -= 0x10000;
    pub(max_cell_t_, max_t / 10.0f);
    pub(min_cell_t_, min_t / 10.0f);
    pub(bat_cycles_, (float)d.bat_cycle_count);
    pub(p_load2_,    (float)d.p_load2);
    uint8_t bs = (uint8_t)(d.bat_status_inv < 17 ? d.bat_status_inv : 16);
    if (BAT_STATUS_TEXTS[bs] && strlen(BAT_STATUS_TEXTS[bs]) > 0) {
        pub(lux_bat_status_text_, BAT_STATUS_TEXTS[bs]);
    } else {
        pub(lux_bat_status_text_, "Unknown Battery Status");
    }
}

// ---------------------------------------------------------------------------
// Bank 3 processing
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::process_bank3_(const Bank3 &d) {
    pub(gen_v_,     d.gen_input_volt / 10.0f);
    pub(gen_freq_,  d.gen_input_freq / 100.0f);
    int16_t gen_p = (d.gen_power_watt < 125) ? 0 : d.gen_power_watt;
    pub(gen_p_,     (float)gen_p);
    pub(gen_p_day_, d.gen_power_day / 10.0f);
    pub(gen_p_all_, d.gen_power_all / 10.0f);
    pub(eps_l1_v_,  d.eps_L1_volt / 10.0f);
    pub(eps_l2_v_,  d.eps_L2_volt / 10.0f);
    pub(eps_l1_w_,  (float)d.eps_L1_watt);
    pub(eps_l2_w_,  (float)d.eps_L2_watt);
}

// ---------------------------------------------------------------------------
// Bank 4 processing
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::process_bank4_(const Bank4 &d) {
    pub(p_load_ongrid_, (float)d.p_load_ongrid);
    pub(e_load_day_,    d.e_load_day / 10.0f);
    pub(e_load_all_,    d.e_load_all_l / 10.0f);
}

// ---------------------------------------------------------------------------
// LuxpowerSNASwitch
// ---------------------------------------------------------------------------
void LuxpowerSNASwitch::write_state(bool state) {
    if (!parent_) return;
    uint16_t cur = parent_->get_hold_register(register_addr_);
    uint16_t new_val = state ? (cur | bitmask_) : (cur & ~bitmask_);
    parent_->queue_write(register_addr_, new_val);
    publish_state(state);
}

void LuxpowerSNASwitch::on_hold_update(const uint16_t *hold_regs) {
    if (register_addr_ >= 240) return;
    bool state = (hold_regs[register_addr_] & bitmask_) == bitmask_;
    publish_state(state);
}

// ---------------------------------------------------------------------------
// LuxpowerSNANumber
// ---------------------------------------------------------------------------
void LuxpowerSNANumber::control(float value) {
    if (!parent_) return;
    uint16_t cur = parent_->get_hold_register(register_addr_);
    uint16_t new_val;
    if (is_signed_) {
        int16_t sv = (int16_t)roundf(value * divisor_);
        new_val = to_unsigned(sv);
    } else {
        uint16_t ival = (uint16_t)roundf(value * divisor_);
        new_val = (cur & ~bitmask_) | ((ival << bitshift_) & bitmask_);
    }
    parent_->queue_write(register_addr_, new_val);
    publish_state(value);
}

void LuxpowerSNANumber::on_hold_update(const uint16_t *hold_regs) {
    if (register_addr_ >= 240) return;
    uint16_t raw = hold_regs[register_addr_];
    float displayed;
    if (is_signed_) {
        int16_t sv = to_signed(raw);
        displayed = (float)sv / divisor_;
    } else {
        displayed = (float)((raw & bitmask_) >> bitshift_) / divisor_;
    }
    publish_state(displayed);
}

// ---------------------------------------------------------------------------
// LuxpowerSNAButton – just delegates to parent
// ---------------------------------------------------------------------------
void LuxpowerSNAButton::press_action() {
    if (!parent_) return;
    switch (action_) {
        case Action::RESTART:    parent_->action_restart();      break;
        case Action::RESET_ALL:  parent_->action_reset_all();    break;
        case Action::SCAN_DONGLE:parent_->action_scan_dongle();  break;
    }
}

// ---------------------------------------------------------------------------
// Hub button actions
// ---------------------------------------------------------------------------
void LuxpowerSNAComponent::action_restart() {
    ESP_LOGW(TAG, "Inverter RESTART – writing reg 11 = 128");
    queue_write(11, 128);
}

void LuxpowerSNAComponent::action_reset_all() {
    ESP_LOGW(TAG, "Inverter RESET ALL SETTINGS – writing reg 11 = 2");
    queue_write(11, 2);
}

// ---------------------------------------------------------------------------
// LuxpowerSNATime
// ---------------------------------------------------------------------------
void LuxpowerSNATime::on_hold_update(const uint16_t *hold_regs) {
    if (register_addr_ >= 240) return;
    uint16_t raw = hold_regs[register_addr_];
    uint8_t hour, minute;
    decode_(raw, hour, minute);
    if (hour > 23) hour = 0;
    if (minute > 59) minute = 0;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", hour, minute);
    current_hhmm_ = buf;
}

void LuxpowerSNATime::set_time(const std::string &hhmm) {
    if (!parent_) return;
    if (hhmm.size() < 5 || hhmm[2] != ':') {
        ESP_LOGW(TAG, "Time '%s' invalid format, expected HH:MM", hhmm.c_str());
        return;
    }
    int hour   = atoi(hhmm.substr(0, 2).c_str());
    int minute = atoi(hhmm.substr(3, 2).c_str());
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        ESP_LOGW(TAG, "Time '%s' out of range", hhmm.c_str());
        return;
    }
    uint16_t encoded = encode_((uint8_t)hour, (uint8_t)minute);
    ESP_LOGI(TAG, "Set time reg=%u → %02d:%02d (raw=0x%04X)", register_addr_, hour, minute, encoded);
    parent_->queue_write(register_addr_, encoded);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    current_hhmm_ = buf;
}

// ---------------------------------------------------------------------------
// Scan LAN for Lux dongle – FreeRTOS task, batch parallel connect
// ESP32-S2 single-core: batch=8, safe for default lwip MAX_SOCKETS=16
//
// IMPORTANT: ScanParams is defined in the header (inside LuxpowerSNAComponent).
// Do NOT redefine it here — that would be a redefinition error.
// ---------------------------------------------------------------------------

void LuxpowerSNAComponent::action_scan_dongle() {
    // Always log so we know the button was pressed regardless of guard outcome
    ESP_LOGI(TAG, "Scan button pressed (scanning_=%d dongle_len=%u inv_len=%u)",
             (int)scanning_,
             (unsigned)dongle_serial_.size(),
             (unsigned)inverter_serial_.size());

    if (scanning_) {
        ESP_LOGW(TAG, "Scan already in progress, ignoring");
        return;
    }

    // Guard: dongle + inverter serial must be set first
    if (dongle_serial_.size() != 10) {
        ESP_LOGW(TAG, "Cannot scan: dongle_serial not configured (need 10 chars, got %u)",
                 (unsigned)dongle_serial_.size());
        pub(scan_status_text_, "Error: set dongle serial first");
        return;
    }
    if (inverter_serial_.size() != 10) {
        ESP_LOGW(TAG, "Cannot scan: inverter_serial not configured (need 10 chars, got %u)",
                 (unsigned)inverter_serial_.size());
        pub(scan_status_text_, "Error: set inverter serial first");
        return;
    }

    // Get local subnet from STA interface
    esp_netif_ip_info_t ip_info{};
    uint8_t a = 192, b = 168, c = 1, self_octet = 0;
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info) == ESP_OK
        && ip_info.ip.addr != 0) {
        a          = ip4_addr1(&ip_info.ip);
        b          = ip4_addr2(&ip_info.ip);
        c          = ip4_addr3(&ip_info.ip);
        self_octet = ip4_addr4(&ip_info.ip);
    } else {
        ESP_LOGW(TAG, "Cannot read STA IP, falling back to 192.168.1.0/24");
    }

    // Close main socket – scan task opens its own batch of sockets
    close_socket_();

    scanning_            = true;
    scan_start_ms_       = millis();   // start watchdog timer
    scan_result_pending_ = false;
    scan_found_          = false;
    deferred_apply_      = false;
    found_ip_buf_[0]     = '\0';

    pub(scan_status_text_, "Scanning...");
    ESP_LOGI(TAG, "Starting dongle scan on %u.%u.%u.0/24 port %u", a, b, c, port_);

    // Heap-allocate params; task frees them before calling vTaskDelete
    ScanParams *params = new ScanParams{a, b, c, self_octet, port_, this};

    BaseType_t ret = xTaskCreate(
        scan_task_fn_,
        "lux_scan",
        6144,   // stack: ample for 8 sockets + snprintf buffers on S2
        params,
        1,      // priority 1 – time-sliced with main loop on single-core S2
        nullptr
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed for lux_scan");
        delete params;
        scanning_ = false;
        pub(scan_status_text_, "Error: task create failed");
    }
}

// Static trampoline – runs in FreeRTOS task context
void LuxpowerSNAComponent::scan_task_fn_(void *param) {
    ScanParams *p = static_cast<ScanParams *>(param);
    LuxpowerSNAComponent *self = p->hub;
    uint8_t  a          = p->a;
    uint8_t  b          = p->b;
    uint8_t  c          = p->c;
    uint8_t  self_octet = p->self_octet;
    uint16_t port       = p->port;
    delete p;

    self->do_scan_(a, b, c, self_octet, port);
    vTaskDelete(nullptr);
}

void LuxpowerSNAComponent::do_scan_(uint8_t a, uint8_t b, uint8_t c,
                                    uint8_t self_octet, uint16_t port) {
    // Sequential scan — one socket at a time.
    // Parallel batching risks exhausting the lwip pool when other components
    // (jk_modbus, mqtt, etc.) are holding sockets concurrently. Sequential is
    // slower (~15 s worst-case) but 100% reliable regardless of pool pressure.
    for (uint16_t i = 1; i <= 254; i++) {
        if ((uint8_t)i == self_octet) continue;

        char ipbuf[20];
        snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", a, b, c, (uint8_t)i);

        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            // Pool temporarily full — yield and retry this address
            ESP_LOGD(TAG, "[lux_scan] socket() failed for %s (errno=%d), retrying", ipbuf, errno);
            vTaskDelay(pdMS_TO_TICKS(20));
            i--;  // retry same address
            continue;
        }

        // Non-blocking connect
        int nb = 1;
        ioctl(fd, FIONBIO, &nb);

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (inet_pton(AF_INET, ipbuf, &addr.sin_addr) != 1) {
            close(fd);
            continue;
        }
        connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));

        // Wait up to 150 ms for connect to complete
        fd_set wfds, efds;
        FD_ZERO(&wfds); FD_ZERO(&efds);
        FD_SET(fd, &wfds); FD_SET(fd, &efds);
        struct timeval tv{0, 150000};  // 150 ms
        int sel = select(fd + 1, nullptr, &wfds, &efds, &tv);

        bool connected = false;
        if (sel > 0 && FD_ISSET(fd, &wfds)) {
            int err = 0;
            socklen_t el = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
            connected = (err == 0);
        }
        close(fd);

        if (connected) {
            ESP_LOGI(TAG, "[lux_scan] Dongle candidate found at %s:%u", ipbuf, port);
            memcpy(found_ip_buf_, ipbuf, sizeof(found_ip_buf_));
            scan_found_          = true;
            scan_result_pending_ = true;
            return;
        }

        // Yield briefly so main loop stays responsive
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    ESP_LOGW(TAG, "[lux_scan] Scan complete: no dongle found on %u.%u.%u.0/24 port %u",
             a, b, c, port);
    scan_found_          = false;
    scan_result_pending_ = true;
}

// ---------------------------------------------------------------------------
// NVS host persistence
// Saves/loads host_ independently of MQTT/HA so MQTT reconnect cannot clear it.
// Uses a fixed 4-byte hash key unique to this component.
// ---------------------------------------------------------------------------
static const uint32_t LUX_HOST_PREF_KEY = 0x4C555848UL;  // "LUXH"

void LuxpowerSNAComponent::save_host_prefs_() {
    ESPPreferenceObject pref = global_preferences->make_preference<std::string>(LUX_HOST_PREF_KEY, true);
    pref.save(&host_);
    global_preferences->sync();
    ESP_LOGI(TAG, "Host saved to NVS: %s", host_.c_str());
}

void LuxpowerSNAComponent::load_host_prefs_() {
    ESPPreferenceObject pref = global_preferences->make_preference<std::string>(LUX_HOST_PREF_KEY, true);
    std::string saved;
    if (pref.load(&saved) && !saved.empty()) {
        host_ = saved;
        ESP_LOGI(TAG, "Host loaded from NVS: %s", host_.c_str());
    }
}

void LuxpowerSNAComponent::apply_scanned_host_(const std::string &ip) {
    ESP_LOGI(TAG, "Applying scanned host: %s", ip.c_str());
    this->set_host(ip);
    if (this->is_config_ready()) {
        this->reconnect();
    }
}

}  // namespace luxpower_sna
}  // namespace esphome