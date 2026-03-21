"""
LuxPower Transparent Proxy + Command Interface
==============================================
Chạy: python lux_proxy.py
Đổi Server Address dongle → IP máy này (192.168.100.30)
"""

import socket, threading, struct, time, sys, os

LOCAL_PORT    = 4346
LUX_SERVER_IP = "120.79.53.27"
LUX_PORT      = 4346
DONGLE_SN     = "BA32500699"
INVERTER_SN   = "3253631886"
LITHIUM_BRAND = 6

# Constant field từ reverse engineering captured packets
WRITE_MULTI_UNKNOWN = bytes.fromhex('2b130000000000000000')

# ── Log file: cùng thư mục với script ────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_PATH   = os.path.join(SCRIPT_DIR, "lux_traffic.log")

HOLD_INFO = {
    0:   ("hold_model",   1.0, ""),
    21:  ("func_en",      1.0, ""),
    64:  ("charge_pwr%",  1.0, "%"),
    65:  ("dischg_pwr%",  1.0, "%"),
    99:  ("la_chg_volt",  0.1, "V"),
    101: ("charge_rate",  1.0, "A"),
    102: ("dischg_rate",  1.0, "A"),
    105: ("eod_soc",      1.0, "%"),
    120: ("sys_enable",   1.0, ""),
}

BAT_NAMES = {0: "Lead-Acid", 1: "Lithium", 2: "Not Selected"}
LI_NAMES  = {0:"SPI",1:"CAN-Pylon",2:"485-Pylon",3:"CAN-WECO",4:"485-WECO",
             5:"CAN-Soltaro",6:"CAN-Alpha",7:"CAN-Varta",8:"CAN-Growatt",
             9:"485-Growatt",10:"CAN-Generic",11:"485-Generic"}

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
    func      = 0x06
    frame_len = 32
    hdr  = bytes([0xA1,0x1A, 0x02,0x00, frame_len&0xFF, frame_len>>8, 0x01, 0xC2])
    hdr += DONGLE_SN.encode('ascii')
    hdr += struct.pack('<H', 18)
    df   = bytes([0x01, func]) + INVERTER_SN.encode('ascii') + struct.pack('<HH', reg, value)
    return hdr + df + struct.pack('<H', crc16(df))

# ── Build write multiple packet (fn=0x10) — dùng cho reg0 battery type ───────
def build_write_multi(reg0_val, reg1_val=0x0100):
    """
    fn=0x10 WRITE_MULTIPLE_REGISTERS
    Cấu trúc df (21 bytes, trước CRC):
      [action=0x01][fn=0x10][unknown(10)][start_reg(2)][count(2)][byte_count(1)][reg0_BE(2)][reg1_BE(2)]
    CRC16/Modbus trên 21 bytes df
    unknown = 2b130000000000000000 (từ captured packets, có thể là timestamp/session)
    """
    df  = bytes([0x01, 0x10])
    df += WRITE_MULTI_UNKNOWN              # 10 bytes unknown field
    df += struct.pack('<H', 0)             # start_reg = 0
    df += struct.pack('<H', 2)             # reg_count = 2
    df += bytes([0x04])                    # byte_count = 4
    df += bytes([(reg0_val>>8)&0xFF, reg0_val&0xFF])   # reg0 big-endian
    df += bytes([(reg1_val>>8)&0xFF, reg1_val&0xFF])   # reg1 big-endian
    df += struct.pack('<H', crc16(df))     # CRC ở cuối df

    data_len  = len(df)   # = 23
    frame_len = data_len + 14
    hdr  = bytes([0xA1,0x1A, 0x02,0x00, frame_len&0xFF, frame_len>>8, 0x01, 0xC2])
    hdr += DONGLE_SN.encode('ascii')
    hdr += struct.pack('<H', data_len)
    return hdr + df

# ── HOLD_MODEL reg 0 ──────────────────────────────────────────────────────────
def unpack_model(raw):
    return {'battery_type':(raw>>12)&0xF, 'lithium_type':(raw>>8)&0xF,
            'measurement': (raw>>6)&0x3,  'meter_brand': (raw>>4)&0x3,
            'us_version':  (raw>>3)&0x1,  'meter_type':   raw    &0x7}

def pack_model(m):
    return (((m['battery_type']&0xF)<<12)|((m['lithium_type']&0xF)<<8)|
            ((m['measurement'] &0x3)<<6) |((m['meter_brand'] &0x3)<<4)|
            ((m['us_version']  &0x1)<<3) | (m['meter_type']  &0x7))

# ── Decode reg 120 sys_enable bitmap ─────────────────────────────────────────
def decode_sys_enable(val):
    ac_type   = (val >> 1) & 0x7
    dischg    = (val >> 4) & 0x3
    eod_type  = (val >> 6) & 0x1
    gen_chg   = (val >> 7) & 0x1
    dischg_str = {0:"Voltage", 1:"SOC", 2:"Power"}.get(dischg, f"?{dischg}")
    eod_str    = "SOC" if eod_type else "Voltage"
    return f"DischgCtrl={dischg_str} EOD={eod_str} GenChg={gen_chg}"

# ── Cache hold regs ───────────────────────────────────────────────────────────
def cache_hold_response(df):
    """Cache hold register READ response (server→dongle, fn=0x03 with data)."""
    try:
        if len(df) < 17: return
        byte_count = df[14]
        data = df[15:15+byte_count]
        start = df[12] | (df[13]<<8)
        n = byte_count // 2
        with lock:
            for i in range(n):
                hold_cache[start+i] = (data[i*2]<<8) | data[i*2+1]
    except Exception:
        pass

# ── Decode + log frame ────────────────────────────────────────────────────────
def log_frame(raw, direction):
    if len(raw) < 20 or raw[0] != 0xA1: return
    ts = time.strftime("%H:%M:%S")
    try:
        tcp_fn = raw[7]
        df = raw[20:]

        # Heartbeat
        if tcp_fn == 0xC1:
            return

        if tcp_fn != 0xC2 or len(df) < 2:
            return

        dev_fn = df[1]
        reg    = df[12] | (df[13]<<8) if len(df) >= 14 else 0
        val    = df[14] | (df[15]<<8) if len(df) >= 16 else 0

        if dev_fn == 0x06:
            # WRITE SINGLE
            name   = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))[0]
            scale  = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))[1]
            unit   = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))[2]
            msg    = f"[{ts}] {direction:14s} ★ WRITE reg={reg}(0x{reg:02X}) [{name}] = {val}"
            if unit:
                msg += f" = {val*scale:.1f}{unit}"
            if reg == 0:
                m = unpack_model(val)
                msg += f"\n        Battery={BAT_NAMES.get(m['battery_type'],'?')} Brand={LI_NAMES.get(m['lithium_type'],'?')} raw=0x{val:04X}"
            elif reg == 120:
                msg += f"\n        {decode_sys_enable(val)}"
            with lock:
                hold_cache[reg] = val

        elif dev_fn == 0x10:
            # WRITE MULTIPLE — log full raw để reverse engineer
            count = val  # df[14]|df[15]<<8 = register count
            # Modbus fn=0x10 request format:
            # [addr_hi][addr_lo][count_hi][count_lo][byte_count][data...]
            # df positions: [0]=action [1]=fn [2..11]=inv_sn [12..13]=start_reg
            #               [14..15]=reg_count [16]=byte_count [17..]=data
            byte_count = df[16] if len(df) > 16 else 0
            data_bytes = df[17:17+byte_count] if len(df) >= 17+byte_count else df[17:]

            msg = (f"[{ts}] {direction:14s} ★ WRITE_MULTI "
                   f"reg={reg}..{reg+count-1} count={count} bytes={byte_count}"
                   f"\n        DATA: {data_bytes.hex()}"
                   f"\n        FULL_DF[12:]: {df[12:12+byte_count+8].hex()}")

            # Try decode as big-endian registers
            if len(data_bytes) >= 2:
                regs_decoded = []
                for i in range(0, len(data_bytes)-1, 2):
                    r = (data_bytes[i]<<8)|data_bytes[i+1]
                    regs_decoded.append(f"reg{reg+i//2}=0x{r:04X}")
                msg += f"\n        REGS: {' | '.join(regs_decoded)}"
                if reg == 0 and len(data_bytes) >= 2:
                    r0 = (data_bytes[0]<<8)|data_bytes[1]
                    m = unpack_model(r0)
                    msg += f"\n        → Battery={BAT_NAMES.get(m['battery_type'],'?')} Brand={LI_NAMES.get(m['lithium_type'],'?')}"

        elif dev_fn == 0x03 and len(df) >= 17:
            # READ HOLD response (server→dongle) — has data
            byte_count_resp = df[14]
            if byte_count_resp > 0 and direction == "SERVER→DONGLE":
                cache_hold_response(df)
                start = df[12]|(df[13]<<8)
                n = byte_count_resp//2
                msg = f"[{ts}] {direction:14s}   READ_HOLD_RESP  reg={start}..{start+n-1} ({n} regs cached)"
            else:
                n = val
                msg = f"[{ts}] {direction:14s}   READ_HOLD       reg={reg}..{reg+n-1}"

        elif dev_fn == 0x04:
            n = val
            msg = f"[{ts}] {direction:14s}   READ_INPUT      reg={reg}..{reg+n-1}"

        else:
            msg = f"[{ts}] {direction:14s}   fn=0x{dev_fn:02X} reg={reg} val={val} | df[12:]: {df[12:20].hex()}"

        print(msg)
        if log_file:
            log_file.write(msg + "\n")
            log_file.flush()

    except Exception as e:
        pass

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

# ── Send command ──────────────────────────────────────────────────────────────
def send_cmd(reg, value, label=""):
    with lock:
        sock = dongle_sock
    if not sock:
        print("[!] Chưa có kết nối dongle"); return False
    pkt  = build_write(reg, value)
    name = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))[0]
    ts   = time.strftime("%H:%M:%S")
    msg  = (f"[{ts}] [CMD]          ★ WRITE reg={reg}(0x{reg:02X}) [{name}] {label} = {value}(0x{value:04X})"
            f"\n        pkt: {pkt.hex()}")
    print(msg)
    if log_file:
        log_file.write(msg + "\n")
        log_file.flush()
    try:
        sock.sendall(pkt); return True
    except Exception as e:
        print(f"[!] Gửi thất bại: {e}"); return False

def send_cmd_multi(reg0_val, reg1_val=0x0100, label=""):
    """Gửi fn=0x10 WRITE_MULTIPLE để đổi battery type (reg 0..1)"""
    with lock:
        sock = dongle_sock
    if not sock:
        print("[!] Chưa có kết nối dongle"); return False
    pkt = build_write_multi(reg0_val, reg1_val)
    ts  = time.strftime("%H:%M:%S")
    msg = (f"[{ts}] [CMD]          ★ WRITE_MULTI reg0=0x{reg0_val:04X} reg1=0x{reg1_val:04X} {label}"
           f"\n        pkt: {pkt.hex()}")
    print(msg)
    if log_file:
        log_file.write(msg + "\n")
        log_file.flush()
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
  set_charge <A>       dòng sạc tối đa    reg 101 (0-140A)
  set_discharge <A>    dòng xả tối đa     reg 102 (0-140A)
  set_eod_soc          quản lý xả → SOC   reg 120 bit4-5=1
  set_eod_volt         quản lý xả → Volt  reg 120 bit4-5=0
  set_lithium          pin → Lithium brand 6  (reg 0 = 0x801A, fn=0x06)
  set_leadacid         pin → Lead Acid brand 6 (reg 0 = 0x8019, fn=0x06)
  get <reg>            đọc cache (dec hoặc 0x...)
  status               tóm tắt kết nối + regs
  help                 danh sách lệnh
""")

    elif op == "set_charge":
        if len(p) < 2: print("Cú pháp: set_charge <A>"); return
        send_cmd(101, int(p[1]), "→ charge rate")

    elif op == "set_discharge":
        if len(p) < 2: print("Cú pháp: set_discharge <A>"); return
        send_cmd(102, int(p[1]), "→ discharge rate")

    elif op == "set_eod_soc":
        with lock: cur = hold_cache.get(120, 0)
        # Set bits 4-5 = 01 (SOC), bit 7 = 1
        new = (cur & ~0xB0) | 0x90  # clear bits[4,5,7], set bit[4,7]
        print(f"    reg120: 0x{cur:02X} → 0x{new:02X} | {decode_sys_enable(new)}")
        send_cmd(120, new, "→ EOD by SOC")

    elif op == "set_eod_volt":
        with lock: cur = hold_cache.get(120, 0)
        new = cur & ~0x30  # clear bits 4-5 → Voltage mode
        print(f"    reg120: 0x{cur:02X} → 0x{new:02X} | {decode_sys_enable(new)}")
        send_cmd(120, new, "→ EOD by Voltage")

    elif op == "set_lithium":
        with lock:
            cur = hold_cache.get(0, None)
        if cur is not None:
            m = unpack_model(cur)
            print(f"    Current: 0x{cur:04X} → {BAT_NAMES.get(m['battery_type'],'?')} brand={LI_NAMES.get(m['lithium_type'],'?')}")
        # 0x801A = Lithium brand 6 (từ captured packets)
        print(f"    → 0x801A = Lithium/CAN-Alpha (brand 6)")
        confirm = input("    Xác nhận? (yes/no): ").strip().lower()
        if confirm == "yes":
            send_cmd_multi(0x801A, 0x0100, "→ Lithium brand6")
        else:
            print("    Hủy")

    elif op == "set_leadacid":
        with lock:
            cur = hold_cache.get(0, None)
        if cur is not None:
            m = unpack_model(cur)
            print(f"    Current: 0x{cur:04X} → {BAT_NAMES.get(m['battery_type'],'?')} brand={LI_NAMES.get(m['lithium_type'],'?')}")
        # 0x8019 = LeadAcid brand 6 (từ captured packets)
        print(f"    → 0x8019 = Lead-Acid (brand 6 preserved)")
        confirm = input("    Xác nhận? (yes/no): ").strip().lower()
        if confirm == "yes":
            send_cmd_multi(0x8019, 0x0100, "→ LeadAcid brand6")
        else:
            print("    Hủy")

    elif op == "get":
        if len(p) < 2: print("Cú pháp: get <reg>"); return
        reg = int(p[1], 0)
        with lock: val = hold_cache.get(reg)
        if val is None:
            print(f"    reg {reg} chưa có trong cache"); return
        info = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))
        print(f"    reg {reg}(0x{reg:02X}) [{info[0]}] = {val} = {val*info[1]:.1f}{info[2]}")
        if reg == 0:
            m = unpack_model(val)
            print(f"      Battery={BAT_NAMES.get(m['battery_type'],'?')} Brand={LI_NAMES.get(m['lithium_type'],'?')}")
            print(f"      measurement={m['measurement']} meterBrand={m['meter_brand']} meterType={m['meter_type']}")
        elif reg == 120:
            print(f"      {decode_sys_enable(val)}")

    elif op == "status":
        with lock:
            connected = dongle_sock is not None
            cache = dict(hold_cache)
        print(f"\n  Dongle: {'✓ Kết nối' if connected else '✗ Chưa kết nối'}"
              f"  | Cache: {len(cache)} regs"
              f"  | Log: {LOG_PATH}")
        for reg in [0, 99, 101, 102, 105, 120]:
            if reg in cache:
                val  = cache[reg]
                info = HOLD_INFO.get(reg, (f"reg_{reg}", 1.0, ""))
                extra = ""
                if reg == 0:
                    m = unpack_model(val)
                    extra = f"  → {BAT_NAMES.get(m['battery_type'],'?')}/{LI_NAMES.get(m['lithium_type'],'?')}"
                elif reg == 120:
                    extra = f"  → {decode_sys_enable(val)}"
                print(f"    [{reg:3d}] {info[0]:14s} = {val:5d} | {val*info[1]:.1f}{info[2]}{extra}")
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
        print(f"[+] Server LuxPower OK")
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
            if line.strip():
                handle(line)
        except (EOFError, KeyboardInterrupt):
            break

def main():
    global log_file
    log_file = open(LOG_PATH, "a", encoding="utf-8")
    log_file.write(f"\n{'='*60}\nSession: {time.strftime('%Y-%m-%d %H:%M:%S')}\n{'='*60}\n")

    print("╔════════════════════════════════════════════════╗")
    print("║  LuxPower Transparent Proxy + Commander        ║")
    print(f"║  Listen  : 0.0.0.0:{LOCAL_PORT}                     ║")
    print(f"║  Forward : {LUX_SERVER_IP}:{LUX_PORT}      ║")
    print(f"║  Log     : {LOG_PATH}")
    print("╚════════════════════════════════════════════════╝")
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