#include "ota_http_request.h"

#include <cctype>

#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"   // ← needed for global_preferences->sync()

#include "esphome/components/md5/md5.h"
#include "esphome/components/watchdog/watchdog.h"

// ── ESP8266: access live WiFi credentials from the SDK ──────────────────────
// WiFi.SSID() / WiFi.psk() read directly from the SDK's station config, which
// lives in RAM and is NOT affected by spi_flash_erase_sector(). This is the
// only reliable source of captive-portal credentials at OTA time.
// ────────────────────────────────────────────────────────────────────────────
#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#endif

namespace esphome {
namespace http_request {

static const char *const TAG = "http_request.ota";

// ── Why WiFi credentials are lost after HTTP OTA on ESP8266 ─────────────────
//
// Sequence of events during HTTP OTA:
//
//   1. backend->begin(content_length)
//        └─ esp8266::preferences_prevent_write(true)   [blocks ESPHome pref writes]
//        └─ start_address  =  FS_start - rounded_firmware_size
//           For esp01_1m (no SPIFFS):
//             FS_start         = 0x40300000 (top of 1 MB flash)
//             firmware gz ~280KB → rounded = 0x47000
//             start_address    = 0xB9000
//
//   2. write loop → spi_flash_erase_sector() called for EVERY sector
//        from start_address (0xB9000) up to 0x100000.
//        ESPHome preference sector for 1 MB flash  = sector 255 = 0xFF000
//        0xFF000 lies inside [0xB9000, 0x100000) → ERASED here.
//        preferences_prevent_write=true only stops ESPHome from WRITING;
//        it does NOT protect the sector from physical erasure.
//
//   3. backend->end()
//        └─ writes eboot command to RTC (copy staging → 0x00000)
//        └─ esp8266::preferences_prevent_write(false)
//
//   4. App.safe_reboot()  [note: does NOT force a global_preferences->sync()]
//
//   5. eboot copies firmware → does NOT touch the preferences sector
//
//   6. New firmware boots → reads preferences sector (now blank) → no WiFi creds
//      → captive portal starts.
//
// Fix (two-part):
//   A. BEFORE OTA  : save live SDK credentials (WiFi.SSID / WiFi.psk) to RTC
//                    user-memory (not affected by sector erasure).
//   B. BEFORE REBOOT: call global_preferences->sync() so that any in-memory
//                    preference data (including WiFi creds if still valid) is
//                    flushed back to the freshly erased sector.
//   C. ON NEXT BOOT : a high-priority on_boot lambda (see YAML snippet below)
//                    reads the RTC backup and feeds credentials back to
//                    global_wifi_component BEFORE WiFi component's own setup()
//                    runs, so connection proceeds without captive portal.
//
// YAML snippet to add to your device config (fires at priority 700, which is
// BEFORE WiFi component setup at setup_priority 300):
//
// esphome:
//   on_boot:
//     - priority: 700
//       then:
//         - lambda: |-
//             #ifdef USE_ESP8266
//             OtaWifiRtcStore s;
//             ESP.rtcUserMemoryRead(OTA_WIFI_RTC_SLOT,
//                                   reinterpret_cast<uint32_t *>(&s),
//                                   OTA_WIFI_RTC_WORDS);
//             if (s.magic == OTA_WIFI_RTC_MAGIC && strlen(s.ssid) > 0) {
//               ESP_LOGI("wifi_rtc", "Restoring WiFi '%s' from RTC backup", s.ssid);
//               esphome::wifi::WiFiAP ap;
//               ap.set_ssid(s.ssid);
//               ap.set_password(s.psk);
//               global_wifi_component->add_sta(ap);
//               // clear magic so normal boots are unaffected
//               s.magic = 0;
//               ESP.rtcUserMemoryWrite(OTA_WIFI_RTC_SLOT,
//                                      reinterpret_cast<uint32_t *>(&s),
//                                      OTA_WIFI_RTC_WORDS);
//             }
//             #endif
// ────────────────────────────────────────────────────────────────────────────

// ── Part A: save live WiFi credentials to RTC memory ────────────────────────
#ifdef USE_ESP8266
void OtaHttpRequestComponent::save_wifi_to_rtc_() {
  OtaWifiRtcStore store;
  memset(&store, 0, sizeof(store));

  String ssid = WiFi.SSID();
  String psk  = WiFi.psk();

  if (ssid.length() == 0) {
    ESP_LOGW(TAG, "No active WiFi SSID to backup – captive portal may restart after OTA");
    return;
  }

  store.magic = OTA_WIFI_RTC_MAGIC;
  strncpy(store.ssid, ssid.c_str(), sizeof(store.ssid) - 1);
  strncpy(store.psk,  psk.c_str(),  sizeof(store.psk)  - 1);

  ESP.rtcUserMemoryWrite(OTA_WIFI_RTC_SLOT,
                         reinterpret_cast<uint32_t *>(&store),
                         OTA_WIFI_RTC_WORDS);

  ESP_LOGI(TAG, "WiFi '%s' backed up to RTC slot %d (%d words)",
           store.ssid, OTA_WIFI_RTC_SLOT, OTA_WIFI_RTC_WORDS);
}
#endif  // USE_ESP8266
// ────────────────────────────────────────────────────────────────────────────

void OtaHttpRequestComponent::dump_config() { ESP_LOGCONFIG(TAG, "Over-The-Air updates via HTTP request"); };

void OtaHttpRequestComponent::set_md5_url(const std::string &url) {
  if (!this->validate_url_(url)) {
    this->md5_url_.clear();  // URL was not valid; prevent flashing until it is
    return;
  }
  this->md5_url_ = url;
  this->md5_expected_.clear();  // to be retrieved later
}

void OtaHttpRequestComponent::set_url(const std::string &url) {
  if (!this->validate_url_(url)) {
    this->url_.clear();  // URL was not valid; prevent flashing until it is
    return;
  }
  this->url_ = url;
}

void OtaHttpRequestComponent::flash() {
  if (this->url_.empty()) {
    ESP_LOGE(TAG, "URL not set; cannot start update");
    return;
  }

#ifdef USE_ESP8266
  // ── Part A: backup WiFi credentials BEFORE staging erases preference sector.
  // WiFi.SSID()/WiFi.psk() read from SDK RAM – unaffected by flash ops.
  this->save_wifi_to_rtc_();
#endif

  ESP_LOGI(TAG, "Starting update");
#ifdef USE_OTA_STATE_LISTENER
  this->notify_state_(ota::OTA_STARTED, 0.0f, 0);
#endif

  auto ota_status = this->do_ota_();

  switch (ota_status) {
    case ota::OTA_RESPONSE_OK:
#ifdef USE_OTA_STATE_LISTENER
      this->notify_state_(ota::OTA_COMPLETED, 100.0f, ota_status);
#endif
      // ── Part B: flush any in-memory preference state back to flash.
      // backend->end() already called preferences_prevent_write(false), so
      // sync() is now permitted. This restores whatever ESPHome data (WiFi
      // prefs, schedules, etc.) is still held in RAM into the sector that
      // OTA staging may have erased.
      global_preferences->sync();
      ESP_LOGD(TAG, "Preferences synced before reboot");

      delay(10);
      App.safe_reboot();
      break;

    default:
#ifdef USE_OTA_STATE_LISTENER
      this->notify_state_(ota::OTA_ERROR, 0.0f, ota_status);
#endif
      this->md5_computed_.clear();  // will be reset at next attempt
      this->md5_expected_.clear();  // will be reset at next attempt
      break;
  }
}

void OtaHttpRequestComponent::cleanup_(ota::OTABackendPtr backend, const std::shared_ptr<HttpContainer> &container) {
  if (this->update_started_) {
    ESP_LOGV(TAG, "Aborting OTA backend");
    backend->abort();
  }
  ESP_LOGV(TAG, "Aborting HTTP connection");
  container->end();
};

uint8_t OtaHttpRequestComponent::do_ota_() {
  uint8_t buf[OtaHttpRequestComponent::HTTP_RECV_BUFFER + 1];
  uint32_t last_progress = 0;
  uint32_t update_start_time = millis();
  md5::MD5Digest md5_receive;
  char md5_receive_str[33];

  if (this->md5_expected_.empty() && !this->http_get_md5_()) {
    return OTA_MD5_INVALID;
  }

  ESP_LOGD(TAG, "MD5 expected: %s", this->md5_expected_.c_str());

  auto url_with_auth = this->get_url_with_auth_(this->url_);
  if (url_with_auth.empty()) {
    return OTA_BAD_URL;
  }
  ESP_LOGVV(TAG, "url_with_auth: %s", url_with_auth.c_str());
  ESP_LOGI(TAG, "Connecting to: %s", this->url_.c_str());

  auto container = this->parent_->get(url_with_auth);

  if (container == nullptr || container->status_code != HTTP_STATUS_OK) {
    return OTA_CONNECTION_ERROR;
  }

  // we will compute MD5 on the fly for verification -- Arduino OTA seems to ignore it
  md5_receive.init();
  ESP_LOGV(TAG, "MD5Digest initialized, OTA backend begin");
  auto backend = ota::make_ota_backend();
  auto error_code = backend->begin(container->content_length);
  if (error_code != ota::OTA_RESPONSE_OK) {
    ESP_LOGW(TAG, "backend->begin error: %d", error_code);
    this->cleanup_(std::move(backend), container);
    return error_code;
  }

  // NOTE: HttpContainer::read() has non-BSD socket semantics - see http_request.h
  // Use http_read_loop_result() helper instead of checking return values directly
  uint32_t last_data_time = millis();
  const uint32_t read_timeout = this->parent_->get_timeout();

  while (container->get_bytes_read() < container->content_length) {
    // read a maximum of chunk_size bytes into buf. (real read size returned, or negative error code)
    int bufsize_or_error = container->read(buf, OtaHttpRequestComponent::HTTP_RECV_BUFFER);
    ESP_LOGVV(TAG, "bytes_read_ = %u, body_length_ = %u, bufsize_or_error = %i", container->get_bytes_read(),
              container->content_length, bufsize_or_error);

    // feed watchdog and give other tasks a chance to run
    App.feed_wdt();
    yield();

    auto result = http_read_loop_result(bufsize_or_error, last_data_time, read_timeout, container->is_read_complete());
    if (result == HttpReadLoopResult::RETRY)
      continue;
    // For non-chunked responses, COMPLETE is unreachable (loop condition checks bytes_read < content_length).
    // For chunked responses, the decoder sets content_length = bytes_read when the final chunk arrives,
    // which causes the loop condition to terminate. But COMPLETE can still be returned if the decoder
    // finishes mid-read, so this is needed for correctness.
    if (result == HttpReadLoopResult::COMPLETE)
      break;
    if (result != HttpReadLoopResult::DATA) {
      if (result == HttpReadLoopResult::TIMEOUT) {
        ESP_LOGE(TAG, "Timeout reading data");
      } else {
        ESP_LOGE(TAG, "Error reading data: %d", bufsize_or_error);
      }
      this->cleanup_(std::move(backend), container);
      return OTA_CONNECTION_ERROR;
    }

    // At this point bufsize_or_error > 0, so it's a valid size
    if (bufsize_or_error <= OtaHttpRequestComponent::HTTP_RECV_BUFFER) {
      // add read bytes to MD5
      md5_receive.add(buf, bufsize_or_error);

      // write bytes to OTA backend
      this->update_started_ = true;
      error_code = backend->write(buf, bufsize_or_error);
      if (error_code != ota::OTA_RESPONSE_OK) {
        // error code explanation available at
        // https://github.com/esphome/esphome/blob/dev/esphome/components/ota/ota_backend.h
        ESP_LOGE(TAG, "Error code (%02X) writing binary data to flash at offset %d and size %d", error_code,
                 container->get_bytes_read() - bufsize_or_error, container->content_length);
        this->cleanup_(std::move(backend), container);
        return error_code;
      }
    }

    uint32_t now = millis();
    if ((now - last_progress > 1000) or (container->get_bytes_read() == container->content_length)) {
      last_progress = now;
      float percentage = container->get_bytes_read() * 100.0f / container->content_length;
      ESP_LOGD(TAG, "Progress: %0.1f%%", percentage);
#ifdef USE_OTA_STATE_LISTENER
      this->notify_state_(ota::OTA_IN_PROGRESS, percentage, 0);
#endif
    }
  }  // while

  ESP_LOGI(TAG, "Done in %.0f seconds", float(millis() - update_start_time) / 1000);

  // verify MD5 is as expected and act accordingly
  md5_receive.calculate();
  md5_receive.get_hex(md5_receive_str);
  this->md5_computed_ = md5_receive_str;
  if (strncmp(this->md5_computed_.c_str(), this->md5_expected_.c_str(), MD5_SIZE) != 0) {
    ESP_LOGE(TAG, "MD5 computed: %s - Aborting due to MD5 mismatch", this->md5_computed_.c_str());
    this->cleanup_(std::move(backend), container);
    return ota::OTA_RESPONSE_ERROR_MD5_MISMATCH;
  } else {
    backend->set_update_md5(md5_receive_str);
  }

  container->end();

  // feed watchdog and give other tasks a chance to run
  App.feed_wdt();
  yield();
  delay(100);  // NOLINT

  error_code = backend->end();
  if (error_code != ota::OTA_RESPONSE_OK) {
    ESP_LOGW(TAG, "Error ending update! error_code: %d", error_code);
    this->cleanup_(std::move(backend), container);
    return error_code;
  }

  ESP_LOGI(TAG, "Update complete");
  return ota::OTA_RESPONSE_OK;
}

// URL-encode characters that are not unreserved per RFC 3986 section 2.3.
// This is needed for embedding userinfo (username/password) in URLs safely.
static std::string url_encode(const std::string &str) {
  std::string result;
  result.reserve(str.size());
  for (char c : str) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      result += c;
    } else {
      result += '%';
      result += format_hex_pretty_char((static_cast<uint8_t>(c) >> 4) & 0x0F);
      result += format_hex_pretty_char(static_cast<uint8_t>(c) & 0x0F);
    }
  }
  return result;
}

void OtaHttpRequestComponent::set_password(const std::string &password) { this->password_ = url_encode(password); }
void OtaHttpRequestComponent::set_username(const std::string &username) { this->username_ = url_encode(username); }

std::string OtaHttpRequestComponent::get_url_with_auth_(const std::string &url) {
  if (this->username_.empty() || this->password_.empty()) {
    return url;
  }

  auto start_char = url.find("://");
  if ((start_char == std::string::npos) || (start_char < 4)) {
    ESP_LOGE(TAG, "Incorrect URL prefix");
    return {};
  }

  ESP_LOGD(TAG, "Using basic HTTP authentication");

  start_char += 3;  // skip '://' characters
  auto url_with_auth =
      url.substr(0, start_char) + this->username_ + ":" + this->password_ + "@" + url.substr(start_char);
  return url_with_auth;
}

bool OtaHttpRequestComponent::http_get_md5_() {
  if (this->md5_url_.empty()) {
    return false;
  }

  auto url_with_auth = this->get_url_with_auth_(this->md5_url_);
  if (url_with_auth.empty()) {
    return false;
  }

  ESP_LOGVV(TAG, "url_with_auth: %s", url_with_auth.c_str());
  ESP_LOGI(TAG, "Connecting to: %s", this->md5_url_.c_str());
  auto container = this->parent_->get(url_with_auth);
  if (container == nullptr) {
    ESP_LOGE(TAG, "Failed to connect to MD5 URL");
    return false;
  }
  size_t length = container->content_length;
  if (length == 0) {
    container->end();
    return false;
  }
  if (length < MD5_SIZE) {
    ESP_LOGE(TAG, "MD5 file must be %u bytes; %u bytes reported by HTTP server. Aborting", MD5_SIZE, length);
    container->end();
    return false;
  }

  this->md5_expected_.resize(MD5_SIZE);
  auto result = http_read_fully(container.get(), (uint8_t *) this->md5_expected_.data(), MD5_SIZE, MD5_SIZE,
                                this->parent_->get_timeout());
  container->end();

  if (result.status != HttpReadStatus::OK) {
    if (result.status == HttpReadStatus::TIMEOUT) {
      ESP_LOGE(TAG, "Timeout reading MD5");
    } else {
      ESP_LOGE(TAG, "Error reading MD5: %d", result.error_code);
    }
    return false;
  }
  return true;
}

bool OtaHttpRequestComponent::validate_url_(const std::string &url) {
  if ((url.length() < 8) || !url.starts_with("http") || (url.find("://") == std::string::npos)) {
    ESP_LOGE(TAG, "URL is invalid and/or must be prefixed with 'http://' or 'https://'");
    return false;
  }
  return true;
}

}  // namespace http_request
}  // namespace esphome