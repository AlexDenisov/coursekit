#!/usr/bin/env python3
"""Oracle for ds/intset and ds/listpack: the exact bytes Redis builds.

`DUMP key` returns the key's value in RDB format. Small sets of integers are
stored as an intset, small hashes and list nodes as a listpack, and the RDB
payload embeds those blobs as-is. This script runs some commands, DUMPs the
key, digs the blob out and prints it as a C++ literal:

    python3 tools/blob_reference.py

(redis_reference.py starts the server with rdbcompression off, so blobs aren't
LZF-compressed.)
"""

import shlex
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import redis_reference as ref  # noqa: E402

RDB_SET_INTSET, RDB_HASH_LISTPACK, RDB_LIST_QUICKLIST_2, RDB_SET_LISTPACK = 11, 16, 18, 20


def read_len(b, i):
    t = b[i] >> 6
    if t == 0:
        return b[i] & 0x3F, i + 1
    if t == 1:
        return ((b[i] & 0x3F) << 8) | b[i + 1], i + 2
    if b[i] == 0x80:
        return int.from_bytes(b[i + 1:i + 5], "big"), i + 5
    if b[i] == 0x81:
        return int.from_bytes(b[i + 1:i + 9], "big"), i + 9
    raise ValueError(f"special encoding 0x{b[i]:02x} (compressed?)")


def read_string(b, i):
    n, i = read_len(b, i)
    return b[i:i + n], i + n


def blob(payload):
    t = payload[0]
    if t in (RDB_SET_INTSET, RDB_HASH_LISTPACK, RDB_SET_LISTPACK):
        return read_string(payload, 1)[0]
    if t == RDB_LIST_QUICKLIST_2:
        nodes, i = read_len(payload, 1)
        assert nodes == 1, "keep test lists to one node"
        container, i = read_len(payload, i)
        assert container == 2, "expected a packed node"
        return read_string(payload, i)[0]
    raise ValueError(f"unexpected RDB type {t}")


CASES = [
    # (label, commands...)  the DUMPed key is always "k"
    ("intset int16", ["SADD k 5 1 3 -2"]),
    ("intset int32", ["SADD k 1 70000 -2"]),
    ("intset int64", ["SADD k 1 5000000000 -40000"]),
    ("intset min max", ["SADD k -9223372036854775808 9223372036854775807 0"]),
    ("listpack hash small", ["HSET k f v"]),
    ("listpack hash ints", ["HSET k a 0 b 127 c 128 d -1 e 4095 f -4096 g 4096"]),
    ("listpack hash wide ints", ["HSET k a 32767 b -32768 c 32768 d 8388607 e -8388609 f 2147483648 g -9223372036854775808"]),
    ("listpack hash int-like strings", ["HSET k a 01 b -0 c +1 d 1.5 e 9223372036854775808"]),
    ("listpack list strings", ["RPUSH k a " + "x" * 63 + " " + "y" * 64 + " ''"]),
    ("listpack list 4095-byte string", ["RPUSH k " + "z" * 4095]),
    ("listpack list 4096-byte string", ["RPUSH k " + "w" * 4096]),
    ("listpack list backlen boundary", ["RPUSH k " + "b" * 125 + " " + "c" * 126 + " " + "d" * 300]),
    ("listpack set", ["SADD k a b c"]),
]


def main():
    proc, port = ref.start_server()
    try:
        s = socket.create_connection(("127.0.0.1", port))
        rd = ref.Reader(s)
        for label, cmds in CASES:
            s.sendall(ref.encode([b"FLUSHALL"]))
            rd.reply()
            for c in cmds:
                s.sendall(ref.encode([a.encode() for a in shlex.split(c)]))
                rd.reply()
            s.sendall(ref.encode([b"OBJECT", b"ENCODING", b"k"]))
            enc = rd.reply()
            s.sendall(ref.encode([b"DUMP", b"k"]))
            raw = rd.reply()
            payload = raw[raw.index(b"\r\n") + 2:-2]
            b = blob(payload)
            if len(b) <= 300:
                shown = ref.cpp(b)
            else:  # too long to paste: size, both ends and a hash (FNV-1a 64)
                h = 0xcbf29ce484222325
                for x in b:
                    h = ((h ^ x) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
                shown = f"BigBlob{{{len(b)}, {ref.cpp(b[:16])}, {ref.cpp(b[-16:])}, 0x{h:016x}ull}}"
            encoding = enc.split(b"\r\n")[1].decode()
            print(f"// {label}: {cmds[0][:70]}  (OBJECT ENCODING {encoding})")
            print(f"    {shown},")
    finally:
        proc.terminate()


if __name__ == "__main__":
    main()
