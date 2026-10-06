#!/usr/bin/env python3
"""Receive TVDM via NP21/W HTTP: tvdump_recv.py [--raw response.bin]."""
import argparse
from pathlib import Path
import sys
from np21w_mcp import np21w_client as emu

TVDM_MAGIC = b"TVDM"
TVDM_COLS = 80
TVDM_ROWS = 25
TVDM_HEADER_BYTES = 6

# PC-98テキストVRAMアトリビュート → ANSIカラー変換
# PC-98 attr: bit 2-0 = color (GRB), bit 3 = 未使用, etc.
# アトリビュート構造: bit 7-5: 色(上位), bit 4: アンダーライン, etc.
# 実際のPC-98: 0xE1=白, 0xA1=シアン, 0xC1=黄, 0x81=緑, 0x41=赤

def pc98_attr_to_ansi(attr):
    """PC-98テキストアトリビュート→ANSIエスケープシーケンス"""
    # PC-98のカラーコード（上位3ビット）
    # C1=赤(bit6), A1=シアン(bit5+7), 81=緑(bit7), E1=白(bit7+6+5)
    color_bits = (attr >> 5) & 0x07

    ansi_map = {
        0: '30',   # 000 = 黒
        1: '34',   # 001 = 青
        2: '31',   # 010 = 赤
        3: '35',   # 011 = 紫
        4: '32',   # 100 = 緑
        5: '36',   # 101 = シアン
        6: '33',   # 110 = 黄
        7: '37',   # 111 = 白
    }
    return f"\033[{ansi_map.get(color_bits, '37')}m"

def receive_tvdump():
    """Preserve binary bytes (including control characters) from /api/cmd."""
    return emu.post("/api/cmd", b"tvdump", timeout=20)


def parse_tvdump(raw):
    """Validate the complete TVDM frame; shell echo/trailer may surround it."""
    start = raw.find(TVDM_MAGIC)
    if start < 0:
        raise ValueError("TVDM header missing")
    if len(raw) - start < TVDM_HEADER_BYTES:
        raise ValueError("TVDM header truncated")
    cols, rows = raw[start + 4:start + TVDM_HEADER_BYTES]
    if (cols, rows) != (TVDM_COLS, TVDM_ROWS):
        raise ValueError("TVDM dimensions must be 80x25")
    end = start + TVDM_HEADER_BYTES + cols * rows * 2
    if len(raw) < end:
        raise ValueError("TVDM cell data truncated")
    return cols, rows, raw[start + TVDM_HEADER_BYTES:end]


def parse_and_display(raw):
    cols, rows, cells = parse_tvdump(raw)
    print(f"テキストVRAMダンプ: {cols}×{rows}")
    print("=" * cols)

    # テキストデータ表示
    for row in range(rows):
        line = ""
        prev_attr = -1
        for col in range(cols):
            idx = (row * cols + col) * 2
            ch = cells[idx]
            at = cells[idx + 1]

            # ANSIカラー
            if at != prev_attr:
                line += pc98_attr_to_ansi(at)
                prev_attr = at

            # 表示可能文字のみ
            if 0x20 <= ch < 0x7F:
                line += chr(ch)
            else:
                line += ' '

        line += "\033[0m"  # リセット
        print(line.rstrip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", type=Path, help="save the complete HTTP response")
    args = parser.parse_args()
    try:
        raw = receive_tvdump()
        if args.raw:
            args.raw.write_bytes(raw)
        parse_and_display(raw)
    except (ValueError, OSError, emu.EmuError) as exc:
        print("tvdump: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
