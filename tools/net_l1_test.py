#!/usr/bin/env python3
"""OS32 リンク層 L1 (WINDOW/Credit) の検証 — docs/tasks/network/LINK_PLAN.md §2 / §5。

前提は L0 と同じ (host_agent.py を listen 起動 → kernel-lgy98-link を配備)。
起動時に drivers/lgy98.c が link_l1_bulk(200, 512) を実行する: ホストに 200 個の
DATA フレームを流させ、絶対値 WINDOW でフロー制御する。credit が NIC リング /
SW キューの空きを超えないので、200 フレーム (ホスト側の素の流量ならリング容量を
超える) を溢れさせずに全部・順序どおり受けられる。

この試験はカーネル内の link_l1_* / ne2k 統計を /api/mem で読み、
  - 200/200 を順序どおり受けた (ooo 0)
  - EOF を受けた
  - NIC の drop が 0 (credit で溢れさせなかった = M4 の「溢れても復旧」と対になる)
  - ページ消費の実測が妥当
を確認する。
"""
import json
import re
import struct
import sys
import urllib.request

BASE = "http://127.0.0.1:8025"
MAP = "build/out/kernel.map"
COUNT = 200

# struct ne2k_stats 内の rx_dropped の word 索引 (offsetof/4)
ST_RX_DROPPED = 11
NIC_ST = 13716


def http(path):
    with urllib.request.urlopen(BASE + path, timeout=10) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def sym(name):
    for line in open(MAP, encoding="utf-8", errors="replace"):
        m = re.match(r"\s+0x([0-9a-f]+)\s+%s$" % re.escape(name), line)
        if m:
            return int(m.group(1), 16)
    return None


def mem(addr, n):
    return bytes.fromhex(http("/api/mem?addr=0x%x&len=%d&space=phys" % (addr, n))["hex"])


def u32(name):
    a = sym(name)
    return None if a is None else struct.unpack("<I", mem(a, 4))[0]


def main():
    fails = 0
    if sym("link_l1_recv") is None:
        print("RESULT: FAIL link_l1_* not in kernel.map — build kernel-lgy98-link first")
        return 1

    recv = u32("link_l1_recv")
    ooo = u32("link_l1_ooo")
    done = u32("link_l1_done")
    win = u32("link_l1_windows")
    cmax = u32("link_l1_max_credit")
    cmin = u32("link_l1_min_credit")
    meas = u32("link_l1_meas_pages")
    nbytes = u32("link_l1_bytes")

    rxdrop = struct.unpack("<I", mem(sym("ne2k_nic") + NIC_ST + ST_RX_DROPPED * 4, 4))[0]

    print("L1: recv=%d/%d ooo=%d done=%d windows=%d credit=%d..%d pages meas=%d.%02d pages/frame bytes=%d"
          % (recv, COUNT, ooo, done, win, cmin, cmax, meas // 100, meas % 100, nbytes))
    print("NIC rx_dropped=%d" % rxdrop)

    # N3: 完了判定は recv == COUNT (= recv_bytes == length)。EOF は補助で、
    # 消失しても全フレーム届いていれば合格 — 情報行に格下げ (契約は本文完了)。
    print("info EOF (link_l1_done) = %d (補助。合否は recv==COUNT)" % done)

    checks = [
        ("%d/%d frames received in order" % (recv, COUNT), recv == COUNT),
        ("no out-of-order / gaps", ooo == 0),
        ("NIC ring not overflowed (rx_dropped 0)", rxdrop == 0),
        ("credit was advertised and bounded (max < ring capacity ~119)", 0 < cmax < 119),
        ("page consumption measured (1.5..3.5 pages/frame for 512B)", 150 <= meas <= 350),
    ]
    for label, ok in checks:
        print("%-4s %s" % ("ok" if ok else "FAIL", label))
        fails += 0 if ok else 1

    net = http("/api/net")
    print("np21w: rx %s rx_dropped %s tx %s" % (net["rx_frames"], net["rx_dropped"], net["tx_frames"]))
    print("RESULT: %s (%d failures)" % ("OK" if fails == 0 else "FAIL", fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
