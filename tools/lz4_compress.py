#!/usr/bin/env python3
# ========================================================================
#  lz4_compress.py -- OS32 アセット LZ4 圧縮ツール
#
#  使用法: python3 tools/lz4_compress.py INPUT OUTPUT
#
#  ファイル形式: [4B orig_size LE] [LZ4ブロックデータ]
#
#  必要パッケージ: pip install lz4
# ========================================================================

import sys
import struct

try:
    import lz4.block
except ImportError:
    print("Error: lz4 package not found. Install with: pip install lz4",
          file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 lz4_compress.py INPUT OUTPUT")
        print("")
        print("Compress INPUT using LZ4 block format and write to OUTPUT.")
        print("Output format: [4B orig_size LE] [LZ4 block data]")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    # 入力ファイル読み込み
    with open(input_path, 'rb') as f:
        data = f.read()

    orig_size = len(data)

    # LZ4ブロック圧縮 (store_size=False で純粋なブロックデータを出力)
    compressed = lz4.block.compress(data, store_size=False)
    comp_size = len(compressed)

    # 出力ファイル書き込み: [4B orig_size LE] + [LZ4 block]
    with open(output_path, 'wb') as f:
        f.write(struct.pack('<I', orig_size))
        f.write(compressed)

    total_size = 4 + comp_size
    if orig_size > 0:
        ratio = 100 * comp_size // orig_size
    else:
        ratio = 0

    print("{}: {} -> {} ({}%)".format(
        input_path, orig_size, total_size, ratio))


if __name__ == '__main__':
    main()
