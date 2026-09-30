#!/usr/bin/env python3
"""
mkd88.py — ベタ(RAW)フロッピーイメージをD88形式に変換する
PC-98 1MB 2HD (77シリンダ × 2ヘッド × 8セクタ × 1024バイト)

D88形式仕様:
  - 688バイトのヘッダ (ディスク名, ディスクサイズ等)
  - 各トラックのオフセットテーブル
  - トラックデータ (各セクタにセクタヘッダ + セクタデータ)

使い方:
  python3 mkd88.py input_raw.img output.d88
"""

import sys
import struct
import os

# ディスクパラメータ (PC-98 2HD 1MB)
CYLINDERS = 77
HEADS = 2
SECTORS_PER_TRACK = 8
SECTOR_SIZE = 1024
TRACK_COUNT = CYLINDERS * HEADS  # 154トラック

# D88ヘッダサイズ
D88_HEADER_SIZE = 0x2B0  # 688バイト

# セクタヘッダサイズ
SECTOR_HEADER_SIZE = 16

def make_d88(raw_path, d88_path):
    """ベタイメージをD88に変換"""
    
    # RAWデータの読み込み
    with open(raw_path, "rb") as f:
        raw_data = f.read()
    
    expected_size = CYLINDERS * HEADS * SECTORS_PER_TRACK * SECTOR_SIZE
    if len(raw_data) < expected_size:
        # 足りない分を0で埋める
        raw_data += b'\x00' * (expected_size - len(raw_data))
    
    # D88ヘッダの構築
    header = bytearray(D88_HEADER_SIZE)
    
    # ディスク名 (最大17バイト + null = 先頭17バイト)
    disk_name = b"OS32 Boot Disk\x00"
    header[0:len(disk_name)] = disk_name
    
    # オフセット 0x1A: write protect (0=なし)
    header[0x1A] = 0x00
    
    # オフセット 0x1B: ディスクタイプ
    # 0x00=2D, 0x10=2DD, 0x20=2HD
    header[0x1B] = 0x20  # 2HD
    
    # ディスク全体のサイズは後で計算
    
    # 各トラックのオフセットテーブル (0x20から、各4バイト × 164エントリ)
    # 使うのは154トラック (77cyl × 2head)
    track_data_list = []
    current_offset = D88_HEADER_SIZE
    
    for track in range(TRACK_COUNT):
        # オフセットテーブルに現在のオフセットを書き込み
        struct.pack_into('<I', header, 0x20 + track * 4, current_offset)
        
        # このトラックのデータを構築
        cyl = track // 2
        head = track % 2
        track_bytes = bytearray()
        
        for sec in range(SECTORS_PER_TRACK):
            # セクタヘッダ (16バイト)
            # C(1) H(1) R(1) N(1) sectors(2) density(1) deleted(1)
            # status(1) reserved(5) datasize(2) = 16バイト
            sec_header = struct.pack('<BBBBHBBB5sH',
                cyl,            # C (シリンダ番号)
                head,           # H (ヘッド番号)
                sec + 1,        # R (セクタ番号, 1始まり)
                3,              # N (セクタサイズ: 3=1024バイト)
                SECTORS_PER_TRACK,  # セクタ数
                0,              # 密度 (0x00=倍密度MFM)
                0,              # 削除マーク (0x00=通常)
                0,              # ステータス (0x00=正常)
                b'\x00' * 5,    # 予約
                SECTOR_SIZE     # データサイズ (2バイト)
            )
            track_bytes += sec_header
            
            # セクタデータ
            raw_offset = (cyl * HEADS + head) * SECTORS_PER_TRACK + sec
            data_start = raw_offset * SECTOR_SIZE
            data_end = data_start + SECTOR_SIZE
            sector_data = raw_data[data_start:data_end]
            if len(sector_data) < SECTOR_SIZE:
                sector_data += b'\x00' * (SECTOR_SIZE - len(sector_data))
            track_bytes += sector_data
        
        track_data_list.append(bytes(track_bytes))
        current_offset += len(track_bytes)
    
    # 未使用トラックのオフセットは0
    for track in range(TRACK_COUNT, 164):
        struct.pack_into('<I', header, 0x20 + track * 4, 0)
    
    # ディスク全体サイズをヘッダに書き込み (オフセット 0x1C, 4バイト)
    total_size = current_offset
    struct.pack_into('<I', header, 0x1C, total_size)
    
    # D88ファイルの書き出し
    with open(d88_path, "wb") as f:
        f.write(bytes(header))
        for td in track_data_list:
            f.write(td)
    
    print(f"  D88イメージ生成: {d88_path}")
    print(f"  サイズ: {total_size} バイト ({total_size/1024:.1f} KB)")
    print(f"  構成: {CYLINDERS}cyl × {HEADS}head × {SECTORS_PER_TRACK}sec × {SECTOR_SIZE}B")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"使い方: {sys.argv[0]} <入力RAWイメージ> <出力D88ファイル>", file=sys.stderr)
        sys.exit(1)
    make_d88(sys.argv[1], sys.argv[2])
