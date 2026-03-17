# LuxPower SNA – ESPHome Component

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.x%2B-blue)](https://esphome.io)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](LICENSE)
[![GitHub](https://img.shields.io/badge/GitHub-paulsteigel%2Fluxpower_sna-lightgrey)](https://github.com/paulsteigel/luxpower_sna)

An ESPHome external component for monitoring and controlling **LuxPower SNA/SNA-G2 inverters** directly from an ESP32, without requiring a Home Assistant server or MQTT broker.

Designed for locations (such as parts of Vietnam) where ISPs assign non-public WAN IPs, making port forwarding impossible for the official Python integration.

---

## ✨ Features

- **Full read access** to all inverter sensor banks (0–4: live, daily, total, BMS, generator)
- **Write support** — switches, numbers, and buttons to control the inverter
- **Zero-config first-run** — press *Scan Dongle IP* button; the ESP scans the local /24 subnet, finds the dongle, and fills in the host automatically
- **Runtime configuration** — set host IP and serial numbers from HA UI without reflashing; values survive reboot
- **IDF-compatible** — uses lwip sockets directly; no WiFiClient dependency; works on ESP32-S2 single-core
- **Persistent TCP connection** — mirrors the official Python integration behaviour; handles heartbeats automatically
- **State machine polling** — non-blocking, no `delay()`, safe on single-core ESP32-S2
- **Auto-reconnect** — reconnects on connection drop with 10 s retry

Here are some snapshots of the control views:

![Set up connection info](docs/images/z7584216112635_9206f501577d9b2cd16d628bec415ae2.jpg)

Lovelace view — copy and replace the entity name in `lovelace.yaml`:

![Lovelace view](docs/images/z7625537015460_91f3f0b315d76ef5f0e5f3b220b229f9.jpg)

---

## 📋 Requirements

- ESP32 (any variant: S2, S3, classic, C3…)
- ESPHome **2026.2+**
- LuxPower SNA / SNA-G2 inverter with WiFi dongle on local network

---

## 🚀 Quick Start

### 1. Add external component

```yaml
external_components:
  - source: github://paulsteigel/luxpower_sna@main
    refresh: 0s
```

### 2. Declare the hub

```yaml
luxpower_sna:
  id: lux_hub
  host: "192.168.1.100"         # Dongle IP — or leave blank and use Scan Dongle button
  port: 8000
  dongle_serial: "BA12345678"   # Exactly 10 characters
  inverter_serial: "1234567890" # Exactly 10 characters
  update_interval: 20s          # READ_INPUT polling interval
  hold_update_interval: 60s     # READ_HOLD refresh interval (switches/numbers)
```

### 3. Add sensors, switches, numbers

See [`luxpower_package.yaml`](luxpower_package.yaml) for a full working example.

---

## ⚙️ Runtime Configuration (no reflash needed)

Leave `host`, `dongle_serial`, and `inverter_serial` blank in YAML and set them from the HA UI or web server. Values are stored in ESP32 flash and survive reboots.

```yaml
luxpower_sna:
  id: lux_hub
  host_text_id: lux_config_host   # wire scan result back to the text entity
  update_interval: 20s
  hold_update_interval: 60s

text:
  - platform: template
    id: lux_config_host
    name: "Inverter Host"
    entity_category: config
    mode: text
    optimistic: true
    restore_value: true
    initial_value: ""
    on_value:
      then:
        - lambda: |-
            id(lux_hub).set_host(x);
            if (id(lux_hub).is_config_ready()) id(lux_hub).reconnect();

  - platform: template
    id: lux_config_dongle
    name: "Dongle Serial"
    entity_category: config
    mode: text
    optimistic: true
    restore_value: true
    initial_value: ""
    on_value:
      then:
        - lambda: |-
            if (x.size() != 10) return;
            id(lux_hub).set_dongle_serial(x);
            if (id(lux_hub).is_config_ready()) id(lux_hub).reconnect();

  - platform: template
    id: lux_config_inverter
    name: "Inverter Serial"
    entity_category: config
    mode: text
    optimistic: true
    restore_value: true
    initial_value: ""
    on_value:
      then:
        - lambda: |-
            if (x.size() != 10) return;
            id(lux_hub).set_inverter_serial(x);
            if (id(lux_hub).is_config_ready()) id(lux_hub).reconnect();
```

> **`host_text_id`** is the key that links the hub to your `lux_config_host` text entity. When the Scan Dongle button finds the dongle, it writes the discovered IP back into this entity automatically — including saving it to flash.

---

## 🔍 Scan Dongle IP (Auto-Discovery)

If you do not know the dongle's IP address, use the **Scan Dongle IP** button. The ESP32 will scan all 254 addresses on its own /24 subnet, connect-test each one on the configured port (default 8000), and set the host automatically when found.

### Prerequisites before scanning

Both `dongle_serial` and `inverter_serial` must be filled in first (they are needed to verify the connection after discovery). The host field can be empty.

### How it works

1. Press **Scan Dongle IP** in HA or the web interface.
2. The component launches a background FreeRTOS task (does not block the main loop).
3. Addresses are tested in batches of 8 parallel TCP connects (200 ms timeout per batch).
4. On a typical /24 network the scan completes in **3–7 seconds**.
5. When the dongle is found:
   - `scan_status_text` sensor shows `Found: 192.168.x.x`
   - The host is written back into the `lux_config_host` text entity (flash-persisted)
   - The component reconnects automatically
6. If nothing is found: `scan_status_text` shows `Not found`.

### YAML snippet

```yaml
luxpower_sna:
  id: lux_hub
  host_text_id: lux_config_host   # required for auto-fill after scan
  update_interval: 20s
  hold_update_interval: 60s

button:
  - platform: luxpower_sna
    luxpower_sna_id: lux_hub
    scan_luxpower_dongle:
      name: "Scan Dongle IP"
      icon: "mdi:magnify"
      entity_category: config

sensor:
  - platform: luxpower_sna
    luxpower_sna_id: lux_hub
    scan_status_text:
      name: "Scan Status"
      icon: "mdi:magnify"
      entity_category: diagnostic
```

### Error states reported by `scan_status_text`

| Message | Cause |
|---------|-------|
| `Scanning...` | Scan in progress |
| `Found: 192.168.x.x` | Dongle located, host applied |
| `Not found` | No device answered on the configured port |
| `Error: set dongle serial first` | `dongle_serial` not yet configured |
| `Error: set inverter serial first` | `inverter_serial` not yet configured |
| `Error: task create failed` | Insufficient FreeRTOS heap (very rare) |
| `Error: scan timeout` | Background task died unexpectedly (watchdog fired after 30 s) |

---

## 📡 Supported Platforms

| Platform | Description |
|----------|-------------|
| `sensor` | All numeric sensors (voltage, power, energy, temperature…) |
| `text_sensor` | Status text, battery status, scan status (defined inside `sensor:` block) |
| `switch` | Bitmask-based hold register switches |
| `number` | Hold register number entities (charge rate, SOC limit, voltage…) |
| `button` | Inverter restart, reset all settings, scan dongle IP |

---

## 🔀 Switch Reference

| Key | Description | Register | Bitmask |
|-----|-------------|----------|---------|
| `normal_or_standby` | Normal / Standby mode | 21 | 0x0200 |
| `ac_charge_enable` | AC Charge Enable | 21 | 0x0080 |
| `feed_in_grid` | Feed In Grid | 21 | 0x8000 |
| `charge_priority` | Charge Priority | 21 | 0x0800 |
| `power_backup_enable` | Power Backup Enable | 21 | 0x0001 |
| `seamless_eps_switching` | Seamless EPS Switching | 21 | 0x0100 |
| `forced_discharge_enable` | Force Discharge Enable | 21 | 0x0400 |
| `charge_last` | Charge Last | 110 | 0x0010 |
| `enable_peak_shaving` | Grid Peak Shaving | 179 | 0x0080 |

---

## 🔢 Number Reference

| Key | Register | Unit | Divisor | Notes |
|-----|----------|------|---------|-------|
| `charge_power_percent` | 64 | % | 1 | |
| `discharge_power_percent` | 65 | % | 1 | |
| `ac_charge_power_percent` | 66 | % | 1 | |
| `ac_charge_soc_limit` | 67 | % | 1 | |
| `discharge_cutoff_soc` | 105 | % | 1 | |
| `forced_discharge_power_percent` | 76 | % | 1 | |
| `priority_charge_rate` | 77 | % | 1 | |
| `priority_charge_soc` | 78 | % | 1 | |
| `charge_voltage` | 99 | V | 10 | |
| `discharge_cutoff_voltage` | 100 | V | 10 | |
| `ct_clamp_offset` | 119 | W | 10 | signed |
| `grid_peak_shaving_power` | 206 | kW | 10 | |

---

## 🗂️ Sensor Banks

| Bank | Registers | Contents |
|------|-----------|----------|
| 0 | 0–39 | Live: PV voltage/power, battery, grid, EPS, daily energy |
| 1 | 40–79 | Totals: lifetime energy counters, fault/warning codes, temperature, uptime |
| 2 | 80–119 | BMS: cell voltage/temp, battery status, current, capacity |
| 3 | 120–159 | Generator input, EPS L1/L2 |
| 4 | 160–199 | On-grid load power, daily/total load energy |

---

## 🔧 Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No data, connection refused | Wrong IP or port | Use Scan Dongle IP button, or check dongle IP in router DHCP table |
| Scan returns "Not found" | Dongle on different subnet or port | Confirm dongle IP manually; check `lux_config_port` value |
| Scan returns error about serials | Dongle/inverter serial not filled in | Set both serials in HA UI before scanning |
| CRC mismatch errors | Serial numbers wrong | Verify 10-char dongle + inverter serial (case-sensitive) |
| `lux_config_host` not updated after scan | Missing `host_text_id` in hub config | Add `host_text_id: lux_config_host` to the `luxpower_sna:` block |
| Values 10× too high (BMS current) | Model uses /100 scale | Change `/10.0f` → `/100.0f` in `luxpower_sna.cpp` bank 2 section |
| Entity not found after OTA | Slug changed | Check `name:` field — slug = lowercase + underscores |
| Config lost after reboot | `restore_value` not set | Add `restore_value: true` to all config text/number entities |

---

## 🔁 First-Run Workflow

For a brand-new installation where you do not know the dongle IP:

1. Flash the firmware (host and serials can be left blank)
2. In HA, set **Dongle Serial** and **Inverter Serial** (both exactly 10 characters)
3. Press **Scan Dongle IP** — wait ~5 seconds
4. Check **Scan Status** — if `Found: 192.168.x.x`, the host is set and the component connects automatically
5. Sensors and controls become available immediately

---

## 📦 File Structure

```
luxpower_sna/
  __init__.py       # Hub component registration + host_text_id wiring
  luxpower_sna.h    # C++ class declarations (scan state, deferred apply)
  luxpower_sna.cpp  # C++ implementation (FreeRTOS scan task, state machine)
  sensor.py         # Sensor + text_sensor platform (incl. scan_status_text)
  switch.py         # Switch platform
  number.py         # Number platform
  button.py         # Button platform (incl. scan_luxpower_dongle)
  time.py           # Time-slot helper (AC charge, force discharge windows)
```

---

## 🙏 Credits

- [guybw/LuxPython_DEV](https://github.com/guybw/LuxPython_DEV) — original Python HA integration, protocol documentation and register map
- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) — ESPHome component architecture reference

---

## 📄 License

MIT — use at your own risk. Not affiliated with LuxPower.