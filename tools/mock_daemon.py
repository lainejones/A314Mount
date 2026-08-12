#!/usr/bin/env python3
"""
mock_daemon.py - a stand-in for the a314 floppy-emulation control daemon.

Listens like the real one (line-based TCP), prints every command it receives,
and replies "ok". Use it to verify A314Mount sends the right thing WITHOUT
touching real floppy images. Run it on the Pi (or any host the Amiga can reach)
on a spare port, then point A314Mount at that HOST/PORT.

    python3 mock_daemon.py [port]        # default 23899 (NOT the real 23890)

Real protocol it mimics (from Niklas):
    insert <drive> <path>
    insert -rw <drive> <path>
    eject <drive>
"""
import socket, sys, datetime

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 23899

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(4)
print(f"mock a314 disk daemon on :{PORT}  (Ctrl-C to stop)")

try:
    while True:
        conn, addr = srv.accept()
        with conn:
            data = b""
            conn.settimeout(3)
            try:
                while b"\n" not in data:
                    chunk = conn.recv(256)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                pass
            for line in data.decode("latin-1").splitlines():
                line = line.strip()
                if not line:
                    continue
                ts = datetime.datetime.now().strftime("%H:%M:%S")
                print(f"[{ts}] {addr[0]}  ->  {line!r}")
                parts = line.split()
                if parts and parts[0] in ("insert", "eject"):
                    conn.sendall(b"ok\n")
                else:
                    conn.sendall(b"error: unknown command\n")
except KeyboardInterrupt:
    print("\nbye")
