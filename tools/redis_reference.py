#!/usr/bin/env python3
"""The oracle: record what a real Redis replies, byte for byte.

Starts a throwaway redis-server on a free port (no persistence), runs a case
file against it and prints every reply as a C++ string literal, ready to paste
into a test. Needs `redis-server` on PATH (`brew install redis`); no Python
packages.

    python3 tools/redis_reference.py tools/cases/strings.txt
    python3 tools/redis_reference.py tools/cases/parser.txt --version

Case file format (one directive per line):

    # comment
    ## group name        start a new group: FLUSHALL, fresh connection
    SET k "a\\r\\nb"       a command; split like a shell, then Python escapes per argument
    raw: *1\\r\\n:5\\r\\n   raw bytes (Python escapes) on a fresh connection;
                         prints everything the server sends before it closes
                         or goes quiet for 0.3 s
    raw-expr: b"*" + b"1" * 65537
                         raw bytes from a Python bytes expression (long inputs)

With --inc, several case files become one C++ table for a compatibility
suite (server/mini_redis): raw lines and groups marked "(optional)" are left
out, and replies without a defined order are marked as such:

    python3 tools/redis_reference.py --inc tools/cases/{strings,lists}.txt > x.inc

The expected values in the tests were produced by this script; re-run it
after a Redis upgrade and diff.
"""

import argparse
import ast
import codecs
import os
import shlex
import socket
import subprocess
import sys
import tempfile
from pathlib import Path
import time


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_server():
    port = free_port()
    workdir = tempfile.mkdtemp(prefix="redis-oracle-")
    proc = subprocess.Popen(
        ["redis-server", "--port", str(port), "--bind", "127.0.0.1", "--save", "",
         "--appendonly", "no", "--dir", workdir, "--loglevel", "warning"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.1).close()
            return proc, port
        except OSError:
            time.sleep(0.05)
    proc.kill()
    sys.exit("redis-server did not start")


def encode(args):
    out = b"*%d\r\n" % len(args)
    for a in args:
        out += b"$%d\r\n%s\r\n" % (len(a), a)
    return out


class Reader:
    """Reads exactly one RESP2 reply and returns its raw bytes."""

    def __init__(self, sock):
        self.sock, self.buf = sock, b""

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise EOFError
        self.buf += chunk

    def _line(self):
        while b"\r\n" not in self.buf:
            self._fill()
        i = self.buf.index(b"\r\n") + 2
        line, self.buf = self.buf[:i], self.buf[i:]
        return line

    def _exact(self, n):
        while len(self.buf) < n:
            self._fill()
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def reply(self):
        line = self._line()
        t = line[:1]
        if t in (b"+", b"-", b":"):
            return line
        if t == b"$":
            n = int(line[1:-2])
            return line if n < 0 else line + self._exact(n + 2)
        if t == b"*":
            n = int(line[1:-2])
            return line + b"".join(self.reply() for _ in range(max(n, 0)))
        raise ValueError(f"unexpected reply line {line!r}")


def raw_exchange(port, data):
    with socket.create_connection(("127.0.0.1", port)) as s:
        s.sendall(data)
        s.settimeout(0.3)
        out = b""
        try:
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        return out


def cpp(b):
    """A C++ string literal for arbitrary bytes (octal escapes are never ambiguous)."""
    out = []
    for c in b:
        ch = chr(c)
        if ch == "\r":
            out.append("\\r")
        elif ch == "\n":
            out.append("\\n")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 32 <= c < 127:
            out.append(ch)
        else:
            out.append("\\%03o" % c)
    return '"' + "".join(out) + '"'


def short(b, limit=60):
    s = cpp(b)
    return s if len(s) <= limit else s[: limit - 4] + '..."' + f" ({len(b)} bytes)"


def run(path, port):
    sock = None
    reader = None

    def reconnect():
        nonlocal sock, reader
        if sock:
            sock.close()
        sock = socket.create_connection(("127.0.0.1", port))
        reader = Reader(sock)

    reconnect()
    with open(path, encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")
            if not line.strip() or (line.startswith("#") and not line.startswith("##")):
                continue
            if line.startswith("##"):
                reconnect()
                sock.sendall(encode([b"FLUSHALL"]))
                reader.reply()
                print(f"\n// {line[2:].strip()}")
                continue
            if line.startswith("raw:") or line.startswith("raw-expr:"):
                if line.startswith("raw:"):
                    data = ast.literal_eval('b"' + line[4:].strip().replace('"', '\\"') + '"')
                    label = cpp(data)
                else:
                    expr = line[len("raw-expr:"):].strip()
                    data = eval(expr, {"__builtins__": {}})  # our own case files only
                    label = expr
                print(f"//   raw {label}\n    {cpp(raw_exchange(port, data))},")
                continue
            # \r, \n, \xHH, ... inside arguments are escapes, as in Python byte strings.
            args = [codecs.escape_decode(a.encode("utf-8"))[0] for a in shlex.split(line, posix=True)]
            sock.sendall(encode(args))
            reply = reader.reply()
            print("    {{" + ", ".join(short(a) for a in args) + "}, " + cpp(reply) + "},")
    sock.close()


ANY_ORDER = {b"SMEMBERS", b"SINTER", b"SUNION", b"SDIFF", b"HKEYS", b"HVALS", b"KEYS"}
PAIR_ORDER = {b"HGETALL"}


def run_inc(paths, port):
    version = subprocess.run(["redis-server", "--version"], capture_output=True, text=True).stdout.split()[2]
    print(f"// Generated by `python3 tools/redis_reference.py --inc ...` from Redis {version}; don't edit.")
    print("// {group, {{{args}, reply, order}, ...}}")
    print("inline const std::vector<CompatGroup> kCompatGroups = {")
    for path in paths:
        sock, rd, skip = None, None, True
        for raw_line in open(path, encoding="utf-8"):
            line = raw_line.rstrip("\n")
            if not line.strip() or (line.startswith("#") and not line.startswith("##")):
                continue
            if line.startswith("##"):
                if sock is not None and not skip:
                    print("    }},")
                name = line[2:].strip()
                skip = "(optional)" in name
                if sock:
                    sock.close()
                sock = socket.create_connection(("127.0.0.1", port))
                rd = Reader(sock)
                sock.sendall(encode([b"FLUSHALL"]))
                rd.reply()
                if not skip:
                    group = f"{Path(path).stem}: {name}"
                    print(f"    {{{cpp(group.encode())}, {{")
                continue
            if skip or line.startswith("raw"):
                continue
            args = [codecs.escape_decode(a.encode("utf-8"))[0] for a in shlex.split(line, posix=True)]
            sock.sendall(encode(args))
            reply = rd.reply()
            cmd = args[0].upper()
            order = "kAnyPairOrder" if cmd in PAIR_ORDER else "kAnyOrder" if cmd in ANY_ORDER else "kExact"
            lit = lambda b: cpp(b) + ("s" if b"\0" in b else "")  # "..."s keeps embedded NULs
            print("        {{" + ", ".join(lit(a) for a in args) + "}, " + lit(reply) + f", {order}}},")
        if sock is not None and not skip:
            print("    }},")
        if sock:
            sock.close()
    print("};")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--version", action="store_true", help="print the Redis version used")
    ap.add_argument("--inc", action="store_true", help="one C++ table from all case files")
    a = ap.parse_args()
    proc, port = start_server()
    try:
        if a.inc:
            run_inc(a.cases, port)
            return
        if a.version or not a.cases:
            print("// " + subprocess.run(["redis-server", "--version"], capture_output=True,
                                         text=True).stdout.strip())
        for c in a.cases:
            run(c, port)
    finally:
        proc.terminate()
        proc.wait()


if __name__ == "__main__":
    main()
