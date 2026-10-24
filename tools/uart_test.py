"""
uart_test.py - Chung minh khong mat byte o 115200 bps (bang chung cho Lab 6)

Cach dung:
    pip install pyserial
    python uart_test.py COM5            (Windows)
    python uart_test.py /dev/ttyACM0    (Linux)
    python uart_test.py COM5 --size 50000 --runs 5

Script gui lenh TEST <n>, cho "OK READY", gui n byte ngau nhien lien tuc
o toc do toi da, roi so CRC32 board tinh voi CRC32 tren PC.
"""
import argparse
import os
import sys
import time
import zlib

import serial

BAUD = 115200


def read_line(ser, timeout_s):
    deadline = time.time() + timeout_s
    buf = b""
    while time.time() < deadline:
        c = ser.read(1)
        if not c:
            continue
        if c == b"\n":
            return buf.decode(errors="replace").strip()
        buf += c
    return None


def wait_prefix(ser, prefixes, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = read_line(ser, deadline - time.time())
        if line is None:
            break
        if line.startswith(prefixes):
            return line
    return None


def run_once(ser, size):
    data = os.urandom(size)
    expected = zlib.crc32(data) & 0xFFFFFFFF

    ser.reset_input_buffer()
    ser.write(f"TEST {size}\r\n".encode())
    if wait_prefix(ser, ("OK READY",), 2.0) is None:
        print("  Board khong tra loi OK READY")
        return False

    t0 = time.time()
    ser.write(data)
    ser.flush()
    send_s = time.time() - t0

    line = wait_prefix(ser, ("OK TEST", "ERR"), 5.0 + size * 10 / BAUD)
    if line is None:
        print("  Khong nhan duoc ket qua")
        return False

    print(f"  Board: {line}")
    fields = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
    board_crc = int(fields.get("crc", "0"), 16)
    ok = (board_crc == expected) and fields.get("n") == str(size)
    rate = size * 10 / send_s if send_s > 0 else 0
    print(f"  PC   : crc=0x{expected:08X}  thoi gian gui={send_s*1000:.0f} ms"
          f"  (~{rate:.0f} bps hieu dung)")
    print("  => KHOP, khong mat byte" if ok else "  => KHONG KHOP")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--size", type=int, default=10000)
    ap.add_argument("--runs", type=int, default=3)
    args = ap.parse_args()

    with serial.Serial(args.port, BAUD, timeout=0.1) as ser:
        time.sleep(0.2)
        passed = 0
        for i in range(args.runs):
            print(f"Lan {i + 1}/{args.runs}: gui {args.size} byte")
            if run_once(ser, args.size):
                passed += 1
        print(f"\nKet qua: {passed}/{args.runs} lan khop")
        sys.exit(0 if passed == args.runs else 1)


if __name__ == "__main__":
    main()
