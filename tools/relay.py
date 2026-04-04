"""
LuxPower Pure Transparent Proxy
================================
:4346  real dongle connects here  →  cloud 47.81.11.236:4346
:8000  ESPHome connects here      →  real dongle 192.168.100.22:8000

Pure relay — no decoding, no caching, just forward + hexdump log.
"""

import socket, threading, time, os

# ── Config ────────────────────────────────────────────────────
CLOUD_IP    = "47.81.11.236"
CLOUD_PORT  = 4346

DONGLE_IP   = "192.168.100.22"
DONGLE_PORT = 8000

LISTEN_4346 = 4346
LISTEN_8000 = 8000

LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lux_pure_proxy.log")
log_lock = threading.Lock()

# ── Log ───────────────────────────────────────────────────────
def log(msg):
    ts  = time.strftime("%H:%M:%S")
    out = f"[{ts}] {msg}"
    with log_lock:
        print(out)
        try:
            with open(LOG_PATH, "a", encoding="utf-8") as f:
                f.write(out + "\n")
        except Exception:
            pass

def hexdump(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        chunk    = data[i:i+16]
        hex_part = " ".join(f"{b:02X}" for b in chunk)
        asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{indent}{i:04X}  {hex_part:<47s}  {asc_part}")
    return "\n".join(lines)

# ── Pure transparent pipe: src → dst, log every chunk ─────────
def pipe(src, dst, label):
    try:
        while True:
            data = src.recv(4096)
            if not data:
                break
            dst.sendall(data)
            log(f"{label}  {len(data)}B\n{hexdump(data)}")
    except Exception:
        pass
    finally:
        try: src.close()
        except: pass
        try: dst.close()
        except: pass

# ── Generic relay: connect client to target ───────────────────
def relay(client_sock, client_tag, target_ip, target_port, label_fwd, label_rev):
    log(f"{'━'*55}")
    log(f"Client  connected   {client_tag}")
    try:
        target_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        target_sock.settimeout(15)
        target_sock.connect((target_ip, target_port))
        target_sock.settimeout(None)
        log(f"Target  connected   {target_ip}:{target_port}")
    except Exception as e:
        log(f"Target  UNREACHABLE {target_ip}:{target_port}  →  {e}")
        client_sock.close()
        return

    t1 = threading.Thread(target=pipe,
                          args=(client_sock, target_sock, label_fwd), daemon=True)
    t2 = threading.Thread(target=pipe,
                          args=(target_sock, client_sock, label_rev), daemon=True)
    t1.start(); t2.start()
    t1.join();  t2.join()
    log(f"Session ended       {client_tag}")
    log(f"{'━'*55}")

# ── Listener ──────────────────────────────────────────────────
def listen(port, target_ip, target_port, label_fwd, label_rev, tag):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(5)
    log(f"{tag} listening on :{port}  →  {target_ip}:{target_port}")
    while True:
        try:
            sock, addr = srv.accept()
            client_tag = f"{addr[0]}:{addr[1]}"
            threading.Thread(
                target=relay,
                args=(sock, client_tag, target_ip, target_port, label_fwd, label_rev),
                daemon=True
            ).start()
        except Exception as e:
            log(f"{tag} accept error: {e}")

# ── Main ──────────────────────────────────────────────────────
def main():
    with open(LOG_PATH, "a", encoding="utf-8") as f:
        sep = "=" * 60
        f.write(f"\n{sep}\nSession: {time.strftime('%Y-%m-%d %H:%M:%S')}\n{sep}\n")

    print("╔══════════════════════════════════════════════════════╗")
    print("║  LuxPower Pure Transparent Proxy                     ║")
    print(f"║  :4346  dongle   →  cloud  {CLOUD_IP}:{CLOUD_PORT}  ║")
    print(f"║  :8000  ESPHome  →  dongle {DONGLE_IP}:{DONGLE_PORT}        ║")
    print(f"║  Log: {LOG_PATH}")
    print("╚══════════════════════════════════════════════════════╝\n")

    # Port 4346: real dongle → cloud
    threading.Thread(
        target=listen,
        args=(LISTEN_4346, CLOUD_IP, CLOUD_PORT,
              "D→Cloud", "Cloud→D", "[4346]"),
        daemon=True
    ).start()

    # Port 8000: ESPHome → real dongle
    threading.Thread(
        target=listen,
        args=(LISTEN_8000, DONGLE_IP, DONGLE_PORT,
              "ESP→Dongle", "Dongle→ESP", "[8000]"),
        daemon=True
    ).start()

    print(f"  Dongle  Server Address  →  this machine : 4346")
    print(f"  ESPHome host            →  this machine : 8000")
    print(f"  Ctrl+C to stop\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopped.")

if __name__ == "__main__":
    main()