#!/usr/bin/env python3
"""
RS485 Monitor - Kiểm tra xem có nhận được data từ node không
Chạy: python monitor.py --port COM14
"""
import serial
import time
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--baud', type=int, default=9600)
args = parser.parse_args()

print(f"Mở {args.port} @ {args.baud} baud...")
print("Đang lắng nghe — nhấn Ctrl+C để thoát\n")

try:
    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    time.sleep(0.5)
    ser.reset_input_buffer()

    last_rx = time.time()
    byte_count = 0

    while True:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            byte_count += len(data)
            hex_str = ' '.join(f'{b:02X}' for b in data)
            asc_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data)
            print(f"[{time.time():.2f}] RX {len(data):3d} bytes: {hex_str}  |  {asc_str}")
            last_rx = time.time()
        else:
            if time.time() - last_rx > 5.0:
                print(f"[{time.time():.2f}] ... không nhận được gì trong 5 giây (total: {byte_count} bytes)")
                last_rx = time.time()
        time.sleep(0.01)

except serial.SerialException as e:
    print(f"Lỗi: {e}")
except KeyboardInterrupt:
    print(f"\nThoát. Tổng nhận: {byte_count} bytes")
    ser.close()