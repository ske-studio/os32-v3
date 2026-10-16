#!/usr/bin/env python3
"""Receive TVD2 via NP21/W HTTP or --port SERIAL [--raw response.bin]."""
import argparse
from pathlib import Path
import sys
from np21w_mcp import np21w_client as emu

TVDM_MAGIC = b"TVD2"
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

def receive_tvdump(port=None, baud=9600):
    """Use identical binary frames on HTTP and an already active rshell."""
    if port is None:
        return emu.post("/api/cmd", b"tvdump", timeout=20)
    import rshell_serial as serial
    connection = serial.open_port(port, baud)
    try:
        connection.reset_input_buffer()
        connection.write(b"tvdump\n")
        connection.flush()
        raw, complete = serial.read_until_eot(connection, 20)
        if not complete:
            raise ValueError("timeout waiting for tvdump EOT")
        if not serial.check_echo(raw.decode("utf-8", errors="replace"), "tvdump"):
            raise ValueError("tvdump command echo missing")
        return raw
    finally:
        connection.close()


def parse_tvdump(raw):
    """Decode TVD2; reject legacy TVDM rather than guessing its encoding."""
    start = raw.find(TVDM_MAGIC)
    if start < 0:
        raise ValueError("TVD2 header missing (legacy TVDM is unsupported)")
    if len(raw) - start < TVDM_HEADER_BYTES:
        raise ValueError("TVD2 header truncated")
    cols, rows = raw[start + 4:start + TVDM_HEADER_BYTES]
    if (cols, rows) != (TVDM_COLS, TVDM_ROWS):
        raise ValueError("TVD2 dimensions must be 80x25")
    cells = bytearray()
    pos = start + TVDM_HEADER_BYTES
    while len(cells) < cols * rows * 2:
        if pos >= len(raw):
            raise ValueError("TVD2 cell data truncated")
        value = raw[pos]
        pos += 1
        if value == 0x1b:
            if pos >= len(raw) or raw[pos] not in (0x44, 0x5b):
                raise ValueError("TVD2 invalid escape")
            value = raw[pos] ^ 0x40
            pos += 1
        elif value == 0x04:
            raise ValueError("TVD2 unexpected EOT")
        cells.append(value)
    return cols, rows, bytes(cells)


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
    parser.add_argument("--raw", type=Path, help="save the received response bytes")
    parser.add_argument("--port", help="serial port with rshell already active")
    parser.add_argument("--baud", type=int, default=9600, help="serial baud (default: 9600)")
    args = parser.parse_args()
    try:
        raw = receive_tvdump(args.port, args.baud)
        if args.raw:
            args.raw.write_bytes(raw)
        parse_and_display(raw)
    except (ValueError, OSError, emu.EmuError) as exc:
        print("tvdump: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
