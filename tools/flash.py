#!/usr/bin/env python3
"""
Direct UART Flash - Flash hex qua bootloader STK500
Cach dung: python flash.py --port COM15 --hex Bootstrap.ino.hex
"""
import serial
import time
import argparse
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--hex',  required=True)
parser.add_argument('--baud', type=int, default=57600)
args = parser.parse_args()

PAGE_SIZE = 128

def load_hex(path):
    flash = bytearray(b'\xFF' * 32768)
    last  = 0
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'): continue
            data    = bytes.fromhex(line[1:])
            count   = data[0]
            addr    = (data[1] << 8) | data[2]
            rectype = data[3]
            if rectype == 0x00:
                for i in range(count):
                    if addr + i < 32768:
                        flash[addr + i] = data[4 + i]
                        if addr + i + 1 > last:
                            last = addr + i + 1
            elif rectype == 0x01:
                break
    padded = ((last + PAGE_SIZE - 1) // PAGE_SIZE) * PAGE_SIZE
    return bytes(flash[:padded]), last

STK_OK     = 0x10
STK_INSYNC = 0x14
STK_EOP    = 0x20
GET_SYNC   = 0x30
ENTER_PROG = 0x50
LEAVE_PROG = 0x51
LOAD_ADDR  = 0x55
PROG_PAGE  = 0x64

def cmd(ser, data):
    ser.write(bytes(data) + bytes([STK_EOP]))

def get_sync(ser):
    cmd(ser, [GET_SYNC])
    r = ser.read(2)
    return len(r) == 2 and r[0] == STK_INSYNC and r[1] == STK_OK

def enter_prog(ser):
    cmd(ser, [ENTER_PROG])
    r = ser.read(2)
    return len(r) >= 1 and r[0] == STK_INSYNC

def leave_prog(ser):
    cmd(ser, [LEAVE_PROG])
    ser.read(2)

def load_addr(ser, byte_addr):
    word = byte_addr >> 1
    cmd(ser, [LOAD_ADDR, word & 0xFF, (word >> 8) & 0xFF])
    r = ser.read(2)
    return len(r) >= 1 and r[0] == STK_INSYNC

def prog_page(ser, data):
    n = len(data)
    payload = [PROG_PAGE, (n >> 8) & 0xFF, n & 0xFF, 0x46] + list(data)
    cmd(ser, payload)
    r = ser.read(2)
    return len(r) >= 1 and r[0] == STK_INSYNC

print(f"\nDirect UART Flash")
print(f"Port: {args.port} | Baud: {args.baud} | Page: {PAGE_SIZE} bytes")

flash_data, app_size = load_hex(args.hex)
num_pages = len(flash_data) // PAGE_SIZE
print(f"App: {app_size} bytes -> {num_pages} pages\n")

ser = serial.Serial(args.port, args.baud, timeout=1.0)
time.sleep(0.1)

print("Dang sync voi bootloader...")
print(">>> Nhan RESET tren board NGAY BEY GIO! <<<\n")

synced = False
for attempt in range(30):
    ser.reset_input_buffer()
    if get_sync(ser):
        print(f"Sync OK (attempt {attempt+1})")
        synced = True
        break
    time.sleep(0.1)

if not synced:
    print("FAIL: Khong sync duoc!")
    ser.close()
    sys.exit(1)

if not enter_prog(ser):
    print("FAIL: enter_progmode")
    ser.close()
    sys.exit(1)

print(f"Flashing {num_pages} pages...")
ok = True
for page in range(num_pages):
    addr = page * PAGE_SIZE
    pct  = int((page + 1) / num_pages * 100)
    bar  = '#' * (pct // 5) + '.' * (20 - pct // 5)
    print(f"\r  [{bar}] {pct:3d}% ({page+1}/{num_pages})", end='', flush=True)
    if not load_addr(ser, addr):
        print(f"\nFAIL: load_addr page {page}")
        ok = False
        break
    page_data = flash_data[addr:addr + PAGE_SIZE]
    if not prog_page(ser, list(page_data)):
        print(f"\nFAIL: prog_page {page}")
        ok = False
        break

if ok:
    print(f"\n\nFlash OK! Board dang reset...")
    leave_prog(ser)
else:
    print("\nFlash that bai!")

ser.close()