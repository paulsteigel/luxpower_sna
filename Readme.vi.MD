# LuxPower SNA – ESPHome Component

[![ESPHome](https://img.shields.io/badge/ESPHome-2024.x%2B-blue)](https://esphome.io)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](LICENSE)

Component ESPHome để giám sát và điều khiển **biến tần LuxPower SNA/SNA-G2** trực tiếp từ ESP32, không cần Home Assistant server hay MQTT broker.

Thiết kế đặc biệt cho người dùng ở các khu vực (như Việt Nam) mà ISP phân phát IP WAN nội bộ, khiến port forwarding không khả thi với integration Python gốc.

---

## ✨ Tính năng

- **Đọc đầy đủ** tất cả sensor bank 0–4 (live, daily, total, BMS, generator)
- **Ghi dữ liệu** — switch, number, button để điều khiển biến tần
- **Cấu hình runtime** — đặt host IP và serial number từ HA UI mà không cần flash lại
- **Tương thích IDF & Arduino** — dùng lwip socket trực tiếp, không phụ thuộc WiFiClient
- **Kết nối TCP bền vững** — giống Python integration gốc, tự xử lý heartbeat
- **State machine polling** — non-blocking, không có `delay()`, an toàn trên ESP32-S2 single-core
- **Tự động kết nối lại** — reconnect khi mất kết nối, retry sau 10 giây

---

## 📋 Yêu cầu

- ESP32 (bất kỳ biến thể: S2, S3, classic, C3…)
- ESPHome **2024.5+**
- Biến tần LuxPower SNA / SNA-G2 có WiFi dongle trên cùng mạng LAN

---

## 🚀 Bắt đầu nhanh

### 1. Thêm external component

```yaml
external_components:
  - source: github://paulsteigel/luxpower_sna@main
    refresh: 0s
```

### 2. Khai báo hub

```yaml
luxpower_sna:
  id: lux_hub
  host: "192.168.1.100"        # IP của dongle (hoặc để trống để cấu hình runtime)
  port: 8000
  dongle_serial: "BA12345678"  # Đúng 10 ký tự
  inverter_serial: "1234567890" # Đúng 10 ký tự
  update_interval: 20s         # Chu kỳ đọc READ_INPUT
  hold_update_interval: 60s    # Chu kỳ refresh READ_HOLD (switch/number)
```

### 3. Thêm sensor, switch, number

Xem file [`luxpower_package.yaml`](luxpower_package.yaml) để có ví dụ đầy đủ.

---

## ⚙️ Cấu hình runtime (không cần flash lại)

Để trống `host`/`dongle_serial`/`inverter_serial` trong YAML và cấu hình từ HA UI hoặc web_server. Giá trị được lưu vào flash ESP32 (NVS) và tồn tại qua các lần khởi động lại.

---

## 📡 Các platform hỗ trợ

| Platform | Mô tả |
|----------|-------|
| `sensor` | Tất cả sensor số (điện áp, công suất, năng lượng, nhiệt độ…) |
| `text_sensor` | Trạng thái biến tần, trạng thái pin (khai báo trong block `sensor:`) |
| `switch` | Bitmask switch trên hold register (reg 21, 110, 179) |
| `number` | Number entity trên hold register (tỷ lệ sạc, giới hạn SOC, điện áp…) |
| `button` | Khởi động lại biến tần, reset toàn bộ cài đặt |

---

## 🗂️ Các bank sensor

| Bank | Register | Nội dung |
|------|----------|----------|
| 0 | 0–39 | Live: điện áp PV, pin, lưới, EPS, năng lượng hàng ngày |
| 1 | 40–79 | Tổng: tích lũy năng lượng, fault/warning code, nhiệt độ, uptime |
| 2 | 80–119 | BMS: điện áp/nhiệt độ cell, trạng thái pin, dòng điện, dung lượng |
| 3 | 120–159 | Máy phát điện, EPS L1/L2 |
| 4 | 160–199 | Tải on-grid, năng lượng tải hàng ngày/tổng |

---

## 🔧 Xử lý sự cố

| Triệu chứng | Nguyên nhân | Cách sửa |
|-------------|-------------|----------|
| Không có dữ liệu, connection refused | Sai IP hoặc port | Kiểm tra IP dongle trong DHCP router |
| Lỗi CRC mismatch | Sai serial number | Kiểm tra lại 10 ký tự dongle + inverter serial |
| Giá trị BMS dòng cao gấp 10 lần | Model dùng scale /100 | Đổi `/10.0f` → `/100.0f` trong file cpp |
| Entity không tìm thấy sau OTA | Slug thay đổi | Kiểm tra trường `name:` — slug = chữ thường + gạch dưới |

---

## 🙏 Tác giả & Credits

- [guybw/LuxPython_DEV](https://github.com/guybw/LuxPython_DEV) — Python HA integration gốc, tài liệu protocol và register map
- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) — Tham khảo kiến trúc ESPHome component

---

## 📄 Giấy phép

MIT — Sử dụng theo rủi ro của bạn. Không liên kết với LuxPower.
