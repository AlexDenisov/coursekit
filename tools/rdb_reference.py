#!/usr/bin/env python3
"""Oracle for persist/rdb_reader: real dump.rdb files written by Redis.

Builds a fixed dataset in a throwaway redis-server (one key per RDB type Redis
8 uses: plain and int-encoded strings, listpack and hashtable hashes, intset /
listpack / hashtable sets, listpack and skiplist sorted sets, quicklist lists),
runs SAVE twice (rdbcompression off and on) and copies the files into
persist/rdb_reader/testdata/. The tests know the dataset from DATASET below.

    python3 tools/rdb_reference.py
"""

import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import redis_reference as ref  # noqa: E402

OUT = Path(__file__).parent.parent / "persist" / "rdb_reader" / "testdata"

DATASET = [
    "SET str hello",
    "SET int 12345",
    "SET neg -1",
    "SET big 1099511627776",
    "SET long " + "L" * 1000,
    "SET empty ''",
    "RPUSH list a b c 1 2 3",
    "RPUSH biglist " + " ".join("item%04d" % i for i in range(2000)),
    "HSET hash f1 v1 f2 2",
    "HSET bighash " + " ".join(f"f{i} v{i}" for i in range(600)),
    "SADD intset 3 1 2 -5",
    "SADD lpset x y z",
    "SADD bigset " + " ".join(f"m{i}" for i in range(200)),
    "ZADD zset 1 a 2.5 b -3 c",
    "ZADD bigzset " + " ".join(f"{i}.5 z{i}" for i in range(200)),
    "SET forever v",
    "EXPIREAT forever 4102444800",          # 2100-01-01
    "SET soon v",
    "PEXPIREAT soon 2524608000000",          # 2050-01-01
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for compression, name in (("no", "redis8_plain.rdb"), ("yes", "redis8_lzf.rdb")):
        port = ref.free_port()
        workdir = tempfile.mkdtemp(prefix="rdb-oracle-")
        proc = subprocess.Popen(["redis-server", "--port", str(port), "--save", "", "--appendonly", "no",
                                 "--dir", workdir, "--rdbcompression", compression, "--loglevel", "warning"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:
                    s = socket.create_connection(("127.0.0.1", port))
                    break
                except OSError:
                    time.sleep(0.05)
            rd = ref.Reader(s)
            for cmd in DATASET + ["SAVE"]:
                s.sendall(ref.encode([a.encode() for a in shlex.split(cmd)]))
                r = rd.reply()
                assert not r.startswith(b"-"), (cmd[:40], r)
            shutil.copy(Path(workdir) / "dump.rdb", OUT / name)
            print(f"wrote {OUT / name} ({(OUT / name).stat().st_size} bytes)")
        finally:
            proc.terminate()
            proc.wait()


if __name__ == "__main__":
    main()
