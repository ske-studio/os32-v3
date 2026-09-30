#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OS32 FEP 辞書コンパイラ (SQLite版)

IPADIC の CSV データを読み込み、SQLite DB ファイルに変換する。

使い方:
    python3 tools/fep_to_sqlite.py                         # デフォルト (M)
    python3 tools/fep_to_sqlite.py --all                   # S/M/L 全て生成
    python3 tools/fep_to_sqlite.py --size L                # L のみ生成
    python3 tools/fep_to_sqlite.py -i assets/ipadic -o assets/fep.db

ページサイズは OS32 カーネルの SQLITE_DEFAULT_PAGE_SIZE (1024) に合わせる。
"""

import sys
import os
import glob
import csv
import sqlite3
import argparse

# OS32 カーネル側の設定と一致させる
PAGE_SIZE = 1024

# 活用形フィルタ (fep_compiler.py と同一)
KEEP_INFLECTIONS = {
    '*', '基本形', '基本形-促音便', '連用テ接続', '連用タ接続',
    '連用形', '未然形', '仮定形', '体言接続',
}

# サイズバリアント定義
SIZE_DEFS = {
    'S': {'cost_limit': 800, 'suffix': '_s'},
    'M': {'cost_limit': 1500, 'suffix': ''},    # デフォルト
    'L': {'cost_limit': 0, 'suffix': '_l'},      # 0 = 無制限
}


def kata_to_hira(text):
    """全角カタカナを全角ひらがなに変換する"""
    if not isinstance(text, str):
        return ""
    return "".join(
        chr(ord(c) - 0x60) if 0x30A1 <= ord(c) <= 0x30F6 else c
        for c in text
    )


def load_ipadic(input_dir):
    """IPADIC CSVファイルを読み込み、エントリリストを返す"""
    csv_files = glob.glob(os.path.join(input_dir, "*.csv"))
    if not csv_files:
        print(f"Error: No CSV files in {input_dir}", file=sys.stderr)
        sys.exit(1)

    entries = []
    for f in csv_files:
        try:
            with open(f, encoding='euc-jp', errors='replace') as fh:
                for row in csv.reader(fh):
                    if len(row) < 12:
                        continue
                    if row[9] not in KEEP_INFLECTIONS:
                        continue

                    kanji = row[0]
                    try:
                        pos_id = int(row[1])
                    except ValueError:
                        continue
                    try:
                        cost = int(row[3])
                    except ValueError:
                        continue

                    yomi_kata = row[11]
                    if not yomi_kata or not kanji:
                        continue

                    yomi = kata_to_hira(yomi_kata)

                    # CP932互換チェック (PC-9801表示可能文字のみ)
                    try:
                        kanji.encode('cp932')
                        yomi.encode('cp932')
                    except UnicodeEncodeError:
                        continue

                    yomi_b = yomi.encode('utf-8')
                    kanji_b = kanji.encode('utf-8')
                    if len(yomi_b) > 31 or len(kanji_b) > 31:
                        continue
                    if pos_id < 0:
                        pos_id = 0
                    if pos_id > 2047:
                        pos_id = 2047

                    entries.append((yomi, kanji, pos_id, cost))
        except Exception as e:
            print(f"Warning: {f}: {e}", file=sys.stderr)

    if not entries:
        print("Error: No valid entries", file=sys.stderr)
        sys.exit(1)

    return entries


def load_joyo_set():
    joyo_path = os.path.join(os.path.dirname(__file__), "../assets/joyo_kanji.txt")
    if not os.path.exists(joyo_path):
        return set()
    with open(joyo_path, 'r', encoding='utf-8') as f:
        return set(f.read().strip())

JOYO_SET = load_joyo_set()

def is_cjk(c):
    cp = ord(c)
    return 0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF

def is_all_katakana(text):
    if not text: return False
    return all(0x30A0 <= ord(c) <= 0x30FF or not (0x3000 <= ord(c)) for c in text)

def normalize_costs(entries):
    import math
    if not entries: return []

    min_cost = min(e[3] for e in entries)
    max_cost = max(e[3] for e in entries)
    
    log_min = math.log(max(1, min_cost))
    log_max = math.log(max(1, max_cost))
    log_range = log_max - log_min if log_max > log_min else 1.0

    result = []
    for yomi, kanji, pos_id, cost in entries:
        # 対数スケール正規化
        scaled = int(((math.log(max(1, cost)) - log_min) / log_range) * 2047)
        scaled = max(0, min(2047, scaled))

        # 常用漢字ブースト
        kanji_chars = [c for c in kanji if is_cjk(c)]
        if kanji_chars:
            joyo_ratio = sum(1 for c in kanji_chars if c in JOYO_SET) / len(kanji_chars)
            if joyo_ratio == 1.0:
                scaled = int(scaled * 0.4)
            elif joyo_ratio >= 0.5:
                scaled = int(scaled * 0.7)

        # 圧縮率ヒューリスティクス:
        # 読みの文字数に対して漢字の文字数が少ないほど「常用語」の可能性が高い
        # 例: ひがし(3) -> 東(1) = 高圧縮 → ブースト
        #     ひがし(3) -> 干菓子(3) = 等倍 → ペナルティ
        yomi_len = len(yomi)    # Python str len = Unicode文字数
        kanji_len = len(kanji)
        if yomi_len > 0 and kanji_len > 0:
            ratio = kanji_len / yomi_len
            if ratio <= 0.5:
                # 高圧縮 (例: 3文字→1文字) → 40%減
                scaled = int(scaled * 0.4)
            elif ratio < 1.0:
                # 中圧縮 (例: 3文字→2文字) → 20%減
                scaled = int(scaled * 0.8)
            elif ratio >= 1.0 and kanji_len > 1:
                # 非圧縮で複数文字 (例: 3文字→3文字) → ペナルティ
                scaled = min(scaled + 200, 2047)

        # カタカナのみ、または、ひらがなそのままの場合は降格
        if is_all_katakana(kanji) or kanji == yomi:
            scaled = min(scaled + 800, 2047)

        result.append((yomi, kanji, pos_id, scaled))

    return result

def create_db(entries, output_path, cost_limit):
    if cost_limit > 0:
        filtered = [e for e in entries if e[3] <= cost_limit]
    else:
        filtered = entries

    if os.path.exists(output_path):
        os.remove(output_path)

    conn = sqlite3.connect(output_path)
    cur = conn.cursor()

    cur.execute(f"PRAGMA page_size = {PAGE_SIZE}")

    cur.execute("""
        CREATE TABLE dict_raw (
            yomi   TEXT NOT NULL,
            kanji  TEXT NOT NULL,
            pos_id INTEGER DEFAULT 0,
            cost   INTEGER DEFAULT 1000
        )
    """)

    cur.executemany(
        "INSERT INTO dict_raw VALUES (?, ?, ?, ?)",
        filtered
    )

    # 重複排除: yomi+kanji でグループ化し、最小コストのみを残す。
    #
    # コストが同点のときの pos_id は最小のものを採る。単に
    # "SELECT pos_id, MIN(cost) ... GROUP BY" と書くと、同点行のうち
    # どの pos_id が採られるかが SQLite の実装依存になり、同じ入力から
    # 毎回違う DB ができる (実測で 97,514 行中 11 行が揺れた)。
    # 生成物を git で追跡しない以上、再生成の再現性は担保しておく。
    cur.execute("""
        CREATE TABLE dict AS
        SELECT d.yomi, d.kanji, MIN(d.pos_id) AS pos_id, d.cost
        FROM dict_raw d
        JOIN (
            SELECT yomi, kanji, MIN(cost) AS min_cost
            FROM dict_raw GROUP BY yomi, kanji
        ) m ON d.yomi = m.yomi AND d.kanji = m.kanji AND d.cost = m.min_cost
        GROUP BY d.yomi, d.kanji
    """)

    cur.execute("DROP TABLE dict_raw")
    cur.execute("CREATE INDEX idx_dict_yomi ON dict(yomi)")
    conn.commit()

    cur.execute("VACUUM")
    conn.close()

    size = os.path.getsize(output_path)
    # 最終エントリ数を取得
    conn = sqlite3.connect(output_path)
    final_count = conn.execute("SELECT count(*) FROM dict").fetchone()[0]
    conn.close()

    print(f"  {output_path}: {final_count:,} entries (filtered from {len(filtered):,}), "
          f"{size:,} bytes ({size / 1048576:.1f} MB)")
    return size


def main():
    parser = argparse.ArgumentParser(
        description="OS32 FEP Dictionary Compiler (SQLite)")
    parser.add_argument("-i", "--input", default="assets/ipadic",
                        help="IPADIC CSVディレクトリ")
    parser.add_argument("-o", "--output", default="assets/fep.db",
                        help="出力DBファイル (デフォルト: assets/fep.db)")
    parser.add_argument("--all", action="store_true",
                        help="S/M/L 全サイズバリアントを生成")
    parser.add_argument("--size", choices=['S', 'M', 'L'], default='M',
                        help="生成するサイズバリアント (デフォルト: M)")
    args = parser.parse_args()

    print("=== OS32 FEP Dictionary Builder (SQLite) ===")
    print(f"Input: {args.input}")

    # IPADIC読み込み + コスト正規化
    entries = load_ipadic(args.input)
    entries = normalize_costs(entries)
    print(f"Total entries (after normalization): {len(entries):,}")

    output_dir = os.path.dirname(args.output) or "."
    basename = os.path.splitext(os.path.basename(args.output))[0]

    if args.all:
        print()
        for size_name in ['S', 'M', 'L']:
            sdef = SIZE_DEFS[size_name]
            suffix = sdef['suffix']
            out_path = os.path.join(output_dir, f"{basename}{suffix}.db")
            limit = sdef['cost_limit']
            label = f"(cost<{limit})" if limit > 0 else "(full)"
            print(f"--- {size_name} {label} ---")
            create_db(entries, out_path, limit)
    else:
        sdef = SIZE_DEFS[args.size]
        out_path = args.output
        if args.size != 'M':
            suffix = sdef['suffix']
            out_path = os.path.join(output_dir, f"{basename}{suffix}.db")
        limit = sdef['cost_limit']
        print()
        create_db(entries, out_path, limit)

    print("\n=== Done ===")


if __name__ == "__main__":
    main()
