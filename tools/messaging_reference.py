#!/usr/bin/env python3
"""Oracle for the Messaging track: multi-client exchanges with a real Redis.

Each scenario is a list of steps `(client, command)`: the command is sent on
that client's connection, then every connection is drained and whatever each
received is printed. That shows pub/sub messages, BLPOP wake-ups and
transaction replies exactly as Redis sends them.

    python3 tools/messaging_reference.py
"""

import shlex
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import redis_reference as ref  # noqa: E402

SCENARIOS = {
    "pubsub basics": [
        ("A", "SUBSCRIBE news sports"),
        ("B", "PUBLISH news hello"),
        ("B", "PUBLISH weather sunny"),
        ("A", "PING"),
        ("A", "PING hi"),
        ("A", "GET k"),
        ("A", "UNSUBSCRIBE news"),
        ("A", "UNSUBSCRIBE"),
        ("A", "UNSUBSCRIBE"),
        ("A", "GET k"),
    ],
    "pubsub patterns": [
        ("A", "PSUBSCRIBE news.*"),
        ("C", "SUBSCRIBE news.tech"),
        ("B", "PUBLISH news.tech fast"),
        ("A", "SUBSCRIBE news.tech"),
        ("B", "PUBLISH news.tech twice"),
        ("A", "PUNSUBSCRIBE news.*"),
        ("A", "PUNSUBSCRIBE"),
        ("B", "PUBSUB CHANNELS"),
        ("B", "PUBSUB NUMSUB news.tech nothing"),
        ("B", "PUBSUB NUMPAT"),
    ],
    "transactions": [
        ("A", "MULTI"),
        ("A", "SET k 1"),
        ("A", "INCR k"),
        ("A", "GET k"),
        ("A", "EXEC"),
        ("A", "EXEC"),
        ("A", "DISCARD"),
        ("A", "MULTI"),
        ("A", "MULTI"),
        ("A", "DISCARD"),
    ],
    "transaction errors": [
        ("A", "MULTI"),
        ("A", "SET k v"),
        ("A", "NOSUCHCMD"),
        ("A", "GET"),
        ("A", "EXEC"),
        ("A", "GET k"),
        ("A", "MULTI"),
        ("A", "SET s v"),
        ("A", "INCR s"),
        ("A", "SET t w"),
        ("A", "EXEC"),
        ("A", "GET t"),
    ],
    "watch": [
        ("A", "SET k 1"),
        ("A", "WATCH k"),
        ("B", "SET k 2"),
        ("A", "MULTI"),
        ("A", "SET k 3"),
        ("A", "EXEC"),
        ("A", "GET k"),
        ("A", "WATCH k"),
        ("A", "MULTI"),
        ("A", "INCR k"),
        ("A", "EXEC"),
        ("A", "WATCH k"),
        ("A", "UNWATCH"),
        ("B", "SET k 9"),
        ("A", "MULTI"),
        ("A", "GET k"),
        ("A", "EXEC"),
        ("A", "MULTI"),
        ("A", "WATCH k"),
        ("A", "DISCARD"),
    ],
    "blocking pops": [
        ("A", "BLPOP q1 q2 0"),
        ("B", "RPUSH q2 x y"),
        ("B", "LLEN q2"),
        ("A", "BRPOP q1 0.1"),
        ("A", "BLPOP q1 abc"),
        ("A", "BLPOP q1 -1"),
        ("B", "RPUSH q1 a"),
        ("A", "BLPOP q1 q2 0"),
        ("A", "BRPOP q2 0"),
    ],
    "blocking fifo": [
        ("A", "BLPOP q 0"),
        ("C", "BLPOP q 0"),
        ("B", "RPUSH q 1 2 3"),
        ("B", "LRANGE q 0 -1"),
    ],
    "blocking inside multi": [
        ("A", "MULTI"),
        ("A", "BLPOP empty 0"),
        ("A", "EXEC"),
    ],
}


def main():
    proc, port = ref.start_server()
    try:
        for name, steps in SCENARIOS.items():
            conns = {}
            for c in sorted({c for c, _ in steps}):
                s = socket.create_connection(("127.0.0.1", port))
                s.settimeout(0.15)
                conns[c] = s
            conns[sorted(conns)[0]].sendall(ref.encode([b"FLUSHALL"]))
            time.sleep(0.05)
            for s in conns.values():
                try:
                    s.recv(65536)
                except socket.timeout:
                    pass
            print(f"\n// {name}")
            for client, cmd in steps:
                conns[client].sendall(ref.encode([a.encode() for a in shlex.split(cmd)]))
                time.sleep(0.2 if "0.1" in cmd else 0.05)
                got = []
                for c, s in sorted(conns.items()):
                    data = b""
                    try:
                        while True:
                            chunk = s.recv(65536)
                            if not chunk:
                                break
                            data += chunk
                    except socket.timeout:
                        pass
                    if data:
                        got.append(f"{c} <- {ref.cpp(data)}")
                print(f"//   {client}: {cmd}")
                for g in got:
                    print(f"//        {g}")
            for s in conns.values():
                s.close()
    finally:
        proc.terminate()


if __name__ == "__main__":
    main()
