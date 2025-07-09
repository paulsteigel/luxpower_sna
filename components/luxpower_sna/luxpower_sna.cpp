// esphome_config/custom_components/luxpower_sna/luxpower_sna.cpp

#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#include "esphome/components/socket/socket.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Define constants for the protocol
static const uint8_t FRAME_START[2] = {0xAA, 0x55};
static const uint8_t PROTOCOL_VERSION = 0x10;
static const uint8_t READ_HOLDING_REGISTERS = 0x03;
static const uint8_t WRITE_HOLDING_REGISTERS = 0x10;

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

  // Create a new socket for this update cycle
  this->socket_ = socket::socket(AF_INET, SOCK_STREAM, 0);
  if (this->socket_ == nullptr) {
    ESP_LOGW(TAG, "Could not create socket");
    this->status_set_warning();
    return;
  }

  // --- FIX #1: Use setsockopt for timeouts ---
  // The 'set_timeout' function does not exist. We must use the low-level setsockopt.
  struct timeval tv;
  tv.tv_sec = 5;  // 5 seconds
  tv.tv_usec = 0; // 0 microseconds
  if (this->socket_->setsockopt(SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    ESP_LOGW(TAG, "Failed to set socket recv timeout");
    // Continue anyway, but log the warning
  }
  if (this->socket_->setsockopt(SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    ESP_LOGW(TAG, "Failed to set socket send timeout");
    // Continue anyway, but log the warning
  }

  // Set up the address structure
  sockaddr_storage address;
  socklen_t address_len = sizeof(address);
  address_len = socket::set_sockaddr(reinterpret_cast<sockaddr *>(&address), sizeof(address), this->host_, this->port_);
  
  if (address_len == 0) {
    ESP_LOGW(TAG, "Could not resolve host %s", this->host_.c_str());
    this->socket_->close();
    this->socket_ = nullptr;
    this->status_set_warning();
    return;
  }

  // --- FIX #2: 'connect' is a member function of the Socket class ---
  // The compiler confirms that connect() must be called on the socket object itself.
  if (this->socket_->connect(reinterpret_cast<sockaddr *>(&address), address_len) != 0) {
    ESP_LOGW(TAG, "Could not connect to %s:%u. Error: %s", this->host_.c_str(), this->port_, strerror(errno));
    this->socket_->close();
    this->socket_ = nullptr;
    this->status_set_warning();
    return;
  }

  ESP_LOGD(TAG, "Connection successful to %s:%u", this->host_.c_str(), this->port_);

  // TODO: Implement packet creation, sending, and response reading here.

  // Clean up the socket for this update cycle
  this->socket_->close();
  this->socket_ = nullptr;
  ESP_LOGD(TAG, "Socket closed, update finished.");
  this->status_clear_warning();
}

}  // namespace luxpower_sna
}  // namespace esphome
