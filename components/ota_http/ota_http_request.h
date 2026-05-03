#pragma once

#include "esphome/components/ota/ota_backend_factory.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include <memory>
#include <string>
#include <utility>

#include "../http_request.h"

// ── ESP8266 WiFi credential backup via RTC memory ──────────────────────────
// RTC user memory (512 bytes / 128 x 4-byte slots) survives software reboot
// but is NOT affected by spi_flash_erase_sector() during OTA staging.
// We use this to preserve captive-portal WiFi credentials across HTTP OTA.
//
// Slot map:
//   64–89  OtaWifiRtcStore (102 bytes → 26 uint32_t words)
//
// On the YAML side add an on_boot trigger (priority > WiFi setup priority 300)
// that reads this struct and feeds credentials back to global_wifi_component
// before the WiFi component runs its own setup(). See README / YAML snippet.
// ───────────────────────────────────────────────────────────────────────────
#ifdef USE_ESP8266
struct OtaWifiRtcStore {
  uint32_t magic;  // must equal OTA_WIFI_RTC_MAGIC to be considered valid
  char     ssid[33];
  char     psk[65];
};
// "OWIF" – arbitrary tag, must match the YAML lambda
static constexpr uint32_t OTA_WIFI_RTC_MAGIC = 0x4F574946;
// First RTC user-memory slot we occupy (slots 0-63 reserved by SDK internally)
static constexpr uint8_t  OTA_WIFI_RTC_SLOT  = 64;
// Number of uint32_t words needed (rounded up)
static constexpr uint8_t  OTA_WIFI_RTC_WORDS = (sizeof(OtaWifiRtcStore) + 3) / 4;
#endif  // USE_ESP8266

namespace esphome {
namespace http_request {

static const uint8_t MD5_SIZE = 32;

enum OtaHttpRequestError : uint8_t {
  OTA_MD5_INVALID = 0x10,
  OTA_BAD_URL = 0x11,
  OTA_CONNECTION_ERROR = 0x12,
};

class OtaHttpRequestComponent final : public ota::OTAComponent, public Parented<HttpRequestComponent> {
 public:
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_md5_url(const std::string &md5_url);
  void set_md5(const std::string &md5) { this->md5_expected_ = md5; }
  void set_password(const std::string &password);
  void set_url(const std::string &url);
  void set_username(const std::string &username);

  std::string md5_computed() { return this->md5_computed_; }
  std::string md5_expected() { return this->md5_expected_; }

  void flash();

 protected:
  void cleanup_(ota::OTABackendPtr backend, const std::shared_ptr<HttpContainer> &container);
  uint8_t do_ota_();
  std::string get_url_with_auth_(const std::string &url);
  bool http_get_md5_();
  bool validate_url_(const std::string &url);

#ifdef USE_ESP8266
  // Save current WiFi SSID/PSK (from the live SDK connection) into RTC memory
  // so they survive the flash-sector erasure that OTA staging causes.
  void save_wifi_to_rtc_();
#endif

  std::string md5_computed_{};
  std::string md5_expected_{};
  std::string md5_url_{};
  std::string password_{};
  std::string username_{};
  std::string url_{};
  int status_ = -1;
  bool update_started_ = false;
  static const uint16_t HTTP_RECV_BUFFER = 256;  // the firmware GET chunk size
};

}  // namespace http_request
}  // namespace esphome