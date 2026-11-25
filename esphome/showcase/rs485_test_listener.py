"""PC as RS485 slave: answer every "?" poll from the ESP with a fake weather line.  pip install pyserial

    python rs485_test_listener.py [COM13] [baud] [seconds]

Baud negotiation (driven by the ESP's "RS485 Baud" select):
  ESP "B=19200"  ->  slave "OK B=19200" (at the OLD speed), then switches.
  Safety nets so the link can't get lost: after a switch the slave must hear a valid "?"
  within 3s, else it goes back to the previous baud; and at any non-default baud, 30s
  without a "?" sends it back to the default.
"""
import random
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM13"
default = int(sys.argv[2]) if len(sys.argv) > 2 else 9600
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9

temp, hum, rain = 28.0, 70.0, 0


def set_baud(ser, b, why):
    ser.baudrate = b
    ser.reset_input_buffer()
    print(f"-- baud {b} ({why})")


with serial.Serial(port, default, timeout=0.2) as ser:
    print(f"slave on {port} @ {default}, waiting for '?'")
    end = time.time() + secs
    prev, probation, last_poll = default, None, time.time()
    while time.time() < end:
        now = time.time()
        if probation and now > probation:
            probation = None
            set_baud(ser, prev, "no '?' after switch, reverting")
        elif not probation and ser.baudrate != default and now - last_poll > 30:
            set_baud(ser, default, "idle 30s, back to default")
        if not (line := ser.readline()):
            continue
        line = line.strip()
        print("RX", line)
        if line.startswith(b"B=") and line[2:].isdigit():
            nb = int(line[2:])
            ack = f"OK B={nb}\n".encode()
            ser.write(ack)
            ser.flush()
            time.sleep(len(ack) * 10 / ser.baudrate + 0.15)  # let the ack leave at the old speed
            prev, probation = ser.baudrate, time.time() + 3
            set_baud(ser, nb, "requested")
        elif line == b"?":
            probation, last_poll = None, time.time()
            temp = min(45, max(10, temp + random.uniform(-0.5, 0.5)))
            hum = min(100, max(20, hum + random.uniform(-2, 2)))
            if random.random() < 0.1:
                rain ^= 1
            reply = f"T={temp:.1f},H={hum:.1f},R={rain}\n"
            ser.write(reply.encode())
            print("TX", reply.strip())
