#!/usr/bin/env python3
"""PC side of the network loads (copied from steam_bench_firmware/host).

sink   TCP sink for E1. Counts the bytes of each connection, checks the
       board's byte pattern, and when the board half-closes the socket replies
       with the received byte count ("<n>\\n"). The board reports that
       confirmed count as the Ethernet payload.
blast  TCP sender for the E5 Wi-Fi row: join the board's soft-AP
       (SSID STEAM-BENCH, password steambench) and stream to 192.168.4.1:5001.

Examples:
  python netload.py sink --port 5001
  python netload.py blast --host 192.168.4.1 --seconds 3600
"""
import argparse
import socket
import threading
import time

CHUNK = 8192
PATTERN = bytes((i * 31 + 7) & 0xFF for i in range(CHUNK))


def handle(conn, peer, log):
    start = time.perf_counter()
    received = 0
    bad = 0
    with conn:
        while True:
            data = conn.recv(65536)
            if not data:
                break
            offset = received % CHUNK
            view = memoryview(data)
            pos = 0
            while pos < len(view):
                take = min(CHUNK - offset, len(view) - pos)
                if view[pos:pos + take] != PATTERN[offset:offset + take]:
                    bad += 1
                pos += take
                offset = 0
            received += len(data)
        elapsed = time.perf_counter() - start
        try:
            conn.sendall(f"{received}\n".encode())
        except OSError:
            pass
    rate = received / elapsed / 1e6 if elapsed > 0 else 0.0
    line = (f"{time.strftime('%H:%M:%S')} {peer[0]}:{peer[1]} bytes={received} "
            f"seconds={elapsed:.3f} MB/s={rate:.3f} pattern_mismatch_segments={bad}")
    print(line, flush=True)
    if log:
        with open(log, "a") as handle_:
            handle_.write(line + "\n")


def sink(args):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    server.bind(("0.0.0.0", args.port))
    server.listen(4)
    print(f"sink listening on TCP {args.port}; Ctrl+C to stop", flush=True)
    while True:
        conn, peer = server.accept()
        threading.Thread(target=handle, args=(conn, peer, args.log), daemon=True).start()


def blast(args):
    payload = PATTERN * 8
    sock = socket.create_connection((args.host, args.port), timeout=10)
    end = time.time() + args.seconds
    sent = 0
    last = time.time()
    while time.time() < end:
        sent += sock.send(payload)
        if time.time() - last >= 10:
            print(f"sent {sent / 1e6:.1f} MB", flush=True)
            last = time.time()
    sock.close()
    print(f"done, {sent} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("sink")
    p.add_argument("--port", type=int, default=5001)
    p.add_argument("--log", help="append one line per connection to this file")
    p = sub.add_parser("blast")
    p.add_argument("--host", default="192.168.4.1")
    p.add_argument("--port", type=int, default=5001)
    p.add_argument("--seconds", type=float, default=3600)
    args = parser.parse_args()
    sink(args) if args.cmd == "sink" else blast(args)


if __name__ == "__main__":
    main()
