"""Automatic RS485 baud sweep: finds the fastest speed the cable runs cleanly.  pip install pyserial paho-mqtt

    python rs485_baud_sweep.py [COM13] [--baud auto] [--dwell 120] [--bauds 19200,38400,57600,115200]

This script is BOTH the slave (answers the ESP's "?" on the serial port, like rs485_test_listener.py)
AND the tester: it sets the ESP's "RS485 Baud" select through MQTT (command topic found from the
retained Home Assistant discovery message, broker/user/password read from qtrdltruongson.yaml),
watches the ESP's log topic, and for each speed counts clean vs corrupted lines on BOTH sides:
  PC side  : "?" received vs junk lines   (ESP -> PC direction)
  ESP side : T=.. lines parsed vs "bad line" warnings (PC -> ESP direction), from the ESP's MQTT log
--baud = the speed the link runs at now (default auto-detects it; the ESP boots at 9600). Stop other programs
that hold the COM port first. Ends by putting the link back to 9600.
"""
import argparse
import json
import queue
import random
import re
import sys
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import serial

NODE = "qtrdltruongson"
YAML = Path(__file__).with_name(NODE + ".yaml")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
SAFE = 9600  # speed the ESP boots at / we leave it at

ap = argparse.ArgumentParser()
ap.add_argument("port", nargs="?", default="COM13")
ap.add_argument("--baud", default="auto", help="current link speed, or auto to detect it")
ap.add_argument("--dwell", type=int, default=120, help="seconds measured per speed")
ap.add_argument("--bauds", default="19200,38400,57600,115200")
ap.add_argument("--topic", help="select command topic, if HA discovery isn't available")
args = ap.parse_args()


def detect():
    """Listen (without answering) at each speed for one ESP poll; the clean "?" shows the speed."""
    for b in (9600, 19200, 38400, 57600, 115200):
        print(f"detecting link speed: trying {b} ...", flush=True)
        with serial.Serial(args.port, b, timeout=0.2) as s:
            s.reset_input_buffer()
            end, data = time.time() + 6.5, b""
            while time.time() < end:
                data += s.read(64)
        if re.fullmatch(rb"(\?\r?\n)+", data):
            return b
    sys.exit("Could not detect the ESP's speed (no clean '?' poll). Is the ESP up and wired?")


args.baud = detect() if args.baud == "auto" else int(args.baud)


def mqtt_creds():
    text = YAML.read_text(encoding="utf-8")
    block = re.search(r"^mqtt:\s*\n((?:[ \t]+.*\n|[ \t]*\n)+)", text, re.M).group(1)
    get = lambda k: re.search(rf"^\s+{k}:\s*(\S+)", block, re.M).group(1).strip("'\"")
    return get("broker"), get("username"), get("password")


class Stats:
    def __init__(self):
        self.pc_ok = self.pc_junk = self.esp_ok = self.esp_bad = 0


cur = Stats()
events = queue.Queue()  # ("ok", baud) / ("fail", text) from the ESP log
lock = threading.Lock()
stop = threading.Event()
cmd_topic = args.topic
link = args.baud


def say(who, msg):
    with lock:
        print(f"{time.strftime('%H:%M:%S')} {who:<3} {msg}", flush=True)


# ---------------------------------------------------------------- MQTT side (ESP log + commands)
def esp_line(ln):
    if "rs485" not in ln or "rx bytes total" in ln:
        return
    say("ESP", ln.split("]: ", 1)[-1])
    if re.search(r"rs485:\d+\]: T=", ln):
        cur.esp_ok += 1
    elif "bad line" in ln:
        cur.esp_bad += 1
    elif m := re.search(r"baud change to (\d+) OK", ln):
        events.put(("ok", int(m[1])))
    elif "no ack from slave" in ln or "no valid reading" in ln:
        events.put(("fail", ln))


def on_connect(c, *_):
    c.subscribe(f"homeassistant/select/{NODE}/+/config")
    c.subscribe(f"{NODE}/debug")


def on_message(c, _u, m):
    global cmd_topic
    if m.topic.endswith("/config"):
        cfg = json.loads(m.payload)  # ESPHome uses the short key "cmd_t"
        cmd_topic = cmd_topic or cfg.get("command_topic") or cfg.get("cmd_t")
        return
    for ln in ANSI.sub("", m.payload.decode(errors="replace")).splitlines():
        esp_line(ln)


broker, user, pwd = mqtt_creds()
try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, f"rs485_sweep_{random.randrange(10**6)}")
except AttributeError:  # paho 1.x
    client = mqtt.Client(f"rs485_sweep_{random.randrange(10**6)}")
client.username_pw_set(user, pwd)
client.on_connect, client.on_message = on_connect, on_message
client.connect(broker, 1883, 30)
client.loop_start()
for _ in range(100):
    if cmd_topic:
        break
    time.sleep(0.1)
if not cmd_topic:
    sys.exit("No select command topic found (HA MQTT discovery off?). Pass --topic <.../command>.")
say("SYS", f"MQTT ok, command topic {cmd_topic}")


# ---------------------------------------------------------------- serial side: the slave
def slave(ser):
    temp, hum, rain = 28.0, 70.0, 0
    prev, probation, last_poll = ser.baudrate, None, time.time()

    def set_baud(b, why):
        ser.baudrate = b
        ser.reset_input_buffer()
        say("PC", f"-- baud {b} ({why})")

    while not stop.is_set():
        now = time.time()
        if probation and now > probation:
            probation = None
            set_baud(prev, "no '?' after switch, reverting")
        elif not probation and ser.baudrate != SAFE and now - last_poll > 30:
            set_baud(SAFE, "idle 30s, back to default")
        line = ser.readline().strip()
        if not line:
            continue
        if line == b"?":
            cur.pc_ok += 1
            probation, last_poll = None, time.time()
            temp = min(45, max(10, temp + random.uniform(-0.5, 0.5)))
            hum = min(100, max(20, hum + random.uniform(-2, 2)))
            rain ^= random.random() < 0.1
            reply = f"T={temp:.1f},H={hum:.1f},R={rain}\n"
            ser.write(reply.encode())
            say("PC", f"? -> {reply.strip()}")
        elif line.startswith(b"B=") and line[2:].isdigit():
            nb = int(line[2:])
            ack = f"OK B={nb}\n".encode()
            ser.write(ack)
            ser.flush()
            time.sleep(len(ack) * 10 / ser.baudrate + 0.15)  # let the ack leave at the old speed
            prev, probation = ser.baudrate, time.time() + 3
            set_baud(nb, "requested")
        else:
            cur.pc_junk += 1
            say("PC", f"JUNK {line[:40]!r}")


def switch(target):
    """Ask the ESP to go to `target`; True if the link is there afterwards."""
    global link
    if target == link:
        return True
    while not events.empty():
        events.get()
    say("SYS", f"asking ESP to switch {link} -> {target}")
    client.publish(cmd_topic, str(target))
    try:
        kind, val = events.get(timeout=25)
    except queue.Empty:
        kind = "fail"
    if kind == "ok" and val == target:
        link = target
        return True
    say("SYS", f"switch to {target} FAILED (ESP stayed/reverted)")
    return False


def measure(target):
    global cur
    cur = Stats()
    ok = switch(target)
    if ok:
        say("SYS", f"link at {target}; 6s warm-up, then measuring for {args.dwell}s ...")
        time.sleep(6)  # skip the first exchange: opening the port / switching can glitch it
        cur = Stats()
        time.sleep(args.dwell)
    return ok, cur


results = []
with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
    say("SYS", f"slave on {args.port} @ {args.baud}; sweep {SAFE},{args.bauds}, {args.dwell}s each")
    threading.Thread(target=slave, args=(ser,), daemon=True).start()
    try:
        for b in [SAFE] + [int(x) for x in args.bauds.split(",") if x]:  # --bauds "" = soak at 9600 only
            ok, st = measure(b)
            expect = args.dwell // 5  # the ESP polls every 5s
            good = ok and st.esp_bad == 0 and st.pc_junk == 0 and st.esp_ok >= 0.8 * expect and st.pc_ok >= 0.8 * expect
            results.append((b, ok, st, good))
            say("SYS", f"==> {b}: {'PASS' if good else 'FAIL'}  esp_ok={st.esp_ok} esp_bad={st.esp_bad} "
                       f"pc_ok={st.pc_ok} pc_junk={st.pc_junk} (expected ~{expect})")
    except KeyboardInterrupt:
        say("SYS", "interrupted")
    finally:
        if link != SAFE:
            say("SYS", "returning link to 9600")
            switch(SAFE)
        time.sleep(2)
        stop.set()

print("\n baud    link   ESP ok/bad   PC ok/junk   result")
for b, ok, st, good in results:
    print(f"{b:>6}  {'up  ' if ok else 'DOWN'}   {st.esp_ok:>4}/{st.esp_bad:<4}   {st.pc_ok:>4}/{st.pc_junk:<4}     {'PASS' if good else 'FAIL'}")
passed = [b for b, _, _, g in results if g]
print(f"\nFastest clean speed: {max(passed)} baud" if passed else "\nNo speed passed")
client.loop_stop()
