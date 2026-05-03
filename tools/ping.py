#!/usr/bin/env python3
"""
Ping test - gửi OTA_START frame và chờ ACK
Chạy: python ping.py --port COM14 --addr 1
"""
import serial
import time
import argparse
import struct

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--baud', type=int, default=9600)
parser.add_argument('--addr', type=int, default=1)
args = parser.parse_args()

SOF           = 0xAA
CMD_OTA_START = 0xF0
CMD_ACK       = 0x06
CMD_NACK      = 0x07

def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def build_frame(addr, cmd, data=b''):
    payload = bytes([addr, cmd, len(data)]) + data
    return bytes([SOF]) + payload + bytes([crc8(payload)])

print(f"Mở {args.port} @ {args.baud} baud...")
ser = serial.Serial(args.port, args.baud, timeout=2.0)
time.sleep(0.5)
ser.reset_input_buffer()

# Gửi OTA_START với 10 pages, CRC=0x1234 (giả để test)
data = struct.pack('>HH', 10, 0x1234)
frame = build_frame(args.addr, CMD_OTA_START, data)

print(f"\nGửi OTA_START đến node 0x{args.addr:02X}...")
print(f"Frame: {' '.join(f'{b:02X}' for b in frame)}")

for attempt in range(5):
    ser.write(frame)
    print(f"Attempt {attempt+1}/5 — chờ response...")

    buf = bytearray()
    deadline = time.time() + 3.0
    got_response = False

    while time.time() < deadline:
        if ser.in_waiting:
            b = ser.read(ser.in_waiting)
            buf.extend(b)
            print(f"  RX raw: {' '.join(f'{x:02X}' for x in b)}")

            # Parse frame
            for i, byte in enumerate(buf):
                if byte == SOF and len(buf) - i >= 5:
                    cmd  = buf[i+2]
                    dlen = buf[i+3]
                    if len(buf) - i >= 5 + dlen:
                        if cmd == CMD_ACK:
                            print(f"\n✅ ACK nhận được! Node đang hoạt động bình thường.")
                            got_response = True
                        elif cmd == CMD_NACK:
                            err = buf[i+4] if dlen > 0 else 0
                            print(f"\n⚠️  NACK nhận được (err=0x{err:02X}) — node phản hồi nhưng từ chối")
                            got_response = True
                        break
        time.sleep(0.01)

    if got_response:
        break

    print(f"  Timeout — thử lại...\n")
    time.sleep(0.5)

if not got_response:
    print("\n❌ Không nhận được phản hồi từ node.")
    print("Kiểm tra:")
    print("  1. LED board có đang nháy chậm 500ms/500ms không?")
    print("  2. Dây A-A, B-B đúng chưa?")
    print("  3. GND chung chưa?")

ser.close()