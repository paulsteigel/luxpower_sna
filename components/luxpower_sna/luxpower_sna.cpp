// components/luxpower_sna/luxpower_sna.cpp

// ... (keep all the other functions like setup, update, request_data_, etc. the same) ...

void LuxpowerSNAComponent::handle_packet_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;

  // Your inverter sends a 117-byte packet. Let's adjust the check.
  if (len < 117) {
    ESP_LOGW(TAG, "Received packet too short: %d bytes. Expected 117 bytes.", len);
    return;
  }
  
  ESP_LOGD(TAG, "Full packet received:\n%s", format_hex_pretty(raw, len).c_str());

  // --- NEW PARSING LOGIC BASED ON YOUR 117-BYTE PACKET ---
  // Note: Byte order is Big-Endian (first byte is MSB).
  
  // --- High-Confidence Mappings ---
  float battery_voltage = (float)(raw[34] << 8 | raw[35]) / 100.0f; // e.g., 0x1450 -> 5200 -> 52.0V
  this->publish_state_("battery_voltage", battery_voltage);

  float soc = (float)raw[46]; // e.g., 0x62 -> 98%
  this->publish_state_("soc", soc);

  float grid_voltage = (float)(raw[38] << 8 | raw[39]) / 10.0f; // e.g., 0x08BB -> 2235 -> 223.5V
  this->publish_state_("grid_voltage", grid_voltage);
  
  float grid_frequency = (float)(raw[70] << 8 | raw[71]) / 100.0f; // e.g., 0x1371 -> 4977 -> 49.77Hz
  this->publish_state_("grid_frequency", grid_frequency);

  // --- Power Values (likely correct, need to verify charge/discharge direction) ---
  int battery_power_raw = (int16_t)(raw[36] << 8 | raw[37]); // Signed value
  this->publish_state_("battery_power", (float)battery_power_raw);
  this->publish_state_("charge_power", (float)(battery_power_raw > 0 ? battery_power_raw : 0));
  this->publish_state_("discharge_power", (float)(battery_power_raw < 0 ? -battery_power_raw : 0));

  float pv1_power = (float)(raw[50] << 8 | raw[51]); // e.g., 0x05CC -> 1484W
  this->publish_state_("pv1_power", pv1_power);
  float pv2_power = (float)(raw[54] << 8 | raw[55]); // e.g., 0x06C0 -> 1728W
  this->publish_state_("pv2_power", pv2_power);
  this->publish_state_("pv_power", pv1_power + pv2_power);

  int grid_power_raw = (int16_t)(raw[58] << 8 | raw[59]); // Signed value
  this->publish_state_("grid_power", (float)grid_power_raw);
  // Assuming positive is import (buying), negative is export (selling)
  // This might need to be swapped depending on your inverter's logic
  this->publish_state_("grid_import_today", (float)(grid_power_raw > 0 ? grid_power_raw : 0));
  this->publish_state_("grid_export_today", (float)(grid_power_raw < 0 ? -grid_power_raw : 0));
  
  float load_power = (float)(raw[62] << 8 | raw[63]); // e.g., 0x015C -> 348W
  this->publish_state_("load_power", load_power);

  // Inverter Power might be EPS power or total AC output. Let's assume it's EPS for now.
  float inverter_power = (float)(raw[60] << 8 | raw[61]); // e.g., 0x0000 -> 0W
  this->publish_state_("inverter_power", inverter_power);
  this->publish_state_("eps_power", inverter_power);

  // --- Other PV and EPS values ---
  float pv1_voltage = (float)(raw[52] << 8 | raw[53]) / 10.0f; // e.g., 0x0700 -> 1792 -> 179.2V
  this->publish_state_("pv1_voltage", pv1_voltage);
  float pv2_voltage = (float)(raw[56] << 8 | raw[57]) / 10.0f; // e.g., 0x00BA -> 186 -> 18.6V (This seems low, is PV2 connected/in sun?)
  this->publish_state_("pv2_voltage", pv2_voltage);

  // These might not be available in this packet, using grid values as placeholders
  this->publish_state_("eps_voltage", grid_voltage);
  this->publish_state_("eps_frequency", grid_frequency);
  this->publish_state_("power_factor", (float)(raw[47]) / 100.0f); // e.g., 0x61 -> 97 -> 0.97

  // --- Temperatures (Best Guesses - Please Verify) ---
  float radiator_temp = (float)(raw[64] << 8 | raw[65]) / 10.0f; // e.g., 0x9A13 -> ? Needs verification
  this->publish_state_("radiator_temp", radiator_temp);
  float inverter_temp = (float)(raw[66] << 8 | raw[67]) / 10.0f; // e.g., 0x1371 -> ? Needs verification
  this->publish_state_("inverter_temp", inverter_temp);
  this->publish_state_("battery_temp", 0.0f); // Cannot locate in this packet yet

  // --- Daily Energy Totals (Best Guesses - Please Verify) ---
  this->publish_state_("pv_today", (float)(raw[88] << 8 | raw[89]) / 10.0f);
  this->publish_state_("load_today", (float)(raw[90] << 8 | raw[91]) / 10.0f);
  this->publish_state_("charge_today", (float)(raw[94] << 8 | raw[95]) / 10.0f);
  this->publish_state_("grid_import_today", (float)(raw[100] << 8 | raw[101]) / 10.0f);
  this->publish_state_("grid_export_today", (float)(raw[98] << 8 | raw[99]) / 10.0f);
  this->publish_state_("discharge_today", (float)(0)); // Can't locate yet
  this->publish_state_("eps_today", (float)(0)); // Can't locate yet
  this->publish_state_("inverter_today", (float)(0)); // Can't locate yet

  // --- Status Code ---
  int status_code = raw[45]; // e.g., 0x02
  this->publish_state_("status_code", (float)status_code);
  std::string status_text = "Unknown";
  switch(status_code) {
      case 0: status_text = "Standby"; break;
      case 1: status_text = "Self Test"; break;
      case 2: status_text = "Normal"; break;
      case 3: status_text = "Alarm"; break;
      case 4: status_text = "Fault"; break;
      default: status_text = "Checking"; break;
  }
  this->publish_state_("status_text", status_text);
}
