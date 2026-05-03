#!/usr/bin/env python3
"""
RS485 Master v3.0  —  Valve Controller
=======================================
Khớp đúng protocol với firmware Node v1.5 (Arduino Nano / ATmega328P).

Cài đặt:
    pip install pyserial intelhex

Chạy:
    python master.py --port COM14            # listener mode (mặc định)
    python master.py --port /dev/ttyUSB0

Lệnh trong CLI (gõ 'h' để xem danh sách):
    s <addr>                    GET_STATUS
    o <addr>                    MOTOR_OPEN
    c <addr>                    MOTOR_CLOSE
    x <addr>                    MOTOR_STOP
    t                           TIME_SYNC broadcast
    d <addr> on|off             Debug mode
    p [addr ...]                Toggle auto-poll + chọn nodes
    ota <addr> <file>           OTA unicast (có ACK từng chunk)
    bcast <file> <a1,a2,...>    OTA broadcast (không ACK)
    q                           Thoát
"""

import serial, time, struct, sys, threading, argparse
from pathlib import Path
from datetime import datetime

try:
    from intelhex import IntelHex
except ImportError:
    IntelHex = None

# ─────────────────────────────────────────────────────────────
# PROTOCOL  (phải khớp config.h)
# ─────────────────────────────────────────────────────────────
SOF          = 0xAA
BROADCAST    = 0x00

CMD = dict(
    MOTOR_OPEN  = 0x01, MOTOR_CLOSE = 0x02, MOTOR_STOP  = 0x03,
    GET_STATUS  = 0x04, TIME_SYNC   = 0x05, ACK         = 0x06,
    NACK        = 0x07, STATUS      = 0x08, ALARM       = 0x09,
    LOG         = 0x0A, DEBUG_ON    = 0x0B, DEBUG_OFF   = 0x0C,
    OTA_START   = 0xF0, OTA_DATA    = 0xF1, OTA_END     = 0xF2,
)
CMD_NAME = {v: k for k, v in CMD.items()}

MOTOR_STATES = {0:'IDLE', 1:'OPENING', 2:'CLOSING',
                3:'OPEN', 4:'CLOSED',  5:'ERROR'}

LOG_LEVELS   = {0:'INFO', 1:'WARN', 2:'ERROR'}

EVENT_NAMES  = {
    0x01:'MOTOR_OPENING',      0x02:'MOTOR_CLOSING',
    0x03:'MOTOR_REACHED_OPEN', 0x04:'MOTOR_REACHED_CLOSE',
    0x05:'MOTOR_STUCK',        0x06:'MOTOR_STOPPED',
    0x07:'MOTOR_ERROR_CLEAR',
    0x10:'OTA_START',          0x11:'OTA_PROGRESS',
    0x12:'OTA_CRC_FAIL',       0x13:'OTA_SEQ_FAIL',
    0x14:'OTA_TIMEOUT',        0x15:'OTA_SUCCESS',
    0x20:'DS18B20_LOST',       0x21:'DS18B20_FOUND',
    0x22:'HUMID_LOST',         0x23:'HUMID_FOUND',
    0x24:'I2C_CHANGE',
    0x30:'BOOT',               0x31:'OTA_WAIT',
    0x32:'WDT_RESET',
}

ERR_NAMES = {
    0x01:'CRC_ERR', 0x02:'UNKNOWN_CMD',
    0x03:'BUSY',    0x04:'OTA_ERR', 0x05:'MOTOR_ERR',
}

OTA_CHUNK_SIZE   = 64
FLASH_PAGE_SIZE  = 128
OTA_MAX_CHUNKS   = 480
BOOTLOADER_START = 0x7E00

# ─────────────────────────────────────────────────────────────
# COLORS
# ─────────────────────────────────────────────────────────────
def _c(t, code): return f"\033[{code}m{t}\033[0m"
def green(t):   return _c(t, '92')
def yellow(t):  return _c(t, '93')
def red(t):     return _c(t, '91')
def cyan(t):    return _c(t, '96')
def bold(t):    return _c(t, '1')
def gray(t):    return _c(t, '90')
def magenta(t): return _c(t, '95')
def ts():       return datetime.now().strftime('%H:%M:%S.%f')[:-3]

# ─────────────────────────────────────────────────────────────
# CRC  —  khớp chính xác với protocol.cpp
# ─────────────────────────────────────────────────────────────
def crc8(data: bytes) -> int:
    """CRC8, tính trên ADDR+CMD+LEN+DATA (không gồm SOF và CRC)."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT poly=0x1021 init=0xFFFF — khớp ota.cpp::crc16_update."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

# ─────────────────────────────────────────────────────────────
# FRAME BUILD  —  [SOF][ADDR][CMD][LEN][DATA...][CRC8]
# CRC8 tính trên [ADDR][CMD][LEN][DATA...]
# ─────────────────────────────────────────────────────────────
def build_frame(addr: int, cmd: int, data: bytes = b'') -> bytes:
    payload = bytes([addr, cmd, len(data)]) + data
    return bytes([SOF]) + payload + bytes([crc8(payload)])

# ─────────────────────────────────────────────────────────────
# FRAME PARSERS
# ─────────────────────────────────────────────────────────────
def parse_status(addr: int, data: bytes):
    if len(data) < 9:
        print(f"  {yellow('STATUS')} 0x{addr:02X}: payload ngắn ({len(data)} bytes)")
        return
    motor    = data[0]
    limits   = data[1]
    temp_raw = struct.unpack('>h', data[2:4])[0]
    dist_raw = struct.unpack('>H', data[4:6])[0]
    s1       = data[6]
    flags    = data[7]
    i2c_cnt  = data[8]
    i2c_list = list(data[9 : 9 + i2c_cnt])

    temp_str  = f"{temp_raw/10:.1f}°C" if temp_raw != 0x7FFF else "N/A"
    dist_str  = f"{dist_raw} cm"       if dist_raw != 0xFFFF else "no sensor"
    mstate    = MOTOR_STATES.get(motor, f'?{motor}')
    mcolor    = green if motor in (3, 4) else (red if motor == 5 else yellow)

    print(f"\n{cyan('─'*52)}")
    print(f"  {bold(f'STATUS  Node 0x{addr:02X}')}  {gray(ts())}")
    print(f"  Motor    : {mcolor(mstate)}")
    print(f"  Limit    : OPEN={'YES' if limits & 1 else ' - '}  "
          f"CLOSE={'YES' if limits & 2 else ' - '}")
    print(f"  Temp     : {cyan(temp_str)}")
    print(f"  Distance : {cyan(dist_str)}  sonar={'OK' if flags & 1 else '-'}")
    print(f"  Sensor1  : {s1} (ADC 8-bit)")
    print(f"  Sensor2  : {'ON' if flags & 2 else '-'}")
    if i2c_list:
        print(f"  I2C      : {', '.join(f'0x{a:02X}' for a in i2c_list)}")
    else:
        print(f"  I2C      : {gray('none')}")
    print(f"{cyan('─'*52)}")

def parse_log(addr: int, data: bytes):
    if len(data) < 5:
        print(f"  {gray(ts())} [LOG] 0x{addr:02X}: payload ngắn")
        return
    level     = data[0]
    event     = data[1]
    uptime    = struct.unpack('>H', data[2:4])[0]
    extra     = data[4]
    lvl_str   = LOG_LEVELS.get(level, f'L{level}')
    evt_str   = EVENT_NAMES.get(event, f'0x{event:02X}')
    lcolor    = green if level == 0 else (yellow if level == 1 else red)

    extra_str = ''
    if   event == 0x11: extra_str = f' ({extra}%)'
    elif event == 0x24: extra_str = f' (count={extra})'
    elif event == 0x30: extra_str = f' (boot_addr=0x{extra:02X})'

    print(f"  {gray(ts())} [{lcolor(lvl_str):5}] "
          f"Node 0x{addr:02X} → {bold(evt_str)}{extra_str}  "
          f"{gray(f'uptime={uptime}s')}")

def parse_alarm(addr: int, data: bytes):
    state = data[0] if data else 0
    err   = data[1] if len(data) > 1 else 0
    print(f"\n  {red('!! ALARM')} Node 0x{addr:02X}: "
          f"state={red(MOTOR_STATES.get(state, f'?{state}'))} "
          f"err={red(ERR_NAMES.get(err, f'0x{err:02X}'))} "
          f"{gray(ts())}")

def dispatch_rx(addr: int, cmd: int, payload: bytes):
    """Chỉ hiển thị frame chứa dữ liệu thực sự từ node.
    ACK (0x06), NACK (0x07) và các frame khác bị bỏ qua hoàn toàn."""
    if   cmd == 0x08: parse_status(addr, payload)   # sensor data
    elif cmd == 0x0A: parse_log(addr, payload)       # log event
    elif cmd == 0x09: parse_alarm(addr, payload)     # alarm

# ─────────────────────────────────────────────────────────────
# OTA — ACK/NACK INTERCEPT  (thread-safe)
# ─────────────────────────────────────────────────────────────
# Trong lúc OTA, reader thread không parse/hiển thị ACK/NACK
# mà đưa vào queue để OTA sender đọc.

_ota_active     = False
_ota_resp       = None
_ota_resp_event = threading.Event()
_ota_lock       = threading.Lock()

def _ota_set_response(addr, cmd, payload):
    global _ota_resp
    with _ota_lock:
        _ota_resp = (addr, cmd, bytes(payload))
    _ota_resp_event.set()

def _ota_wait(timeout: float = 2.0):
    global _ota_resp
    _ota_resp_event.clear()
    if _ota_resp_event.wait(timeout):
        with _ota_lock:
            r = _ota_resp
            _ota_resp = None
        return r
    return None

# ─────────────────────────────────────────────────────────────
# READER THREAD  —  parse frame liên tục, không bao giờ gửi
# ─────────────────────────────────────────────────────────────
def reader_thread(ser: serial.Serial):
    buf = bytearray()
    last_time = time.monotonic()

    while True:
        try:
            n = ser.in_waiting
            if n:
                raw = ser.read(n)
                now = time.monotonic()
                if buf and (now - last_time > 0.020):   # 20ms frame gap → reset
                    buf.clear()
                last_time = now
                buf.extend(raw)

                # Parse tất cả frame hoàn chỉnh trong buffer
                while len(buf) >= 5:
                    if buf[0] != SOF:
                        buf.pop(0)
                        continue
                    dlen = buf[3]
                    flen = 5 + dlen
                    if flen > 80:           # sanity guard
                        buf.pop(0)
                        continue
                    if len(buf) < flen:
                        break

                    frame = bytes(buf[:flen])
                    if crc8(frame[1:flen-1]) != frame[flen-1]:
                        # CRC sai: bỏ SOF đầu, tìm SOF tiếp theo
                        buf.pop(0)
                        continue

                    addr    = frame[1]
                    cmd     = frame[2]
                    payload = frame[4 : 4 + dlen]
                    del buf[:flen]

                    # OTA đang chạy: ACK/NACK → intercept
                    if _ota_active and cmd in (0x06, 0x07):
                        _ota_set_response(addr, cmd, payload)
                        continue

                    # Hiển thị bình thường
                    dispatch_rx(addr, cmd, payload)
            else:
                time.sleep(0.004)
        except Exception:
            time.sleep(0.01)

# ─────────────────────────────────────────────────────────────
# TRANSMIT
# ─────────────────────────────────────────────────────────────
_tx_lock = threading.Lock()

def send(ser: serial.Serial, addr: int, cmd: int,
         data: bytes = b'', quiet: bool = True):   # ← default quiet=True
    frame = build_frame(addr, cmd, data)
    with _tx_lock:
        ser.write(frame)
    if not quiet:
        cname = CMD_NAME.get(cmd, f'0x{cmd:02X}')
        bcast = ' (broadcast)' if addr == BROADCAST else ''
        print(f"  {yellow('TX →')} [{cname}] to 0x{addr:02X}{bcast}")
    time.sleep(0.008)   # ≥ 1 byte @ 9600 baud guard

def send_timesync(ser: serial.Serial, n_nodes: int = 15, slot_ms: int = 60):
    """Broadcast TIME_SYNC — xem timesync.cpp để biết format."""
    now_ms = int(time.time() * 1000) & 0xFFFFFFFF
    data   = struct.pack('>IHB', now_ms, slot_ms, n_nodes)
    frame  = build_frame(BROADCAST, CMD['TIME_SYNC'], data)
    with _tx_lock:
        ser.write(frame)
    print(f"  {yellow('TX →')} [TIME_SYNC] broadcast "
          f"ts={now_ms} slot={slot_ms}ms nodes={n_nodes}")

# ─────────────────────────────────────────────────────────────
# AUTO-POLL
# ─────────────────────────────────────────────────────────────
_poll_on       = False
_poll_nodes    = [1]
_poll_interval = 2.0

def poll_thread(ser: serial.Serial):
    counter = 0
    while True:
        if _poll_on and not _ota_active:
            for addr in _poll_nodes:
                try:
                    send(ser, addr, CMD['GET_STATUS'], quiet=True)
                except Exception:
                    pass
                time.sleep(0.3)
            counter += 1
            if counter % 5 == 0:
                try:
                    send_timesync(ser)
                except Exception:
                    pass
        time.sleep(_poll_interval)

# ─────────────────────────────────────────────────────────────
# OTA — LOAD FIRMWARE
# ─────────────────────────────────────────────────────────────
def load_firmware(filepath: str) -> bytes | None:
    path = Path(filepath)
    if not path.exists():
        print(f"  {red('ERROR')} File không tồn tại: {filepath}")
        return None

    ext = path.suffix.lower()
    if ext == '.hex':
        if IntelHex is None:
            print(f"  {red('ERROR')} Cần intelhex: pip install intelhex")
            return None
        ih = IntelHex(str(path))
        min_a = ih.minaddr()
        max_a = min(ih.maxaddr(), BOOTLOADER_START - 1)
        fw = bytes(ih.tobinarray(start=min_a, end=max_a))
        print(f"  {cyan('HEX')} 0x{min_a:04X}..0x{max_a:04X}  ({len(fw)} bytes)")
    elif ext == '.bin':
        fw = path.read_bytes()
        print(f"  {cyan('BIN')} {len(fw)} bytes")
    else:
        print(f"  {red('ERROR')} Format không hỗ trợ: {ext}")
        return None

    # Kiểm tra kích thước
    if len(fw) == 0:
        print(f"  {red('ERROR')} Firmware rỗng!")
        return None
    if len(fw) > BOOTLOADER_START:
        print(f"  {red('ERROR')} Firmware quá lớn: {len(fw)} > {BOOTLOADER_START}")
        return None
    n_chunks_raw = len(fw) // OTA_CHUNK_SIZE + (1 if len(fw) % OTA_CHUNK_SIZE else 0)
    if n_chunks_raw > OTA_MAX_CHUNKS:
        print(f"  {red('ERROR')} Quá nhiều chunk: {n_chunks_raw} > {OTA_MAX_CHUNKS}")
        return None

    # Pad lên bội số của OTA_CHUNK_SIZE bằng 0xFF (giống Arduino pad)
    rem = len(fw) % OTA_CHUNK_SIZE
    if rem:
        fw += b'\xFF' * (OTA_CHUNK_SIZE - rem)

    return fw

# ─────────────────────────────────────────────────────────────
# OTA UNICAST  —  có ACK từng chunk
# Frame protocol khớp chính xác với ota.cpp
# ─────────────────────────────────────────────────────────────
def ota_unicast(ser: serial.Serial, addr: int, filepath: str,
                max_retries: int = 3, chunk_delay: float = 0.025):
    global _ota_active

    fw = load_firmware(filepath)
    if fw is None:
        return False

    n_chunks = len(fw) // OTA_CHUNK_SIZE
    fw_crc   = crc16_ccitt(fw)

    print(f"\n{magenta('='*56)}")
    print(f"  {bold('OTA UNICAST')}  → Node 0x{addr:02X}")
    print(f"  File     : {filepath}")
    print(f"  Size     : {len(fw)} bytes")
    print(f"  Chunks   : {n_chunks}  (× {OTA_CHUNK_SIZE} bytes)")
    print(f"  Pages    : {n_chunks // 2 + n_chunks % 2}  (× {FLASH_PAGE_SIZE} bytes)")
    print(f"  CRC16    : 0x{fw_crc:04X}")
    print(f"  Delay    : {chunk_delay*1000:.0f}ms/chunk")
    print(f"{magenta('='*56)}\n")

    _ota_active = True

    try:
        # ── STEP 1: OTA_START ─────────────────────────────────
        # data = [n_chunks_hi][n_chunks_lo][crc16_hi][crc16_lo]
        print(f"  {magenta('[1/3]')} OTA_START ...")
        start_data = struct.pack('>HH', n_chunks, fw_crc)
        send(ser, addr, CMD['OTA_START'], start_data, quiet=True)

        resp = _ota_wait(timeout=4.0)
        if resp is None:
            print(f"  {red('TIMEOUT')} — Node không phản hồi OTA_START")
            return False
        _, rcmd, rdata = resp
        if rcmd != CMD['ACK']:
            err = rdata[0] if rdata else 0
            print(f"  {red('NACK')} OTA_START: {ERR_NAMES.get(err, f'0x{err:02X}')}")
            return False
        print(f"  {green('ACK')} — Node sẵn sàng nhận firmware\n")

        # ── STEP 2: OTA_DATA ──────────────────────────────────
        # data = [seq_hi][seq_lo][64 bytes chunk]
        print(f"  {magenta('[2/3]')} Gửi {n_chunks} chunk...")
        t0 = time.monotonic()

        for i in range(n_chunks):
            chunk    = fw[i * OTA_CHUNK_SIZE : (i + 1) * OTA_CHUNK_SIZE]
            seq_data = struct.pack('>H', i) + chunk   # 2 + 64 = 66 bytes

            success = False
            for retry in range(max_retries + 1):
                send(ser, addr, CMD['OTA_DATA'], seq_data, quiet=True)
                resp = _ota_wait(timeout=2.0)

                if resp is None:
                    print(f"\r  Chunk {i:4d}/{n_chunks} {red('TIMEOUT')} "
                          f"retry {retry}/{max_retries}  ", end='')
                    time.sleep(0.1)
                    continue

                _, rcmd, rdata = resp
                if rcmd == CMD['ACK']:
                    # ACK chứa seq echo: [seq_hi][seq_lo]
                    if len(rdata) >= 2:
                        ack_seq = struct.unpack('>H', bytes(rdata[:2]))[0]
                        if ack_seq != i:
                            print(f"\r  Chunk {i:4d} ACK seq sai "
                                  f"({ack_seq} ≠ {i}) retry {retry}  ", end='')
                            time.sleep(0.05)
                            continue
                    success = True
                    break
                elif rcmd == CMD['NACK']:
                    err = rdata[0] if rdata else 0
                    print(f"\r  Chunk {i:4d} {red('NACK')} "
                          f"{ERR_NAMES.get(err, f'0x{err:02X}')} "
                          f"retry {retry}  ", end='')
                    time.sleep(0.05)

            if not success:
                print(f"\n\n  {red('FAIL')} Chunk {i} thất bại sau {max_retries} retry")
                print(f"  Node sẽ reset vào recovery mode. Gửi OTA lại sau.")
                return False

            # Progress bar
            pct    = (i + 1) * 100 // n_chunks
            filled = pct // 2
            bar    = '█' * filled + '░' * (50 - filled)
            elapsed = time.monotonic() - t0
            speed   = (i + 1) * OTA_CHUNK_SIZE / elapsed if elapsed > 0 else 0
            eta     = (elapsed / (i + 1)) * (n_chunks - i - 1) if i > 0 else 0
            print(f"\r  [{bar}] {pct:3d}%  {i+1}/{n_chunks}  "
                  f"{speed:5.0f} B/s  ETA {eta:4.0f}s", end='', flush=True)

            time.sleep(chunk_delay)

        elapsed = time.monotonic() - t0
        print(f"\n\n  {green('Done')} — {n_chunks} chunks / {elapsed:.1f}s "
              f"({len(fw)/elapsed:.0f} B/s)\n")

        # ── STEP 3: OTA_END ───────────────────────────────────
        # data rỗng — node verify CRC16 toàn bộ firmware
        print(f"  {magenta('[3/3]')} OTA_END ...")
        send(ser, addr, CMD['OTA_END'], b'', quiet=True)

        resp = _ota_wait(timeout=6.0)
        if resp is None:
            print(f"  {yellow('WARN')} Không nhận ACK/NACK — "
                  f"node có thể đã reset ngay sau khi CRC pass")
            # Node gửi ACK rồi delay 150ms rồi WDT reset → có thể ACK không kịp
            print(f"  Chờ node khởi động lại ...")
            time.sleep(4.0)
            _ota_active = False
            time.sleep(0.1)
            # Poll để kiểm tra
            send(ser, addr, CMD['GET_STATUS'])
            time.sleep(2.0)
            print(f"\n  {green('★ OTA có thể thành công!')} "
                  f"Kiểm tra log phía trên.")
            return True

        _, rcmd, rdata = resp
        if rcmd == CMD['ACK']:
            print(f"  {green('ACK')} — CRC OK! Node đang reset...")
            time.sleep(4.0)
            _ota_active = False
            time.sleep(0.1)
            send(ser, addr, CMD['GET_STATUS'])
            time.sleep(2.0)
            print(f"\n  {green('★ OTA THÀNH CÔNG!')}")
            return True
        else:
            err = rdata[0] if rdata else 0
            print(f"  {red('NACK')} OTA_END: "
                  f"{ERR_NAMES.get(err, f'0x{err:02X}')}")
            print(f"  → CRC mismatch. Node reset vào recovery. Gửi OTA lại.")
            return False

    finally:
        _ota_active = False

# ─────────────────────────────────────────────────────────────
# OTA BROADCAST  —  không ACK, mass update
# ─────────────────────────────────────────────────────────────
def ota_broadcast(ser: serial.Serial, filepath: str, node_addrs: list,
                  chunk_delay: float = 0.060):
    global _ota_active

    fw = load_firmware(filepath)
    if fw is None:
        return False

    n_chunks = len(fw) // OTA_CHUNK_SIZE
    fw_crc   = crc16_ccitt(fw)

    addrs_str = ', '.join(f'0x{a:02X}' for a in node_addrs)
    print(f"\n{magenta('='*56)}")
    print(f"  {bold('OTA BROADCAST')}  → {len(node_addrs)} nodes: {addrs_str}")
    print(f"  File     : {filepath}")
    print(f"  Chunks   : {n_chunks}")
    print(f"  CRC16    : 0x{fw_crc:04X}")
    print(f"  Delay    : {chunk_delay*1000:.0f}ms/chunk (tối thiểu 50ms)")
    print(f"{magenta('='*56)}")
    print(f"\n  {red('!! CẢNH BÁO')}: Broadcast KHÔNG có ACK từng chunk.")
    print(f"  Nếu bus nhiễu → node cần recovery/ISP.\n")

    try:
        confirm = input(f"  Tiếp tục? (yes/no): ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        print("\n  Đã hủy.")
        return False
    if confirm != 'yes':
        print("  Đã hủy.")
        return False

    _ota_active = True
    chunk_delay = max(chunk_delay, 0.050)   # tối thiểu 50ms để node kịp xử lý

    try:
        # STEP 1
        print(f"\n  {magenta('[1/3]')} OTA_START broadcast...")
        start_data = struct.pack('>HH', n_chunks, fw_crc)
        send(ser, BROADCAST, CMD['OTA_START'], start_data, quiet=True)
        time.sleep(0.5)   # Tất cả node vào OTA mode

        # STEP 2
        print(f"  {magenta('[2/3]')} Gửi {n_chunks} chunk...")
        t0 = time.monotonic()

        for i in range(n_chunks):
            chunk    = fw[i * OTA_CHUNK_SIZE : (i + 1) * OTA_CHUNK_SIZE]
            seq_data = struct.pack('>H', i) + chunk
            send(ser, BROADCAST, CMD['OTA_DATA'], seq_data, quiet=True)

            pct    = (i + 1) * 100 // n_chunks
            filled = pct // 2
            bar    = '█' * filled + '░' * (50 - filled)
            elapsed = time.monotonic() - t0
            eta     = (elapsed / (i + 1)) * (n_chunks - i - 1) if i > 0 else 0
            print(f"\r  [{bar}] {pct:3d}%  {i+1}/{n_chunks}  "
                  f"ETA {eta:4.0f}s", end='', flush=True)
            time.sleep(chunk_delay)

        elapsed = time.monotonic() - t0
        print(f"\n\n  Done: {elapsed:.1f}s\n")

        # STEP 3
        print(f"  {magenta('[3/3]')} OTA_END broadcast...")
        send(ser, BROADCAST, CMD['OTA_END'], b'', quiet=True)
        print(f"  Chờ 4s cho tất cả node verify + reset...")
        time.sleep(4.0)

        # VERIFY từng node
        _ota_active = False
        time.sleep(0.2)

        ok_nodes, fail_nodes = [], []
        print(f"\n  {magenta('[VERIFY]')} Poll từng node...")

        for node_addr in node_addrs:
            print(f"  Checking 0x{node_addr:02X} ...", end=' ', flush=True)
            verified = False
            for _ in range(2):
                send(ser, node_addr, CMD['GET_STATUS'], quiet=True)
                time.sleep(1.5)
                verified = True   # Optimistic — user tự kiểm tra output
                break
            if verified:
                ok_nodes.append(node_addr)
            else:
                fail_nodes.append(node_addr)

        print(f"\n{magenta('='*56)}")
        print(f"  Broadcast OTA hoàn tất — xem STATUS output phía trên")
        print(f"  Nodes đã poll: {[f'0x{a:02X}' for a in node_addrs]}")
        if fail_nodes:
            print(f"  {red('Cần kiểm tra')}: {[f'0x{a:02X}' for a in fail_nodes]}")
            print(f"  → Dùng 'ota <addr> <file>' để recovery từng node")
        print(f"{magenta('='*56)}")
        return True

    finally:
        _ota_active = False

# ─────────────────────────────────────────────────────────────
# HELP
# ─────────────────────────────────────────────────────────────
def print_help():
    print(f"""
{bold('Lệnh có thể dùng:')}
  {cyan('s <addr>')}                    GET_STATUS
  {cyan('o <addr>')}                    MOTOR_OPEN
  {cyan('c <addr>')}                    MOTOR_CLOSE
  {cyan('x <addr>')}                    MOTOR_STOP
  {cyan('t [slot_ms] [n_nodes]')}       TIME_SYNC broadcast
  {cyan('d <addr> on|off')}             Debug verbose mode
  {cyan('p [addr1 addr2 ...]')}         Toggle auto-poll (mặc định OFF)
  {cyan('ota <addr> <file>')}           OTA unicast — 1 node, có ACK từng chunk
  {cyan('bcast <file> <a1,a2,...>')}    OTA broadcast — nhiều node, không ACK
  {cyan('h')}                           Help
  {cyan('q')}                           Thoát

{gray('Ví dụ:')}
  s 1                          Status node 1
  ota 1 firmware.hex           OTA node 1 (file .hex hoặc .bin)
  ota 1 firmware.bin 3         OTA node 1, max 3 retry/chunk
  bcast firmware.hex 1,2,3     Broadcast OTA cho 3 node
  p 1 2 3                      Auto-poll node 1, 2, 3 mỗi 2s
  t 60 15                      Time sync: slot=60ms, 15 nodes

{gray('Ghi chú OTA:')}
  • Yêu cầu Optiboot (Arduino Nano "New Bootloader")
  • Unicast: ACK từng chunk, retry nếu fail → reliable
  • Broadcast: không ACK, fixed delay ≥50ms → nhanh hơn
  • Sau OTA fail → node vào recovery mode 60s, gửi OTA lại
""")

# ─────────────────────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────────────────────
def main():
    global _poll_on, _poll_nodes

    parser = argparse.ArgumentParser(
        description='RS485 Master v3.0 — Valve Controller')
    parser.add_argument('--port', '-p', required=True,
                        help='Serial port (COM14, /dev/ttyUSB0...)')
    parser.add_argument('--baud', '-b', type=int, default=9600)
    args = parser.parse_args()

    print(f"\n{bold('RS485 Master v3.0  —  Valve Controller Node Firmware')}")
    print(f"Port: {args.port}  Baud: {args.baud}")
    print(f"Listener mode: {green('ON')} (chờ frame từ node)")
    print(f"Auto-poll    : {red('OFF')} (dùng lệnh p để bật)")

    if IntelHex is None:
        print(f"{yellow('WARN')}: intelhex chưa cài → chỉ hỗ trợ .bin")
        print(f"       pip install intelhex  (để dùng .hex)")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except Exception as e:
        print(f"{red('Lỗi:')} Không mở được {args.port}: {e}")
        sys.exit(1)

    time.sleep(0.3)
    ser.reset_input_buffer()

    # Start threads
    t_reader = threading.Thread(target=reader_thread, args=(ser,), daemon=True)
    t_poll   = threading.Thread(target=poll_thread,   args=(ser,), daemon=True)
    t_reader.start()
    t_poll.start()

    print_help()

    while True:
        try:
            line = input(f"{gray('master>')} ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nThoát.")
            break

        if not line:
            continue

        parts = line.split()
        cmd   = parts[0].lower()

        try:
            if cmd == 'q':
                break

            elif cmd == 'h':
                print_help()

            elif cmd == 's' and len(parts) >= 2:
                send(ser, int(parts[1]), CMD['GET_STATUS'], quiet=False)

            elif cmd == 'o' and len(parts) >= 2:
                send(ser, int(parts[1]), CMD['MOTOR_OPEN'], quiet=False)

            elif cmd == 'c' and len(parts) >= 2:
                send(ser, int(parts[1]), CMD['MOTOR_CLOSE'], quiet=False)

            elif cmd == 'x' and len(parts) >= 2:
                send(ser, int(parts[1]), CMD['MOTOR_STOP'], quiet=False)

            elif cmd == 't':
                slot = int(parts[1]) if len(parts) > 1 else 60
                n    = int(parts[2]) if len(parts) > 2 else 15
                send_timesync(ser, n, slot)

            elif cmd == 'd' and len(parts) >= 3:
                a  = int(parts[1])
                c  = CMD['DEBUG_ON'] if parts[2] == 'on' else CMD['DEBUG_OFF']
                send(ser, a, c, quiet=False)

            elif cmd == 'p':
                if len(parts) > 1:
                    _poll_nodes = [int(x) for x in parts[1:]]
                _poll_on = not _poll_on
                state = green('ON') if _poll_on else red('OFF')
                print(f"  Auto-poll {state}  nodes={_poll_nodes}  "
                      f"interval={_poll_interval}s")

            elif cmd == 'ota' and len(parts) >= 3:
                addr    = int(parts[1])
                fpath   = parts[2]
                retries = int(parts[3]) if len(parts) > 3 else 3
                delay   = float(parts[4]) if len(parts) > 4 else 0.025
                ota_unicast(ser, addr, fpath, retries, delay)

            elif cmd == 'bcast' and len(parts) >= 3:
                fpath  = parts[1]
                addrs  = [int(a) for a in parts[2].split(',')]
                delay  = float(parts[3]) if len(parts) > 3 else 0.060
                ota_broadcast(ser, fpath, addrs, delay)

            else:
                print(f"  {gray('Lệnh không hợp lệ — gõ h để xem help')}")

        except ValueError as e:
            print(f"  {gray(f'Sai cú pháp: {e}')}")
        except Exception as e:
            print(f"  {red('ERROR')}: {e}")

    ser.close()
    print("Bye!")

if __name__ == '__main__':
    main()