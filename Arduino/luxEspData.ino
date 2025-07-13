#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include "LuxParser.h"

// ───────────────────────────────── Wi‑Fi / OTA ─────────────────────────────────
const char* ssid      = "WIFI";
const char* password = "pass";

// OTA (blank password)
void setupOTA() {
  ArduinoOTA.setHostname("luxpower-esp");
  //ArduinoOTA.setPassword("");
  ArduinoOTA.begin();
}

// ─────────────────────────── Inverter TCP settings ────────────────────────────
const char* inverterHost = "192.168.100.10";
const uint16_t inverterPort = 8000;

// Default serials (will be overwritten by EEPROM config)
char dongleSerial[11]     = "xxxxxxxxxx";
char inverterSerial[11] = "xxxxxxxxxx";

// ─────────────────────────── EEPROM Configuration ─────────────────────────────
struct Config {
  char magic[4] = "CFG";  // Configuration marker
  uint8_t version = 1;
  char dongle[11] = "xxxxxxxxxx";
  char inverter[11] = "xxxxxxxxxx";
};

Config config;
const int EEPROM_SIZE = sizeof(Config);

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  
  if (strncmp(config.magic, "CFG", 3) != 0) {
    // Invalid config - reset to default
    strncpy(config.magic, "CFG", 4);
    config.version = 1;
    strncpy(config.dongle, "BA32500699", 11);
    strncpy(config.inverter, "3253631886", 11);
    EEPROM.put(0, config);
    EEPROM.commit();
  }
  
  strncpy(dongleSerial, config.dongle, 11);
  strncpy(inverterSerial, config.inverter, 11);
}

void saveConfig() {
  strncpy(config.dongle, dongleSerial, 11);
  strncpy(config.inverter, inverterSerial, 11);
  EEPROM.put(0, config);
  EEPROM.commit();
}

// ─────────────────────────── Globals & helpers ────────────────────────────────
ESP8266WebServer server(80);
WiFiClient        client;

// Global sections to preserve data between polls
Section1 gSection1 = { .loaded = false };
Section2 gSection2 = { .loaded = false };
Section3 gSection3 = { .loaded = false };
Section4 gSection4 = { .loaded = false };
Section5 gSection5 = { .loaded = false };
SystemData gSystem;  // Added global system data

const uint8_t BANKS[] = {0, 40, 80, 120, 160};
uint8_t bankIndex = 0;
uint32_t lastPollMs    = 0;
const    uint32_t POLL = 20000;       // 20 s
const    uint32_t TCP_TIMEOUT = 15000;// 15 s

// CRC‑16 (Modbus) helper
uint16_t calcCRC(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
  }
  return crc;
}

// HTML helpers
String row(const String& k, const String& v, bool isCalculated = false) {
  String cls = isCalculated ? " class='calculated'" : "";
  return "<tr" + cls + "><td><b>" + k + "</b></td><td>" + v + "</td></tr>\n";
}
String fmtF(float v, uint8_t d=1) { return isnan(v) ? "-" : String(v, d); }

// System state helper
String getSystemState() {
  // Renamed from gSystem.last_heartbeat to gSystem.lux_data_last_received_time
  if (gSystem.lux_data_last_received_time == 0) return "OFFLINE (no heartbeat)";
  // Renamed from gSystem.last_heartbeat to gSystem.lux_data_last_received_time
  return (millis() - gSystem.lux_data_last_received_time) < 15000 ? "ONLINE" : "OFFLINE";
}

// ───────────────────────────── Web UI (all data) ──────────────────────────────
String webPage() {
  String h = "<html><head><meta http-equiv='refresh' content='20'>";
  h += "<style>body{font-family:sans-serif}table{border-collapse:collapse}"
       "td,th{border:1px solid #888;padding:5px 10px}"
       ".note{color:#666;font-size:0.9em}"
       "a{color:#0066cc;text-decoration:none}"
       ".calculated {color: #006400; font-weight: bold}"
       ".system-row {background-color: #f0f0f0; font-weight: bold}</style></head><body>";
  h += "<h2>LuxPower Real‑time Data</h2>";
  h += "<p class='note'>Dongle: " + String(dongleSerial) + " | Inverter: " + String(inverterSerial) + 
       " | <a href='/config'>Configure</a></p>";
  h += "<table>";
  h += "<tr><th>Field</th><th>Value</th></tr>";

  // System Information
  h += "<tr class='system-row'><td colspan='2'>System Information</td></tr>";
  h += row("Lux Inverter Model", gSystem.lux_inverter_model);
  h += row("Lux Firmware Version", gSystem.lux_firmware_version);
  h += row("Lux Status (Text)", gSystem.lux_status_text);
  h += row("Lux Battery Status", gSystem.lux_battery_status_text);
  h += row("Online State", getSystemState());

  // Section 1 - Bank 0
  if (gSection1.loaded) {
    h += "<tr class='system-row'><td colspan='2'>Section 1 - Real-time Data</td></tr>";
    h += row("Lux Status", String(gSection1.lux_status));
    h += row("Lux Solar Voltage Array 1 (Live)", fmtF(gSection1.lux_current_solar_voltage_1) + " V");
    h += row("Lux Solar Voltage Array 2 (Live)", fmtF(gSection1.lux_current_solar_voltage_2) + " V");
    h += row("Lux Solar Voltage Array 3 (Live)", fmtF(gSection1.lux_current_solar_voltage_3) + " V");
    h += row("Lux Battery Voltage (Live)", fmtF(gSection1.lux_battery_voltage) + " V");
    h += row("Lux Battery %", String(gSection1.lux_battery_percent) + " %");
    h += row("SOH", String(gSection1.soh) + " %");
    h += row("Lux Internal Fault", String(gSection1.lux_internal_fault));

    h += row("Lux Solar Output Array 1 (Live)", String(gSection1.lux_current_solar_output_1) + " W");
    h += row("Lux Solar Output Array 2 (Live)", String(gSection1.lux_current_solar_output_2) + " W");
    h += row("Lux Solar Output Array 3 (Live)", String(gSection1.lux_current_solar_output_3) + " W");
    h += row("Lux Battery Charge (Live)", String(gSection1.lux_battery_charge) + " W");
    h += row("Lux Battery Discharge (Live)", String(gSection1.lux_battery_discharge) + " W");
    h += row("Lux Power From Inverter (Live)", String(gSection1.lux_power_from_inverter_live) + " W");
    h += row("Lux Power To Inverter (Live)", String(gSection1.lux_power_to_inverter_live) + " W");
    h += row("Lux Power To Grid (Live)", String(gSection1.lux_power_to_grid_live) + " W");
    h += row("Lux Power From Grid (Live)", String(gSection1.lux_power_from_grid_live) + " W");

    h += row("Grid Volt R", fmtF(gSection1.grid_voltage_r) + " V");
    h += row("Grid Volt S", fmtF(gSection1.grid_voltage_s) + " V");
    h += row("Grid Volt T", fmtF(gSection1.grid_voltage_t) + " V");
    h += row("Lux Grid Voltage (Live)", fmtF(gSection1.lux_grid_voltage_live) + " V");
    h += row("Lux Grid Frequency (Live)", fmtF(gSection1.lux_grid_frequency_live, 2) + " Hz");

    h += row("Lux CT Clamp (Live)", fmtF(gSection1.lux_power_current_clamp, 2) + " A");
    h += row("Grid Power Factor", fmtF(gSection1.grid_power_factor, 3));

    h += row("EPS Volt R", fmtF(gSection1.eps_voltage_r) + " V");
    h += row("EPS Volt S", fmtF(gSection1.eps_voltage_s) + " V");
    h += row("EPS Volt T", fmtF(gSection1.eps_voltage_t) + " V");
    h += row("EPS Frequency", fmtF(gSection1.eps_frequency, 2) + " Hz");
    h += row("Lux Power To EPS (Live)", String(gSection1.lux_power_to_eps) + " W");
    h += row("EPS Apparent Power", String(gSection1.apparent_eps_power) + " VA");

    h += row("Lux Solar Output Array 1 (Daily)", fmtF(gSection1.lux_daily_solar_array_1) + " kWh");
    h += row("Lux Solar Output Array 2 (Daily)", fmtF(gSection1.lux_daily_solar_array_2) + " kWh");
    h += row("Lux Solar Output Array 3 (Daily)", fmtF(gSection1.lux_daily_solar_array_3) + " kWh");
    h += row("Lux Power from Inverter to Home (Daily)", fmtF(gSection1.lux_power_from_inverter_daily) + " kWh");
    h += row("Lux Power to Inverter (Daily)", fmtF(gSection1.lux_power_to_inverter_daily) + " kWh");
    h += row("Lux Battery Charge (Daily)", fmtF(gSection1.lux_daily_battery_charge) + " kWh");
    h += row("Lux Battery Discharge (Daily)", fmtF(gSection1.lux_daily_battery_discharge) + " kWh");
    h += row("Lux Power To EPS (Daily)", fmtF(gSection1.lux_power_to_eps_daily) + " kWh");
    h += row("Lux Power To Grid (Daily)", fmtF(gSection1.lux_power_to_grid_daily) + " kWh");
    h += row("Lux Power From Grid (Daily)", fmtF(gSection1.lux_power_from_grid_daily) + " kWh");

    h += row("Bus1 Voltage", fmtF(gSection1.bus1_voltage) + " V");
    h += row("Bus2 Voltage", fmtF(gSection1.bus2_voltage) + " V");
    
    // CALCULATED FIELDS - SECTION 1
    h += row("Lux Solar Output (Live)", String(gSection1.lux_current_solar_output) + " W", true);
    h += row("Lux Solar Output (Daily)", fmtF(gSection1.lux_daily_solar) + " kWh", true);
    h += row("Lux Power From Grid to HOUSE (Live)", String(gSection1.lux_power_to_home) + " W", true);
    h += row("Lux Battery Flow (Live)", String(gSection1.lux_battery_flow) + " W", true);
    h += row("Lux Grid Flow (Live)", String(gSection1.lux_grid_flow) + " W", true);
    h += row("Lux Home Consumption (Live)", String(gSection1.lux_home_consumption_live) + " W", true);
    h += row("Lux Home Consumption (Daily)", fmtF(gSection1.lux_home_consumption) + " kWh", true);
  }

  // Section 2 - Bank 40
  if (gSection2.loaded) {
    h += "<tr class='system-row'><td colspan='2'>Section 2 - Total Energy Data</td></tr>";
    h += row("Lux Solar Output Array 1 (Total)", fmtF(gSection2.lux_total_solar_array_1) + " kWh");
    h += row("Lux Solar Output Array 2 (Total)", fmtF(gSection2.lux_total_solar_array_2) + " kWh");
    h += row("Lux Solar Output Array 3 (Total)", fmtF(gSection2.lux_total_solar_array_3) + " kWh");
    h += row("Lux Power from Inverter to Home (Total)", fmtF(gSection2.lux_power_from_inverter_total) + " kWh");
    h += row("Lux Power to Inverter (Total)", fmtF(gSection2.lux_power_to_inverter_total) + " kWh");
    h += row("Lux Battery Charge (Total)", fmtF(gSection2.lux_total_battery_charge) + " kWh");
    h += row("Lux Battery Discharge (Total)", fmtF(gSection2.lux_total_battery_discharge) + " kWh");
    h += row("Lux Power To EPS (Total)", fmtF(gSection2.lux_power_to_eps_total) + " kWh");
    h += row("Lux Power To Grid (Total)", fmtF(gSection2.lux_power_to_grid_total) + " kWh");
    h += row("Lux Power From Grid (Total)", fmtF(gSection2.lux_power_from_grid_total) + " kWh");

    h += row("Lux Fault Code", String(gSection2.lux_fault_code));
    h += row("Lux Warning Code", String(gSection2.lux_warning_code));
    h += row("Lux Internal Temperature (Live)", fmtF(gSection2.lux_internal_temp) + " °C");
    h += row("Lux Radiator 1 Temperature (Live)", fmtF(gSection2.lux_radiator1_temp) + " °C");
    h += row("Lux Radiator 2 Temperature (Live)", fmtF(gSection2.lux_radiator2_temp) + " °C");
    h += row("Lux Battery Temperature (Live)", fmtF(gSection2.lux_battery_temperature_live) + " °C");

    h += row("Lux Uptime (Potential)", String(gSection2.lux_uptime) + " s");
    
    // CALCULATED FIELDS - SECTION 2
    h += row("Lux Solar Output (Total)", fmtF(gSection2.lux_total_solar) + " kWh", true);
    h += row("Lux Home Consumption (Total)", fmtF(gSection2.lux_home_consumption_total) + " kWh", true);
  }

  // Section 3 - Bank 80
  if (gSection3.loaded) {    
    h += "<tr class='system-row'><td colspan='2'>Section 3 - Battery Data</td></tr>";
    h += row("Lux BMS Limit Charge (Live)", fmtF(gSection3.lux_bms_limit_charge) + " A");
    h += row("Lux BMS Limit Discharge (Live)", fmtF(gSection3.lux_bms_limit_discharge) + " A");
    h += row("Charge Voltage Ref", fmtF(gSection3.charge_voltage_ref) + " V");
    h += row("Discharge Cutoff Voltage", fmtF(gSection3.discharge_cutoff_voltage) + " V");
    h += row("Battery Status INV", String(gSection3.battery_status_inv));
    h += row("Lux Battery Count", String(gSection3.lux_battery_count));
    h += row("Lux Battery Capacity Ah", String(gSection3.lux_battery_capacity_ah) + " Ah");

    h += row("Lux Battery Current", fmtF(gSection3.lux_battery_current, 2) + " A");
    h += row("Lux Battery Max Cell Voltage (Live)", fmtF(gSection3.max_cell_volt, 3) + " V");
    h += row("Lux Battery Min Cell Voltage (Live)", fmtF(gSection3.min_cell_volt, 3) + " V");
    h += row("Lux Battery Max Cell Temperature (Live)", fmtF(gSection3.max_cell_temp, 1) + " °C");
    h += row("Lux Battery Min Cell Temperature (Live)", fmtF(gSection3.min_cell_temp, 1) + " °C");

    h += row("Lux Battery Cycle Count", String(gSection3.lux_battery_cycle_count));
    h += row("Lux Home Consumption 2 (Live)", String(gSection3.lux_home_consumption_2_live) + " W");
    
    // CALCULATED FIELDS - SECTION 3
    h += row("Lux Home Consumption 2 (Live)", String(gSection3.lux_home_consumption_2_live_alias) + " W", true);
  }

  // Section 4 - Bank 120
  if (gSection4.loaded) {
    h += "<tr class='system-row'><td colspan='2'>Section 4 - Generator & EPS Data</td></tr>";
    h += row("Lux Generator Voltage (Live)", fmtF(gSection4.lux_current_generator_voltage) + " V");
    h += row("Lux Generator Frequency (Live)", fmtF(gSection4.lux_current_generator_frequency, 2) + " Hz");
    h += row("Lux Generator Power (Live)", String(gSection4.lux_current_generator_power) + " W");
    h += row("Lux Generator Power (Daily)", fmtF(gSection4.lux_current_generator_power_daily) + " kWh");
    h += row("Lux Generator Power (Total)", fmtF(gSection4.lux_current_generator_power_all) + " kWh");
    
    h += row("Lux EPS L1 Voltage (Live)", fmtF(gSection4.lux_current_eps_L1_voltage) + " V");
    h += row("Lux EPS L2 Voltage (Live)", fmtF(gSection4.lux_current_eps_L2_voltage) + " V");
    h += row("Lux EPS L1 Watts (Live)", String(gSection4.lux_current_eps_L1_watt) + " W");
    h += row("Lux EPS L2 Watts (Live)", String(gSection4.lux_current_eps_L2_watt) + " W");
  }

  // Section 5 - Bank 160
  if (gSection5.loaded) {
    h += "<tr class='system-row'><td colspan='2'>Section 5 - Load Data</td></tr>";
    h += row("On-grid Load Power", String(gSection5.p_load_ongrid) + " W");
    h += row("Load Energy Today", fmtF(gSection5.e_load_day) + " kWh");
    h += row("Total Load Energy", fmtF(gSection5.e_load_all_l) + " kWh");
  }

  h += "</table><p style='color:gray;'>Auto‑refresh 20 s · OTA enabled</p></body></html>";
  return h;
}

// ──────────────────────── Configuration Web Page ──────────────────────────────
String configPage() {
  String page = "<html><head><style>"
                "body{font-family:sans-serif;max-width:600px;margin:0 auto}"
                "form{background:#f8f8f8;padding:20px;border-radius:8px}"
                "label{display:block;margin:10px 0 5px}"
                "input{width:100%;padding:8px;box-sizing:border-box}"
                "button{background:#4CAF50;color:white;border:none;padding:10px;border-radius:4px;cursor:pointer}"
                ".msg{color:#d00;margin-top:10px}</style></head><body>"
                "<h2>Configuration</h2>"
                "<form action='/updateconfig' method='post'>";
  
  page += "<label>Dongle Serial:</label>";
  page += "<input type='text' name='dongle' value='" + String(dongleSerial) + "' maxlength='10' required>";
  
  page += "<label>Inverter Serial:</label>";
  page += "<input type='text' name='inverter' value='" + String(inverterSerial) + "' maxlength='10' required>";
  
  page += "<button type='submit'>Save Configuration</button>";
  
  if (server.hasArg("msg")) {
    page += "<p class='msg'>" + server.arg("msg") + "</p>";
  }
  
  page += "</form><p><a href='/'>← Back to Dashboard</a></p></body></html>";
  return page;
}

// ─────────────────────── TCP request / response helpers ───────────────────────
bool sendRequest(uint8_t bank) {
  if (!client.connect(inverterHost, inverterPort)) {
    Serial.println("❌ TCP connect failed");
    return false;
  }

  uint8_t pkt[38] = {
    0xA1, 0x1A,        // Prefix
    0x02, 0x00,        // Protocol version 2
    0x20, 0x00,        // Frame length (32)
    0x01,              // Address
    0xC2,              // Function (TRANSLATED_DATA)
    // Dongle serial (10 bytes)
    dongleSerial[0], dongleSerial[1], dongleSerial[2], dongleSerial[3], dongleSerial[4],
    dongleSerial[5], dongleSerial[6], dongleSerial[7], dongleSerial[8], dongleSerial[9],
    0x12, 0x00,        // Data length (18)
    // Data frame starts here
    0x00,              // Address action
    0x04,              // Device function (READ_INPUT)
    // Inverter serial (10 bytes)
    inverterSerial[0], inverterSerial[1], inverterSerial[2], inverterSerial[3], inverterSerial[4],
    inverterSerial[5], inverterSerial[6], inverterSerial[7], inverterSerial[8], inverterSerial[9],
    // Register and value
    static_cast<uint8_t>(bank), 0x00, // Register (low, high)
    0x28, 0x00         // Value (40 registers)
  };

  // Calculate CRC for data frame portion only
  uint16_t crc = calcCRC(pkt + 20, 16);
  pkt[36] = crc & 0xFF;
  pkt[37] = crc >> 8;

  Serial.printf("→ Req bank %d: ", bank);
  for (int i = 0; i < 38; i++) Serial.printf("%02X ", pkt[i]);
  Serial.println();
  
  client.write(pkt, 38);
  return true;
}

bool receiveResponse(uint8_t* buf, uint16_t& len) {
  uint32_t t0 = millis();
  len = 0;
  while (millis()-t0 < TCP_TIMEOUT) {
    while (client.available() && len<512) buf[len++] = client.read();
    if (len) break;
    delay(10);
  }
  client.stop();
  if (!len) {
    Serial.println("❌ Timeout / no data");
    return false;
  }
  
  Serial.printf("← %d bytes: ", len);
  uint16_t printLen = (len < 50) ? len : 50;
  for (uint16_t i = 0; i < printLen; i++) Serial.printf("%02X ", buf[i]);
  if (len > 50) Serial.print("...");
  Serial.println();
  return true;
}

// ────────────────────────────────── Setup ─────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // Load configuration from EEPROM
  loadConfig();
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected : " + WiFi.localIP().toString());

  server.on("/", []() { server.send(200, "text/html; charset=utf-8", webPage()); });
  server.on("/config", []() { server.send(200, "text/html; charset=utf-8", configPage()); });
  
  server.on("/updateconfig", []() {
    if (server.method() != HTTP_POST) {
      server.send(405, "text/plain", "Method Not Allowed");
      return;
    }
    
    String newDongle = server.arg("dongle");
    String newInverter = server.arg("inverter");
    
    String msg;
    if (newDongle.length() == 10 && newInverter.length() == 10) {
      newDongle.toCharArray(dongleSerial, 11);
      newInverter.toCharArray(inverterSerial, 11);
      saveConfig();
      msg = "Configuration saved successfully!";
    } else {
      msg = "Error: Serial numbers must be exactly 10 characters!";
    }
    
    server.sendHeader("Location", "/config?msg=" + msg);
    server.send(302, "text/plain", "Redirecting...");
  });
  
  server.begin();
  setupOTA();
}

// ────────────────────────────────── Loop ──────────────────────────────────────
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  if (millis()-lastPollMs < POLL) return;
  lastPollMs = millis();

  uint8_t bank = BANKS[bankIndex];
  bankIndex = (bankIndex + 1) % 5;  // Now cycles through 5 banks

  uint8_t rx[512];  
  uint16_t rxLen;
  LuxData lux;  // Temporary for decoding
  
  bool success = false;
  for (int attempt = 0; attempt < 2 && !success; attempt++) {
    if (sendRequest(bank) && receiveResponse(rx, rxLen)) {
      if (lux.decode(rx, rxLen)) {
        success = true;
        Serial.print("✅ ");
        if (lux.header.function == 0xC1) {
          Serial.print("Heartbeat");
          // Renamed from gSystem.last_heartbeat to gSystem.lux_data_last_received_time
          gSystem.lux_data_last_received_time = millis();  // Update heartbeat
        } else {
          Serial.print("Decoded");
          // Update global system data
          gSystem = lux.system;
          
          // Update sections
          if (lux.section1.loaded) gSection1 = lux.section1;
          if (lux.section2.loaded) gSection2 = lux.section2;
          if (lux.section3.loaded) gSection3 = lux.section3;
          if (lux.section4.loaded) gSection4 = lux.section4;
          if (lux.section5.loaded) gSection5 = lux.section5;
        }
        Serial.printf(" bank %d\n", bank);
      }
    }
  }
  
  if (!success) {
    Serial.printf("❌ Failed bank %d after 2 attempts\n", bank);
  }
}   
