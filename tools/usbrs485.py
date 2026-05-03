#!/usr/bin/env python3
"""
RS485 OTA Firmware Updater
Gửi firmware .hex qua RS485 đến Bootstrap node

Cách dùng:
  python ota_flash.py --port COM5 --hex Node.ino.hex --addr 1

Cài thư viện:
  pip install pyserial intelhex
"""

import argparse
import struct
import time
import sys
import serial

# ── Protocol constants ────────────────────────────────────────
SOF           = 0xAA
CMD_OTA_START = 0xF0
CMD_OTA_DATA  = 0xF1
CMD_OTA_END   = 0xF2
CMD_ACK       = 0x06
CMD_NACK      = 0x07
PAGE_SIZE     = 64
MAX_RETRIES   = 3
TIMEOUT_S     = 5.0     # Tăng từ 3s lên 5s

# ── CRC8 ─────────────────────────────────────────────────────
def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

# ── CRC16 CCITT ───────────────────────────────────────────────
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

# ── Build frame ───────────────────────────────────────────────
def build_frame(addr: int, cmd: int, data: bytes) -> bytes:
    payload = bytes([addr, cmd, len(data)]) + data
    return bytes([SOF]) + payload + bytes([crc8(payload)])

# ── Parse response frame ──────────────────────────────────────
def read_response(ser: serial.Serial, timeout: float = TIMEOUT_S):
    """Đọc và parse 1 frame response từ node"""
    buf = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            b = ser.read(1)[0]
            if not buf and b != SOF:
                continue
            buf.append(b)
            if len(buf) < 5:
                continue
            data_len = buf[3]
            frame_len = 5 + data_len
            if len(buf) < frame_len:
                continue
            # Verify CRC
            calc = crc8(bytes(buf[1:frame_len - 1]))
            if calc != buf[frame_len - 1]:
                buf.clear()
                continue
            return {
                'addr': buf[1],
                'cmd':  buf[2],
                'data': bytes(buf[4:4 + data_len])
            }
        time.sleep(0.001)
    return None

# ── Load hex file và pad thành pages ─────────────────────────
def load_hex(hex_path: str):
    """Load .hex thủ công, không dùng intelhex library"""
    flash = bytearray(b'\xFF' * 0x7000)   # 28KB app section, fill 0xFF

    with open(hex_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            data    = bytes.fromhex(line[1:])
            count   = data[0]
            addr    = (data[1] << 8) | data[2]
            rectype = data[3]
            if rectype == 0x00:     # Data record
                for i in range(count):
                    if addr + i < 0x7000:
                        flash[addr + i] = data[4 + i]
            elif rectype == 0x01:   # EOF
                break

    # Cắt bỏ 0xFF trailing ở cuối, pad về bội số PAGE_SIZE
    last = 0
    for i in range(len(flash) - 1, -1, -1):
        if flash[i] != 0xFF:
            last = i + 1
            break
    padded = ((last + PAGE_SIZE - 1) // PAGE_SIZE) * PAGE_SIZE
    flash   = flash[:padded]

    pages = [bytes(flash[i:i + PAGE_SIZE])
             for i in range(0, padded, PAGE_SIZE)]

    print(f"[HEX] Loaded: {hex_path}")
    print(f"[HEX] App size: {last} bytes → {len(pages)} pages × {PAGE_SIZE} bytes")
    return pages

# ── OTA Flash ─────────────────────────────────────────────────
def ota_flash(port: str, hex_path: str, node_addr: int, baud: int = 9600):
    print(f"\n{'='*50}")
    print(f"  RS485 OTA Firmware Updater")
    print(f"  Port: {port} | Baud: {baud} | Node: 0x{node_addr:02X}")
    print(f"{'='*50}\n")

    # Load firmware
    try:
        pages = load_hex(hex_path)
    except Exception as e:
        print(f"[ERROR] Không đọc được file hex: {e}")
        print("  Cần cài: pip install intelhex")
        sys.exit(1)

    total_crc = crc16(b''.join(pages))
    print(f"[HEX] Total CRC16: 0x{total_crc:04X}\n")

    # Mở serial port
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
        time.sleep(0.5)  # Chờ port ổn định
        ser.reset_input_buffer()
    except Exception as e:
        print(f"[ERROR] Không mở được {port}: {e}")
        sys.exit(1)

    # ── OTA_START ────────────────────────────────────────────
    print("[1/3] Gửi OTA_START...")
    start_data = struct.pack('>HH', len(pages), total_crc)
    frame = build_frame(node_addr, CMD_OTA_START, start_data)

    for attempt in range(MAX_RETRIES):
        ser.write(frame)
        resp = read_response(ser)
        if resp and resp['cmd'] == CMD_ACK:
            print("      → ACK ✓")
            time.sleep(0.5)     # Chờ node sẵn sàng nhận data
            break
        print(f"      → Thử lại ({attempt + 1}/{MAX_RETRIES})...")
    else:
        print("[ERROR] Node không phản hồi OTA_START")
        ser.close()
        sys.exit(1)

    # ── OTA_DATA (từng page) ─────────────────────────────────
    print(f"[2/3] Gửi {len(pages)} pages firmware...")
    start_time = time.time()

    for seq, page in enumerate(pages):
        # Hiển thị progress
        pct = int((seq + 1) / len(pages) * 100)
        bar = '█' * (pct // 5) + '░' * (20 - pct // 5)
        print(f"\r      [{bar}] {pct:3d}% ({seq+1}/{len(pages)})", end='', flush=True)

        chunk_data = struct.pack('>H', seq) + page
        frame = build_frame(node_addr, CMD_OTA_DATA, chunk_data)

        for attempt in range(MAX_RETRIES):
            ser.write(frame)
            resp = read_response(ser, timeout=3.0)
            if resp and resp['cmd'] == CMD_ACK:
                break
            elif resp and resp['cmd'] == CMD_NACK:
                err = resp['data'][0] if resp['data'] else 0
                if attempt == MAX_RETRIES - 1:
                    print(f"\n[ERROR] NACK page {seq}, error=0x{err:02X}")
                    ser.close()
                    sys.exit(1)
            # Retry
            time.sleep(0.1)
        else:
            print(f"\n[ERROR] Timeout page {seq}")
            ser.close()
            sys.exit(1)

        # Delay giữa các page để node kịp ghi flash (~4ms/page)
        time.sleep(0.05)    # 50ms

    elapsed = time.time() - start_time
    print(f"\n      Xong {len(pages)} pages trong {elapsed:.1f}s ✓")

    # ── OTA_END ───────────────────────────────────────────────
    print("[3/3] Gửi OTA_END...")
    frame = build_frame(node_addr, CMD_OTA_END, b'')

    for attempt in range(MAX_RETRIES):
        ser.write(frame)
        resp = read_response(ser, timeout=5.0)
        if resp and resp['cmd'] == CMD_ACK:
            print("      → ACK ✓")
            break
        elif resp and resp['cmd'] == CMD_NACK:
            err = resp['data'][0] if resp['data'] else 0
            print(f"[ERROR] OTA_END NACK, error=0x{err:02X}")
            ser.close()
            sys.exit(1)
        print(f"      → Thử lại ({attempt + 1}/{MAX_RETRIES})...")
    else:
        print("[ERROR] Timeout OTA_END")
        ser.close()
        sys.exit(1)

    ser.close()

    print(f"\n{'='*50}")
    print(f"  ✅ OTA THÀNH CÔNG!")
    print(f"  Node 0x{node_addr:02X} đang reset và chạy firmware mới")
    print(f"{'='*50}\n")

# ── Main ──────────────────────────────────────────────────────
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='RS485 OTA Firmware Updater')
    parser.add_argument('--port', required=True,
                        help='COM port (vd: COM5 hoặc /dev/ttyUSB0)')
    parser.add_argument('--hex',  required=True,
                        help='Đường dẫn file .hex (Node.ino.hex)')
    parser.add_argument('--addr', type=lambda x: int(x, 0), default=1,
                        help='Địa chỉ node (decimal hoặc 0x...), mặc định=1')
    parser.add_argument('--bcast', action='store_true',
                        help='Broadcast tất cả node (addr=0x00)')
    parser.add_argument('--baud', type=int, default=9600,
                        help='Baudrate (mặc định 9600)')
    args = parser.parse_args()

    node_addr = 0x00 if args.bcast else args.addr
    ota_flash(args.port, args.hex, node_addr, args.baud)