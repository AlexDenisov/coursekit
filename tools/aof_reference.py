#!/usr/bin/env python3
"""Oracle for persist/aof: what Redis writes to its append-only file.

Runs commands on a throwaway redis-server with appendonly yes and prints the
resulting incremental AOF file (Redis 7+ keeps a base file plus incremental
files in appendonlydir/), showing how commands are logged, including how
relative TTLs become absolute ones.

    python3 tools/aof_reference.py
"""

import glob
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import redis_reference as ref  # noqa: E402

COMMANDS = [
    "SET k v",
    "GET k",
    "SET t v EX 100",
    "SET p v PX 5000",
    "EXPIRE k 100",
    "PEXPIRE k 5000",
    "EXPIREAT k 4102444800",
    "INCR n",
    "DEL nothere",
    "DEL n",
    "RPUSH l a b",
    "SET k v KEEPTTL",
]


def main():
    port = ref.free_port()
    workdir = tempfile.mkdtemp(prefix="aof-oracle-")
    proc = subprocess.Popen(["redis-server", "--port", str(port), "--save", "", "--appendonly", "yes",
                             "--appendfsync", "always", "--dir", workdir, "--loglevel", "warning"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                s = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                time.sleep(0.05)
        rd = ref.Reader(s)
        t0 = int(time.time() * 1000)
        for c in COMMANDS:
            s.sendall(ref.encode([a.encode() for a in shlex.split(c)]))
            rd.reply()
        incr = sorted(glob.glob(workdir + "/appendonlydir/*.incr.aof"))[-1]
        data = open(incr, "rb").read()
        print(f"// commands sent at about unix ms {t0}")
        for c in COMMANDS:
            print(f"//   {c}")
        print("// were logged as:")
        # one request per line, readable
        i = 0
        while i < len(data):
            n_end = data.index(b"\r\n", i)
            n = int(data[i + 1:n_end])
            j = n_end + 2
            args = []
            for _ in range(n):
                l_end = data.index(b"\r\n", j)
                ln = int(data[j + 1:l_end])
                args.append(data[l_end + 2:l_end + 2 + ln].decode())
                j = l_end + 2 + ln + 2
            print("//   " + " ".join(args))
            i = j
    finally:
        proc.terminate()


if __name__ == "__main__":
    main()
