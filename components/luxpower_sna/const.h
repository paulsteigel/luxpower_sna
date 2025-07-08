#pragma once

namespace esphome {
namespace luxpower_sna {

// From original Python library (LXPPacket.py)
const uint8_t TCP_FUNCTION_HEARTBEAT = 193;
const uint8_t TCP_FUNCTION_TRANSLATED_DATA = 194;

const uint8_t DEVICE_FUNCTION_READ_HOLD = 3;
const uint8_t DEVICE_FUNCTION_READ_INPUT = 4;
const uint8_t DEVICE_FUNCTION_WRITE_SINGLE = 6;

const uint8_t ACTION_READ = 1;
const uint8_t ACTION_WRITE = 0; // The protocol uses this for read requests

}  // namespace luxpower_sna
}  // namespace esphome
