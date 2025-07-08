#pragma once // This should always be the very first line

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
// #include "esphome/components/binary_sensor/binary_sensor.h" // Only include if actually used in this header

#include <vector>
#include <map>
#include <string> // For std::string
#include <functional> // For std::bind if used directly in header

#include <ESPAsyncTCP.h> // Correct header for AsyncClient on ESP8266
#include "sensor/luxpower_sna_sensor.h"
#include "luxpower_sna_constants.h" // <--- ADDED: Include this if constants like LUX_REG_TYPE_FIRMWARE or LUXPOWER_END_BYTE_LENGTH are used in this header


namespace esphome {
namespace luxpower_sna {

// Forward declaration for the LuxpowerSnaSensor class definition
// This is typically done if only pointers/references to the class are used
// If full class members are used, the header must be included, which we've done above.
// You might still keep this if there are circular dependencies, but usually the include is enough.
// class LuxpowerSnaSensor; // Unnecessary if luxpower_sna_sensor.h is included above

enum LuxpowerRegType : uint8_t; // Forward declare enum if it's defined in constants.h

class LuxPowerInverterComponent : public Component {
public:
    // Constructor declaration (assuming you have one defined in .cpp)
    LuxPowerInverterComponent();

    // Standard ESPHome component lifecycle methods
    void setup() override;
    void loop() override;
    void dump_config() override; // <--- ADDED: Declaration for dump_config()

    // Methods related to TCP connection and data handling (based on previous errors)
    bool is_connected();
    bool connect_to_inverter();
    void disconnect_from_inverter();
    bool send_data(const std::vector<unsigned char>& data);

    // Callbacks for AsyncClient events (must match definitions in .cpp)
    // Note: If these are static functions, they do not have 'this' pointer implicitly.
    // If they are member functions, they need to be associated with 'this' like in std::bind.
    // Assuming they are member functions as per previous errors referring to 'this->client_'.
    void on_connect_cb(void *arg, AsyncClient *client);
    void on_disconnect_cb(void *arg, AsyncClient *client);
    void on_data_cb(void *arg, AsyncClient *client, void *data, size_t len);
    void on_error_cb(void *arg, AsyncClient *client, int error);

    // Method to add sensors to this component (based on previous errors)
    void add_luxpower_sensor(LuxpowerSnaSensor* obj, const std::string& name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank);

    // Data parsing method
    bool parse_luxpower_response_packet(const std::vector<unsigned char>& raw_data, std::vector<unsigned char>& parsed_data);

protected:
    // TCP Client
    AsyncClient *client_;

    // Connection state
    bool client_connected_ = false;
    uint32_t last_connection_attempt_time_ = 0;
    uint32_t reconnect_interval_ms_ = 5000; // Example reconnect interval

    // Inverter host and port
    std::string inverter_host_;
    uint16_t inverter_port_;

    // Data buffers and state
    std::vector<unsigned char> receive_buffer_;
    uint32_t last_request_time_ = 0;
    bool request_pending_ = false;
    bool waiting_for_response_ = false;

    // Storage for sensor values and sensor objects
    std::map<uint16_t, uint16_t> current_raw_registers_; // Map of register address to raw value
    std::vector<LuxpowerSnaSensor*> luxpower_sensors_; // List of sensors managed by this component

    // Helper method for getting sensor values (if defined in .cpp)
    float get_sensor_value_(uint16_t raw_value, LuxpowerRegType reg_type);
};

} // namespace luxpower_sna
} // namespace esphome