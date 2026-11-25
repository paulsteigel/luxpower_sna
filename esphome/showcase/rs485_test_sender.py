"""Fake weather sender for the ESP-07 RS485 test.  pip install pyserial

    python rs485_test_sender.py [COM13] [baud]

Sends "T=28.5,H=70.2,R=0\n" every 2s (random walk, rain toggles now and then)
and prints whatever the ESP answers ("OK" = both RX and TX paths work).
"""
import random
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM13"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 9600  # must match uart: baud_rate in the yaml

temp, hum, rain = 28.0, 70.0, 0
with serial.Serial(port, baud, timeout=0.1) as ser:
    print(f"sending on {port} @ {baud}, Ctrl+C to stop")
    while True:
        temp = min(45, max(10, temp + random.uniform(-0.5, 0.5)))
        hum = min(100, max(20, hum + random.uniform(-2, 2)))
        if random.random() < 0.1:
            rain ^= 1
        line = f"T={temp:.1f},H={hum:.1f},R={rain}\n"
        ser.write(line.encode())
        print("TX", line.strip())
        end = time.time() + 2
        while time.time() < end:
            if reply := ser.readline().strip():
                print("RX", reply.decode(errors="replace"))
