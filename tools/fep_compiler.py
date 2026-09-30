#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
OS32 FEP 辞書コンパイラ (v2 — 先頭2文字インデックス常駐方式)

IPADICのCSVデータを読み込み、独自のバイナリ形式 (.dic) に変換します。

■ バイナリ構造:
  [Header 16B]
  [Level-1 Index: 先頭1文字 → Level-2サブインデックスの開始位置]
  [Level-2 Index: 先頭2文字 → データブロックの開始位置]
  [Data Blocks: WordMeta32 + yomi + kanji の連続]

■ 実機での常駐メモリ:
  Header(16B) + Level1(~624B) + Level2(~43KB) ≒ 約44KB
"""

import sys
import os
import glob
import pandas as pd
import struct

DICT_MAGIC = b"DICT"
DICT_VERSION = 2

def kata_to_hira(text):
    """全角カタカナを全角ひらがなに変換する"""
    if not isinstance(text, str):
        return ""
    res = []
    for c in text:
        cp = ord(c)
        if 0x30A1 <= cp <= 0x30F6:
            res.append(chr(cp - 0x60))
        else:
            res.append(c)
    return "".join(res)


def compile_dict(input_dir, output_file):
    print(f"Reading CSV files from {input_dir} ...")
    csv_files = glob.glob(os.path.join(input_dir, "*.csv"))

    if not csv_files:
        print(f"Error: No CSV files found in {input_dir}")
        sys.exit(1)

    df_list = []
    for file in csv_files:
        try:
            df = pd.read_csv(file, encoding='euc-jp', header=None, on_bad_lines='skip')
            df_list.append(df)
        except Exception as e:
            print(f"Warning: Failed to read {file}: {e}")

    if not df_list:
        print("Error: Could not read any CSV data.")
        sys.exit(1)

    data = pd.concat(df_list, ignore_index=True)
    total_raw = len(data)
    print(f"Total raw entries: {total_raw}")

    if data.shape[1] < 12:
        print("Error: Invalid CSV format (columns < 12).")
        sys.exit(1)

    # 活用形フィルタ (実用頻度の高い活用形を含める)
    KEEP_INFLECTIONS = [
        '*',            # 非活用語 (名詞、副詞等)
        '基本形',        # 辞書形 (生きる、食べる)
        '基本形-促音便',   # 促音便基本形
        '連用テ接続',     # て形 (生きて、食べて)
        '連用タ接続',     # た形 (生きた、食べた)
        '連用形',        # ます形 (生き、食べ)
        '未然形',        # ない形語幹 (生きな、食べな)
        '仮定形',        # ば形 (生きれ、食べれ)
        '体言接続',      # 名詞修飾 (大きな)
    ]
    data = data[data[9].isin(KEEP_INFLECTIONS)]
    data = data.dropna(subset=[11, 0, 1, 3])

    # コスト正規化範囲
    min_cost = data[3].min()
    max_cost = data[3].max()
    cost_range = max_cost - min_cost if max_cost > min_cost else 1

    print("Filtering and processing entries...")
    filtered_entries = []

    for idx, row in data.iterrows():
        kanji = str(row[0])
        pos_id = int(row[1])
        original_cost = int(row[3])
        yomi_kata = str(row[11])

        yomi = kata_to_hira(yomi_kata)

        try:
            kanji.encode('cp932')
            yomi.encode('cp932')
        except UnicodeEncodeError:
            continue

        yomi_bytes = yomi.encode('utf-8')
        kanji_bytes = kanji.encode('utf-8')

        if len(yomi_bytes) > 31 or len(kanji_bytes) > 31:
            continue
        if pos_id < 0:
            pos_id = 0
        if pos_id > 2047:
            pos_id = 2047

        scaled_cost = int(((original_cost - min_cost) / cost_range) * 2047)
        if scaled_cost < 0:
            scaled_cost = 0
        if scaled_cost > 2047:
            scaled_cost = 2047

        if len(yomi) > 0:
            # Level-1 key: 先頭1文字, Level-2 key: 先頭2文字(1文字しかなければ1文字)
            l1_key = yomi[0]
            l2_key = yomi[:2] if len(yomi) >= 2 else yomi[:1]
            filtered_entries.append((kanji_bytes, pos_id, scaled_cost, yomi_bytes, l1_key, l2_key))

    print(f"Processed valid entries: {len(filtered_entries)} / {total_raw}")

    if not filtered_entries:
        print("No valid entries left.")
        sys.exit(1)

    # ソート: 読みバイト列 → コスト昇順
    filtered_entries.sort(key=lambda x: (x[3], x[2]))

    # Level-2 ブロックグループ化
    l2_blocks = {}
    for entry in filtered_entries:
        l2k = entry[5]
        if l2k not in l2_blocks:
            l2_blocks[l2k] = []
        l2_blocks[l2k].append(entry)

    # Level-1 グループ化 (どのL2キーがどのL1に属するか)
    l1_to_l2 = {}
    for l2k in sorted(l2_blocks.keys()):
        l1k = l2k[0]
        if l1k not in l1_to_l2:
            l1_to_l2[l1k] = []
        l1_to_l2[l1k].append(l2k)

    l1_count = len(l1_to_l2)
    l2_count = len(l2_blocks)

    print(f"Level-1 blocks: {l1_count}")
    print(f"Level-2 blocks: {l2_count}")

    # === バイナリ構築 ===
    # Header: 16B
    #   0x00: magic "DICT" (4B)
    #   0x04: version (4B)
    #   0x08: total_words (4B)
    #   0x0C: l1_count (4B)

    # Level-1 Index: l1_count × 12B
    #   key_char (4B, UTF-8 + padding) + l2_offset (4B) + l2_num (4B)
    #   l2_offset: Level-2 インデックス内でこのL1に属するL2エントリの開始位置(ファイル先頭から)
    #   l2_num: このL1に属するL2エントリ数

    # Level-2 Index: l2_count × 12B
    #   key_chars (8B, UTF-8 + padding) + data_offset (4B)

    # Data Blocks: 各 WordEntry の連続

    HEADER_SIZE = 16
    L1_ENTRY_SIZE = 12
    L2_ENTRY_SIZE = 12

    l1_index_offset = HEADER_SIZE
    l1_index_size = l1_count * L1_ENTRY_SIZE
    l2_index_offset = l1_index_offset + l1_index_size
    l2_index_size = l2_count * L2_ENTRY_SIZE
    data_offset_start = l2_index_offset + l2_index_size

    out_f = open(output_file, 'wb')

    # ヘッダ
    out_f.write(DICT_MAGIC)
    out_f.write(struct.pack("<I", DICT_VERSION))
    out_f.write(struct.pack("<I", len(filtered_entries)))
    out_f.write(struct.pack("<I", l1_count))

    # Level-1 インデックスのプレースホルダ
    out_f.write(b'\0' * l1_index_size)

    # Level-2 インデックスのプレースホルダ
    out_f.write(b'\0' * l2_index_size)

    # データブロック書き込み
    l2_index_data = []  # [(key_bytes_8, data_offset)]
    l1_index_data = []  # [(key_bytes_4, l2_file_offset, l2_num)]

    l2_serial = 0  # Level-2エントリの通し番号

    for l1k in sorted(l1_to_l2.keys()):
        l2_keys = l1_to_l2[l1k]
        l1_l2_start_offset = l2_index_offset + l2_serial * L2_ENTRY_SIZE
        l1_l2_num = len(l2_keys)

        l1_key_bytes = l1k.encode('utf-8')[:4].ljust(4, b'\0')
        l1_index_data.append((l1_key_bytes, l1_l2_start_offset, l1_l2_num))

        for l2k in l2_keys:
            data_offset = out_f.tell()
            l2_key_bytes = l2k.encode('utf-8')[:8].ljust(8, b'\0')
            l2_index_data.append((l2_key_bytes, data_offset))
            l2_serial += 1

            for entry in l2_blocks[l2k]:
                kanji_b, pos_id, cost, yomi_b, _, _ = entry
                y_len = len(yomi_b)
                k_len = len(kanji_b)

                meta = ((y_len & 0x1F)
                        | ((k_len & 0x1F) << 5)
                        | ((pos_id & 0x7FF) << 10)
                        | ((cost & 0x7FF) << 21))
                out_f.write(struct.pack("<I", meta))
                out_f.write(yomi_b)
                out_f.write(kanji_b)

    file_end = out_f.tell()

    # Level-1 インデックスを書き戻す
    out_f.seek(l1_index_offset)
    for key_b4, l2_off, l2_n in l1_index_data:
        out_f.write(key_b4)
        out_f.write(struct.pack("<I", l2_off))
        out_f.write(struct.pack("<I", l2_n))

    # Level-2 インデックスを書き戻す
    out_f.seek(l2_index_offset)
    for key_b8, d_off in l2_index_data:
        out_f.write(key_b8)
        out_f.write(struct.pack("<I", d_off))

    out_f.close()

    # 統計出力
    file_size = os.path.getsize(output_file)
    resident_mem = HEADER_SIZE + l1_index_size + l2_index_size

    # ブロックサイズ計算
    block_sizes = []
    for i in range(len(l2_index_data)):
        start = l2_index_data[i][1]
        end = l2_index_data[i + 1][1] if i + 1 < len(l2_index_data) else file_end
        block_sizes.append(end - start)

    print(f"\n=== Compilation Result ===")
    print(f"Output: {output_file}")
    print(f"File size: {file_size:,} bytes ({file_size / 1024:.1f} KB)")
    print(f"Dict version: {DICT_VERSION}")
    print(f"Total words: {len(filtered_entries):,}")
    print(f"Level-1 entries: {l1_count}")
    print(f"Level-2 entries: {l2_count}")
    print(f"Resident memory (header + L1 + L2): {resident_mem:,} bytes ({resident_mem / 1024:.1f} KB)")
    print(f"Block size — max: {max(block_sizes):,} bytes ({max(block_sizes) / 1024:.1f} KB)")
    print(f"Block size — avg: {sum(block_sizes) // len(block_sizes):,} bytes")
    print(f"Block size — median: {sorted(block_sizes)[len(block_sizes) // 2]:,} bytes")
    print(f"Block size — min: {min(block_sizes):,} bytes")

    # 最大10ブロック表示
    sorted_l2_keys = sorted(l2_blocks.keys())
    block_with_size = list(zip(sorted_l2_keys, block_sizes))
    block_with_size.sort(key=lambda x: x[1], reverse=True)
    print(f"\nLargest 10 blocks:")
    for k, s in block_with_size[:10]:
        print(f"  \"{k}\" : {s:,} bytes ({s / 1024:.1f} KB)")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="OS32 FEP Dictionary Compiler (v2)")
    parser.add_argument("-i", "--input", default="assets/ipadic",
                        help="Input directory containing CSV files")
    parser.add_argument("-o", "--output", default="assets/fep.dic",
                        help="Output .dic file path")
    args = parser.parse_args()

    compile_dict(args.input, args.output)
