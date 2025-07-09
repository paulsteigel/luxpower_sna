// esphome_config/custom_components/luxpower_sna/luxpower_sna.cpp

#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

// We must use the low-level LwIP API directly.
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- LwIP Callback Functions ---
// These functions are called by the LwIP stack when network events occur.
// They must be static or global. We pass 'this' as an argument to get back to our object.

// Called when the connection is successfully established.
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);
  if (err == ERR_OK) {
    ESP_LOGD(TAG, "Connection successful!");
    // TODO: Now that we are connected, set up receive callbacks and send the first packet.
    // For now, we just close the connection to prove it works.
    component->close_connection();
  } else {
    ESP_LOGW(TAG, "Connection failed. Error: %d", err);
    component->close_connection();
  }
  return ERR_OK;
}

// Called when an error occurs on the connection.
static void tcp_error_callback(void *arg, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);
  ESP_LOGW(TAG, "TCP Error. Code: %d. Closing connection.", err);
  component->pcb_ = nullptr; // PCB is already freed by LwIP
  component->status_set_warning();
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNAComponent...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxpowerSNAComponent:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  LOG_UPDATE_INTERVAL(this);
}

void LuxpowerSNAComponent::update() {
  ESP_LOGD(TAG, "Starting update...");

  // If a connection is already in progress, don't start another.
  if (this->pcb_ != nullptr) {
    ESP_LOGD(TAG, "Update called but a connection is already active. Aborting old one.");
    this->close_connection();
  }

  // 1. Create a new TCP Protocol Control Block (PCB)
  this->pcb_ = tcp_new();
  if (this->pcb_ == nullptr) {
    ESP_LOGW(TAG, "Could not create TCP PCB");
    this->status_set_warning();
    return;
  }

  // 2. Set up the argument that will be passed to our callbacks
  tcp_arg(this->pcb_, this);

  // 3. Set up the error callback
  tcp_err(this->pcb_, tcp_error_callback);

  // 4. Resolve the IP address
  ip_addr_t remote_ip;
  err_t err = dns_gethostbyname(this->host_.c_str(), &remote_ip, nullptr, nullptr);
  if (err != ERR_OK && err != ERR_INPROGRESS) {
      ESP_LOGW(TAG, "Could not resolve host %s. Error: %d", this->host_.c_str(), err);
      this->close_connection();
      return;
  }
  // For now, we assume immediate resolution. A more robust solution would handle ERR_INPROGRESS.

  // 5. Initiate the connection
  ESP_LOGD(TAG, "Connecting to %s:%u...", this->host_.c_str(), this->port_);
  err = tcp_connect(this->pcb_, &remote_ip, this->port_, tcp_connected_callback);

  if (err != ERR_OK) {
    ESP_LOGW(TAG, "Could not initiate TCP connection. Error: %d", err);
    this->close_connection();
    return;
  }

  // The connection is now happening asynchronously. The result will be delivered
  // to our `tcp_connected_callback` or `tcp_error_callback`.
  // We clear the warning for now, it will be set again on error.
  this->status_clear_warning();
}

void LuxpowerSNAComponent::close_connection() {
  if (this->pcb_ != nullptr) {
    // Unset callbacks to prevent them from firing during/after close
    tcp_err(this->pcb_, nullptr);
    tcp_sent(this->pcb_, nullptr);
    tcp_recv(this->pcb_, nullptr);
    
    err_t err = tcp_close(this->pcb_);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "Error closing TCP connection: %d. Aborting.", err);
        tcp_abort(this->pcb_);
    }
    this->pcb_ = nullptr;
    ESP_LOGD(TAG, "Connection closed.");
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
