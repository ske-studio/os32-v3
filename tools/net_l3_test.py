#!/usr/bin/env python3
"""OS32 リンク層 L3 (Host Services) の検証 — docs/tasks/network/LINK_PLAN.md §1 / §5。

前提は L0〜L2 と同じ (host_agent.py 起動 → kernel-lgy98-link 配備)。起動時に
drivers/lgy98.c が link_l3_service() を実行する:
  1. GET /pattern/65536  — ホストが本文を生成、OS32 はストリームで受けて内容検証
  2. GET /notfound       — status 404 の要求応答
  3. GET http://example.com/ — ホストが実 HTTP を取得 (オンラインなら 200)。付録
  4. TIME                — 本文のない RPC (ホストの時刻文字列)
OS32 は TCP/IP も HTTP も持たず、要求を出して結果だけ受け取る。

この試験は link_l3_* を /api/mem で読み、決定的な部分 (pattern GET / 404 / TIME) を
合否とし、実 HTTP はオンライン状況として報告する。
"""
import json
import re
import struct
import sys
import urllib.request

BASE = "http://127.0.0.1:8025"
MAP = "build/out/kernel.map"
GET_LEN = 65536


def http(path):
    with urllib.request.urlopen(BASE + path, timeout=10) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def sym(name):
    for line in open(MAP, encoding="utf-8", errors="replace"):
        m = re.match(r"\s+0x([0-9a-f]+)\s+%s$" % re.escape(name), line)
        if m:
            return int(m.group(1), 16)
    return None


def u32(name):
    a = sym(name)
    if a is None:
        return None
    h = http("/api/mem?addr=0x%x&len=4&space=phys" % a)["hex"]
    return struct.unpack("<I", bytes.fromhex(h))[0]


def main():
    fails = 0
    if sym("link_l3_get_status") is None:
        print("RESULT: FAIL link_l3_* not in kernel.map — build kernel-lgy98-link first")
        return 1

    gs = u32("link_l3_get_status")
    gr = u32("link_l3_get_read")
    gl = u32("link_l3_get_len")
    gb = u32("link_l3_get_bad")
    nf = u32("link_l3_404")
    hs = u32("link_l3_http_status")
    hr = u32("link_l3_http_read")
    tl = u32("link_l3_time_len")

    print("L3: GET /pattern -> status=%d read=%d/%d bad=%d" % (gs, gr, gl, gb))
    print("    GET /notfound -> status=%d" % nf)
    print("    GET http://example.com/ -> status=%d read=%d %s"
          % (hs, hr, "(online)" if hs == 200 else "(offline/blocked — informational)"))
    print("    TIME RPC -> %d bytes" % tl)

    checks = [
        ("GET /pattern returned 200", gs == 200),
        ("streamed %d/%d bytes" % (gr, GET_LEN), gr == GET_LEN and gl == GET_LEN),
        ("content intact (0 mismatched)", gb == 0),
        ("GET /notfound returned 404", nf == 404),
        ("TIME RPC answered (len>0)", tl > 0),
    ]
    for label, ok in checks:
        print("%-4s %s" % ("ok" if ok else "FAIL", label))
        fails += 0 if ok else 1

    if hs == 200 and hr > 0:
        print("ok   real HTTP GET fetched %d bytes via the host (online)" % hr)
    else:
        print("--   real HTTP GET not online (status %d) — host-side, not a driver/link failure" % hs)

    print("RESULT: %s (%d failures)" % ("OK" if fails == 0 else "FAIL", fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
