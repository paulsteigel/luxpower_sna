"""
LuxPower Transparent Proxy + Command Interface
==============================================
Chạy: python lux_proxy.py
Đổi Server Address dongle → IP máy này (192.168.100.30)
"""

import socket, threading, struct, time, sys, os

LOCAL_PORT    = 4346
LUX_SERVER_IP = "192.168.1.6" # "120.79.53.27" # "47.81.11.236"
LUX_PORT      = 4346
DONGLE_SN     = "BA32500699"
INVERTER_SN   = "3253631886"

# ── Log file: cùng thư mục với script ────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_PATH   = os.path.join(SCRIPT_DIR, "lux_traffic.log")

# ── Register info: (tên, scale, đơn vị) ──────────────────────────────────────
HOLD_INFO = {
    0:   ("hold_model",   1.0,  ""),
    14:  ("uptime_tick",  1.0,  ""),   # tăng ~7/giây, không ghi
    21:  ("func_en",      1.0,  ""),
    64:  ("charge_pwr%",  1.0,  "%"),
    65:  ("dischg_pwr%",  1.0,  "%"),
    99:  ("la_chg_volt",  0.1,  "V"),
    101: ("charge_rate",  1.0,  "A"),
    102: ("dischg_rate",  1.0,  "A"),
    105: ("eod_soc",      1.0,  "%"),
    120: ("sys_enable",   1.0,  ""),
}

# bits[1:0] của lo-byte reg0
BAT_NAMES = {0: "Not Set", 1: "Lead-Acid", 2: "Lithium"}

# bits[5:2] của lo-byte reg0 → khớp value= trong dealer web
LI_NAMES = {
    0:  "Standard Battery",
    1:  "HINA Battery",
    2:  "Pylon/Freedom Won/Solar MD/Hubble/Blue Nova",
    3:  "Enopte",
    4:  "MSUN",
    5:  "GSL1 Battery",
    6:  "Lux",
    7:  "Aobo Battery",
    8:  "Rsvd",
    9:  "Stealth",
    10: "TeLongMei",
    11: "Merit",
    14: "WECO",
    15: "Murata",
    16: "BITEK",
    17: "OKSolar",
    18: "GW Battery",
    19: "CROWN",
    20: "Revov",
    21: "Beebeejump",
}

hold_cache  = {}
dongle_sock = None
lock        = threading.Lock()
log_file    = None

# ── CRC-16 Modbus ─────────────────────────────────────────────────────────────
def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc

# ── Build write single packet (fn=0x06) ──────────────────────────────────────
def build_write(reg, value):
    frame_len = 32
    hdr  = bytes([0xA1,0x1A, 0x02,0x00, frame_len&0xFF, frame_len>>8, 0x01, 0xC2])
    hdr += DONGLE_SN.encode('ascii')
    hdr += struct.pack('<H', 18)
    df   = bytes([0x01, 0x06]) + INVERTER_SN.encode('ascii') + struct.pack('<HH', reg, value)
    return hdr + df + struct.pack('<H', crc16(df))

# ── Build write multiple packet (fn=0x10) — dùng cho reg0 battery type ───────
def build_write_multi(reg0_val, reg1_val=0x0100):
    """
    fn=0x10 WRITE_MULTIPLE_REGISTERS cho reg0 (battery type+brand).
    Data dạng Big-Endian (confirmed từ captured packets).
    """
    unknown = bytes.fromhex('2b130000000000000000')  # constant, observed
    df  = bytes([0x01, 0x10])
    df += unknown
    df += struct.pack('<H', 0)              # start_reg = 0 (LE)
    df += struct.pack('<H', 2)              # reg_count = 2 (LE)
    df += bytes([0x04])                     # byte_count = 4
    df += bytes([(reg0_val>>8)&0xFF, reg0_val&0xFF])   # reg0 BE
    df += bytes([(reg1_val>>8)&0xFF, reg1_val&0xFF])   # reg1 BE
    df += struct.pack('<H', crc16(df))

    data_len  = len(df)   # = 23
    frame_len = data_len + 14
    hdr  = bytes([0xA1,0x1A, 0x02,0x00, frame_len&0xFF, frame_len>>8, 0x01, 0xC2])
    hdr += DONGLE_SN.encode('ascii')
    hdr += struct.pack('<H', data_len)
    return hdr + df

# ── Decode reg 0 (hold_model) ─────────────────────────────────────────────────
def unpack_model(raw):
    """
    raw = 16-bit register value (LE từ inverter)
    lo byte: bits[1:0] = battery_type (1=LeadAcid, 2=Lithium)
             bits[5:2] = lithium_brand (0-indexed, khớp dealer web)
    hi byte: 0x80 (fixed)
    """
    lo = raw & 0xFF
    return {
        'battery_type': lo & 0x03,
        'lithium_type': (lo >> 2) & 0x0F,
    }

def model_str(raw):
    m = unpack_model(raw)
    bat  = BAT_NAMES.get(m['battery_type'], f"?{m['battery_type']}")
    brand = LI_NAMES.get(m['lithium_type'], f"brand{m['lithium_type']}")
    return f"{bat} / {brand} (raw=0x{raw:04X})"

# ── Decode reg 120 sys_enable bitmap ─────────────────────────────────────────
def decode_sys_enable(val):
    dischg     = (val >> 4) & 0x3
    eod_type   = (val >> 6) & 0x1
    gen_chg    = (val >> 7) & 0x1
    dischg_str = {0:"Voltage", 1:"SOC", 2:"Power"}.get(dischg, f"?{dischg}")
    return f"DischgCtrl={dischg_str} EOD={'SOC' if eod_type else 'Voltage'} GenChg={gen_chg}"

# ── Parse một giá trị register HOLD (Little-Endian) ──────────────────────────
def parse_hold_reg(ridx, raw_lo, raw_hi):
    """raw_lo, raw_hi là 2 bytes theo thứ tự trong packet (LE)."""
    return raw_lo | (raw_hi << 8)

# ── Decode + log frame ────────────────────────────────────────────────────────
def log_frame(raw, direction):
    if len(raw) < 20 or raw[0] != 0xA1 or raw[1] != 0x1A:
        return
    ts     = time.strftime("%H:%M:%S")
    seq    = raw[6]
    tcp_fn = raw[7]
    prefix = f"[{ts}] seq={seq:03d} {direction:14s}"

    try:
        # ── Heartbeat ────────────────────────────────────────────────────────
        if tcp_fn == 0xC1:
            msg = f"{prefix}   HEARTBEAT"
            print(msg)
            if log_file:
                log_file.write(msg + "\n"); log_file.flush()
            return

        if tcp_fn != 0xC2:
            print(f"{prefix}   tcp_fn=0x{tcp_fn:02X} (unknown)")
            return

        df     = raw[20:]
        dev_fn = df[1]
        reg    = df[12] | (df[13] << 8)

        # ── WRITE SINGLE fn=0x06 ─────────────────────────────────────────────
        if dev_fn == 0x06:
            val  = df[14] | (df[15] << 8)
            info = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))
            extra = f" = {val * info[1]:.1f}{info[2]}" if info[2] else ""
            msg  = f"{prefix} ★ WRITE_SINGLE reg={reg}(0x{reg:02X}) [{info[0]}] = {val}{extra}"
            if reg == 0:
                msg += f"\n        → {model_str(val)}"
            elif reg == 120:
                msg += f"\n        → {decode_sys_enable(val)}"
            with lock:
                hold_cache[reg] = val

        # ── WRITE MULTI fn=0x10 ──────────────────────────────────────────────
        elif dev_fn == 0x10:
            # df[14..15] = reg_count LE, df[16] = byte_count, df[17:] = data BE
            count      = df[14] | (df[15] << 8)
            byte_count = df[16] if len(df) > 16 else 0
            data_bytes = df[17:17+byte_count] if len(df) >= 17+byte_count else df[17:]
            msg = (f"{prefix} ★ WRITE_MULTI reg={reg}..{reg+count-1} count={count}"
                   f"\n        DATA(BE): {data_bytes.hex()}")
            if len(data_bytes) >= 2:
                r0 = (data_bytes[0] << 8) | data_bytes[1]   # BE trong packet
                msg += f"\n        → reg0=0x{r0:04X} : {model_str(r0)}"
            if len(data_bytes) >= 4:
                r1 = (data_bytes[2] << 8) | data_bytes[3]
                msg += f"  reg1=0x{r1:04X}"

        # ── READ HOLD fn=0x03 ────────────────────────────────────────────────
        elif dev_fn == 0x03:
            if direction == "DONGLE→SERVER":
                # Response: df[14]=byte_count, df[15:]=data LE pairs
                byte_count = df[14]
                data_bytes = df[15:15+byte_count]
                n_regs     = byte_count // 2
                msg = (f"{prefix}   READ_HOLD_RESP  "
                       f"reg={reg}..{reg+n_regs-1} ({n_regs} regs)")
                lines = []
                for i in range(n_regs):
                    # ✅ Little-Endian: lo byte trước, hi byte sau
                    r    = data_bytes[i*2] | (data_bytes[i*2+1] << 8)
                    ridx = reg + i
                    info = HOLD_INFO.get(ridx, (f"reg{ridx}", 1.0, ""))
                    extra = f"  {r * info[1]:.1f}{info[2]}" if info[2] else ""
                    detail = ""
                    if ridx == 0:
                        detail = f"  → {model_str(r)}"
                    elif ridx == 120:
                        detail = f"  → {decode_sys_enable(r)}"
                    lines.append(f"  [{ridx:3d}] {info[0]:>14s} = {r:5d}{extra}{detail}")
                    with lock:
                        hold_cache[ridx] = r
                if lines:
                    msg += "\n" + "\n".join(lines)
            else:
                # Request từ server
                count = df[14] | (df[15] << 8)
                msg   = f"{prefix}   READ_HOLD_REQ   reg={reg}..{reg+count-1} count={count}"

        # ── READ INPUT fn=0x04 ───────────────────────────────────────────────
        elif dev_fn == 0x04:
            if direction == "DONGLE→SERVER":
                byte_count = df[14]
                data_bytes = df[15:15+byte_count]
                n_regs     = byte_count // 2
                msg = (f"{prefix}   READ_INPUT_RESP "
                       f"reg={reg}..{reg+n_regs-1} ({n_regs} regs)"
                       f"\n        RAW: {data_bytes.hex()}")
            else:
                count = df[14] | (df[15] << 8)
                msg   = f"{prefix}   READ_INPUT_REQ  reg={reg}..{reg+count-1} count={count}"

        # ── Exception responses ──────────────────────────────────────────────
        elif dev_fn in (0x83, 0x84, 0x86):
            orig = dev_fn & 0x7F
            code = df[2] if len(df) > 2 else 0xFF
            msg  = f"{prefix} ✗ EXCEPTION fn=0x{orig:02X} code=0x{code:02X}"

        else:
            msg = f"{prefix}   fn=0x{dev_fn:02X} reg={reg}\n        df_hex: {df[:32].hex()}"

        print(msg)
        if log_file:
            log_file.write(msg + "\n"); log_file.flush()

    except Exception as e:
        import traceback
        tb  = traceback.format_exc()
        err = f"{prefix} !! log_frame ERROR: {e}\n{tb}\n   raw: {raw.hex()}"
        print(err)
        if log_file:
            log_file.write(err + "\n"); log_file.flush()

# ── Forward ───────────────────────────────────────────────────────────────────
def forward(src, dst, label):
    buf = b''
    try:
        while True:
            data = src.recv(4096)
            if not data: break
            buf += data
            while len(buf) >= 6:
                if buf[0] != 0xA1 or buf[1] != 0x1A:
                    buf = buf[1:]; continue
                total = (buf[4] | buf[5]<<8) + 6
                if len(buf) < total: break
                frame, buf = buf[:total], buf[total:]
                log_frame(frame, label)
                dst.sendall(frame)
    except Exception:
        pass
    finally:
        try: src.close()
        except: pass
        try: dst.close()
        except: pass

# ── Send helpers ──────────────────────────────────────────────────────────────
def send_cmd(reg, value, label=""):
    with lock: sock = dongle_sock
    if not sock:
        print("[!] Chưa có kết nối dongle"); return False
    pkt  = build_write(reg, value)
    info = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))
    ts   = time.strftime("%H:%M:%S")
    msg  = (f"[{ts}] [CMD]  ★ WRITE reg={reg}(0x{reg:02X}) [{info[0]}] {label} = {value}(0x{value:04X})"
            f"\n        pkt: {pkt.hex()}")
    print(msg)
    if log_file: log_file.write(msg + "\n"); log_file.flush()
    try:
        sock.sendall(pkt); return True
    except Exception as e:
        print(f"[!] Gửi thất bại: {e}"); return False

def send_cmd_multi(reg0_val, reg1_val=0x0100, label=""):
    with lock: sock = dongle_sock
    if not sock:
        print("[!] Chưa có kết nối dongle"); return False
    pkt = build_write_multi(reg0_val, reg1_val)
    ts  = time.strftime("%H:%M:%S")
    msg = (f"[{ts}] [CMD]  ★ WRITE_MULTI reg0=0x{reg0_val:04X} reg1=0x{reg1_val:04X} {label}"
           f"\n        → {model_str(reg0_val)}"
           f"\n        pkt: {pkt.hex()}")
    print(msg)
    if log_file: log_file.write(msg + "\n"); log_file.flush()
    try:
        sock.sendall(pkt); return True
    except Exception as e:
        print(f"[!] Gửi thất bại: {e}"); return False

# ── Commands ──────────────────────────────────────────────────────────────────
def handle(cmd):
    p  = cmd.strip().split()
    op = p[0].lower() if p else ""

    if op == "help":
        print("""
  set_charge <A>           dòng sạc tối đa    reg 101
  set_discharge <A>        dòng xả tối đa     reg 102
  set_eod_soc              EOD → SOC mode      reg 120 bit4-5=1
  set_eod_volt             EOD → Voltage mode  reg 120 bit4-5=0
  set_battery <brand_idx>  đổi loại pin (Lithium, brand 0-21)
  set_leadacid             đổi sang Lead-Acid  (brand giữ nguyên)
  get <reg>                đọc cache
  status                   tóm tắt kết nối + regs
  brands                   danh sách brand lithium
  help                     danh sách lệnh
""")

    elif op == "brands":
        print("\n  Lithium brand list (bits[5:2] trong reg0):")
        for k, v in LI_NAMES.items():
            print(f"    {k:2d}: {v}")
        print()

    elif op == "set_charge":
        if len(p) < 2: print("Cú pháp: set_charge <A>"); return
        send_cmd(101, int(p[1]), "→ charge rate")

    elif op == "set_discharge":
        if len(p) < 2: print("Cú pháp: set_discharge <A>"); return
        send_cmd(102, int(p[1]), "→ discharge rate")

    elif op == "set_eod_soc":
        with lock: cur = hold_cache.get(120, 0)
        new = (cur & ~0xB0) | 0x90
        print(f"    reg120: 0x{cur:04X} → 0x{new:04X} | {decode_sys_enable(new)}")
        send_cmd(120, new, "→ EOD by SOC")

    elif op == "set_eod_volt":
        with lock: cur = hold_cache.get(120, 0)
        new = cur & ~0x30
        print(f"    reg120: 0x{cur:04X} → 0x{new:04X} | {decode_sys_enable(new)}")
        send_cmd(120, new, "→ EOD by Voltage")

    elif op == "set_battery":
        if len(p) < 2:
            print("Cú pháp: set_battery <brand_idx>  (xem 'brands' để tra số)")
            return
        brand = int(p[1])
        if brand not in LI_NAMES:
            print(f"    Brand {brand} không hợp lệ. Gõ 'brands' để xem danh sách.")
            return
        reg0_val = 0x8000 | ((brand & 0x0F) << 2) | 0x02  # Lithium + brand
        with lock: cur = hold_cache.get(0, None)
        if cur is not None:
            print(f"    Hiện tại: {model_str(cur)}")
        print(f"    → Mới:     {model_str(reg0_val)}")
        confirm = input("    Xác nhận? (yes/no): ").strip().lower()
        if confirm == "yes":
            send_cmd_multi(reg0_val, 0x0100, f"→ Lithium brand{brand}")
            print("    ⚠ Inverter sẽ restart ~15-20s, chờ trước khi ghi tiếp!")
        else:
            print("    Hủy")

    elif op == "set_leadacid":
        with lock: cur = hold_cache.get(0, None)
        if cur is None:
            print("    Chưa có cache reg0"); return
        m = unpack_model(cur)
        # Giữ nguyên brand, đổi battery_type = 1
        reg0_val = 0x8000 | ((m['lithium_type'] & 0x0F) << 2) | 0x01
        print(f"    Hiện tại: {model_str(cur)}")
        print(f"    → Mới:     {model_str(reg0_val)}")
        confirm = input("    Xác nhận? (yes/no): ").strip().lower()
        if confirm == "yes":
            send_cmd_multi(reg0_val, 0x0100, "→ LeadAcid")
            print("    ⚠ Inverter sẽ restart ~15-20s, chờ trước khi ghi tiếp!")
        else:
            print("    Hủy")

    elif op == "get":
        if len(p) < 2: print("Cú pháp: get <reg>"); return
        reg = int(p[1], 0)
        with lock: val = hold_cache.get(reg)
        if val is None:
            print(f"    reg {reg} chưa có trong cache"); return
        info = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))
        print(f"    reg {reg}(0x{reg:02X}) [{info[0]}] = {val}  →  {val*info[1]:.1f}{info[2]}")
        if reg == 0:
            print(f"      {model_str(val)}")
        elif reg == 120:
            print(f"      {decode_sys_enable(val)}")

    elif op == "status":
        with lock:
            connected = dongle_sock is not None
            cache = dict(hold_cache)
        print(f"\n  Dongle : {'✓ Kết nối' if connected else '✗ Chưa kết nối'}"
              f"  | Cache: {len(cache)} regs  | Log: {LOG_PATH}")
        for reg in [0, 99, 101, 102, 105, 120]:
            if reg not in cache: continue
            val  = cache[reg]
            info = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))
            if reg == 0:
                extra = f"  → {model_str(val)}"
            elif reg == 120:
                extra = f"  → {decode_sys_enable(val)}"
            else:
                extra = f"  {val*info[1]:.1f}{info[2]}" if info[2] else ""
            print(f"    [{reg:3d}] {info[0]:14s} = {val:6d}{extra}")
        print()

    else:
        print(f"  Lệnh không rõ: '{op}'  (gõ 'help')")

# ── Client handler ────────────────────────────────────────────────────────────
def handle_client(client, addr):
    global dongle_sock
    print(f"\n[+] Dongle: {addr[0]}:{addr[1]}")
    try:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.connect((LUX_SERVER_IP, LUX_PORT))
        print("[+] Server LuxPower OK")
    except Exception as e:
        print(f"[!] Server unreachable: {e}")
        client.close(); return
    with lock:
        dongle_sock = client
    t1 = threading.Thread(target=forward, args=(client, srv, "DONGLE→SERVER"), daemon=True)
    t2 = threading.Thread(target=forward, args=(srv, client, "SERVER→DONGLE"), daemon=True)
    t1.start(); t2.start()
    t1.join();  t2.join()
    with lock:
        dongle_sock = None
    print("[-] Session kết thúc")

def cmd_loop():
    time.sleep(1.5)
    print(f"\n[CMD] Sẵn sàng. Gõ 'help'  | Log: {LOG_PATH}\n")
    while True:
        try:
            line = input("lux> ")
            if line.strip(): handle(line)
        except (EOFError, KeyboardInterrupt):
            break

def main():
    global log_file
    log_file = open(LOG_PATH, "a", encoding="utf-8")
    log_file.write(f"\n{'='*60}\nSession: {time.strftime('%Y-%m-%d %H:%M:%S')}\n{'='*60}\n")
    print("╔════════════════════════════════════════════════════╗")
    print("║  LuxPower Transparent Proxy + Commander            ║")
    print(f"║  Listen  : 0.0.0.0:{LOCAL_PORT}                         ║")
    print(f"║  Forward : {LUX_SERVER_IP}:{LUX_PORT}            ║")
    print(f"║  Log     : {LOG_PATH}")
    print("╚════════════════════════════════════════════════════╝")
    print(f"\n>>> Đổi Server Address dongle → 192.168.100.30\n")
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", LOCAL_PORT))
    srv.listen(5)
    threading.Thread(target=cmd_loop, daemon=True).start()
    print("[*] Chờ dongle kết nối...")
    try:
        while True:
            c, a = srv.accept()
            threading.Thread(target=handle_client, args=(c,a), daemon=True).start()
    except KeyboardInterrupt:
        print("\n[*] Dừng.")
    finally:
        srv.close()
        log_file.close()

if __name__ == "__main__":
    main()