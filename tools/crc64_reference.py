#!/usr/bin/env python3
"""Oracle for persist/crc64: Redis's CRC-64 (Jones polynomial, reflected, init 0).

Two independent sources:
  1. a bit-by-bit implementation of the definition, here, in Python;
  2. real Redis: every DUMP payload ends with a little-endian CRC-64 of the bytes
     before it, so DUMP output is a set of test vectors computed by Redis itself.

    python3 tools/crc64_reference.py
"""

import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import redis_reference as ref  # noqa: E402

POLY_REFLECTED = 0x95AC9329AC4BC9B5  # 0xad93d23594c935a9, bit-reversed


def crc64(data, crc=0):
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ POLY_REFLECTED if crc & 1 else crc >> 1
    return crc


def main():
    assert crc64(b"123456789") == 0xE9C6D914C4B8D9CA  # the published check value
    print("// crc64 of strings, from the bit-by-bit definition")
    for s in [b"", b"a", b"123456789", b"hello world", bytes(range(256)), b"\x00" * 1000]:
        print(f"    {{{ref.cpp(s) if len(s) < 40 else repr(len(s))}, 0x{crc64(s):016x}ull}},")
    proc, port = ref.start_server()
    try:
        sock = socket.create_connection(("127.0.0.1", port))
        rd = ref.Reader(sock)
        print("// DUMP payloads from Redis (the last 8 bytes are the CRC-64 of the rest)")
        for args in [[b"SET", b"k", b"hello"], [b"SET", b"n", b"12345"],
                     [b"RPUSH", b"l", b"a", b"b", b"c"], [b"HSET", b"h", b"f", b"v"]]:
            sock.sendall(ref.encode(args)); rd.reply()
            sock.sendall(ref.encode([b"DUMP", args[1]]))
            raw = rd.reply()
            payload = raw[raw.index(b"\r\n") + 2:-2]
            body, crc = payload[:-8], int.from_bytes(payload[-8:], "little")
            assert crc64(body) == crc, "Redis and the definition disagree"
            print(f"    {{{ref.cpp(body)}, 0x{crc:016x}ull}},  // DUMP after {b' '.join(args).decode()}")
    finally:
        proc.terminate()


if __name__ == "__main__":
    main()
