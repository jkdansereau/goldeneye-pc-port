"""Minimal nxlink (devkitPro switch-tools src/nxlink.c protocol) for Windows.

    python nxlink.py <switch-ip> <file.nro> <sd-path> <log-out> [args...]

Uploads <file.nro> to the homebrew menu's netloader (Y), at <sd-path>, with the
given arguments, then acts as the stdio server (`nxlink -s`): everything the
program writes after nxlinkStdio() is appended to <log-out>.
"""
import os, socket, struct, sys, time, zlib

SERVER_PORT, CLIENT_PORT, CHUNK = 28280, 28771, 16 * 1024
# Connection attempts, 3 s apart, before giving up with "no netloader"
# (NXLINK_RETRIES; a test loop sets it low so a dead launcher shows up fast).
RETRIES = int(os.environ.get("NXLINK_RETRIES", "100"))


def send_nro(ip, path, sdpath, args):
    data = open(path, "rb").read()
    for attempt in range(RETRIES):
        try:
            s = socket.create_connection((ip, SERVER_PORT), timeout=5)
            break
        except OSError as e:
            print(f"connect {ip}:{SERVER_PORT} failed ({e}); netloader open (hbmenu: Y)? retrying", flush=True)
            time.sleep(3)
    else:
        sys.exit("no netloader")
    s.settimeout(60)
    name = sdpath.encode()
    s.sendall(struct.pack("<i", len(name)) + name + struct.pack("<i", len(data)))
    (resp,) = struct.unpack("<i", s.recv(4))
    if resp != 0:
        sys.exit(f"netloader refused the file ({resp})")
    comp = zlib.compressobj()
    for i in range(0, len(data), CHUNK):
        out = comp.compress(data[i:i + CHUNK])
        if out:
            s.sendall(struct.pack("<i", len(out)) + out)
    out = comp.flush()
    for i in range(0, len(out), CHUNK):
        part = out[i:i + CHUNK]
        s.sendall(struct.pack("<i", len(part)) + part)
    (resp,) = struct.unpack("<i", s.recv(4))
    cmd = b"".join(a.encode() + b"\0" for a in args)
    s.sendall(struct.pack("<i", len(cmd)) + cmd)
    s.shutdown(socket.SHUT_WR)
    s.close()
    print(f"sent {path} -> {sdpath} ({len(data)} bytes), args {args}", flush=True)


def listen():
    # Listening before the upload: the program connects as soon as it starts.
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", CLIENT_PORT))
    srv.listen(1)
    srv.settimeout(120)
    return srv


def serve(srv, logpath):
    print("waiting for the program's stdio on port", CLIENT_PORT, flush=True)
    conn, addr = srv.accept()
    print("stdio connected from", addr[0], flush=True)
    conn.settimeout(None)
    with open(logpath, "ab") as f:
        while True:
            buf = conn.recv(65536)
            if not buf:
                break
            f.write(buf)
            f.flush()
    print("stdio closed", flush=True)


if __name__ == "__main__":
    ip, nro, sdpath, logpath, *args = sys.argv[1:]
    srv = listen()
    # libnx's nxlinkSetup() wants the LAST argument to be "<8 hex digits of this
    # PC's IPv4 as in_addr>_NXLINK_" (hbmenu's netloader adds it; sphaira may
    # not), otherwise nxlinkConnectToHost() has no host to connect to.
    _u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    _u.connect((ip, SERVER_PORT))
    mine = _u.getsockname()[0]
    _u.close()
    s_addr = struct.unpack("<I", socket.inet_aton(mine))[0]
    args.append("%08x_NXLINK_" % s_addr)
    print("stdio host", mine, "->", args[-1], flush=True)
    send_nro(ip, nro, sdpath, args)
    serve(srv, logpath)
