#!/usr/bin/env python3
"""
create_fat12_d88.py — FAT12フォーマットのPC-98用D88テストフロッピー作成

外部ツール（mkfs.fat, mtools）不要。純Python実装。
PC-98 2HD: 77cyl × 2head × 8sect × 1024B/sect = 1,261,568 B
"""

import struct
import sys
import os

# ======== ディスクパラメータ ========
SECTOR_SIZE = 1024
CYLINDERS = 77
HEADS = 2
SPT = 8
TOTAL_SECTORS = CYLINDERS * HEADS * SPT  # 1232
DISK_SIZE = TOTAL_SECTORS * SECTOR_SIZE   # 1,261,568

# ======== FAT12レイアウト ========
RESERVED_SECTORS = 1   # ブートセクタ
NUM_FATS = 2
ROOT_ENTRIES = 64      # ルートディレクトリエントリ数
SECTORS_PER_CLUSTER = 1
FAT_SIZE = 2           # FATあたりセクタ数 (1232クラスタ → 1848バイト → 2セクタ)
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE  # 2

FAT_START = RESERVED_SECTORS                     # LBA 1
ROOT_DIR_START = FAT_START + NUM_FATS * FAT_SIZE # LBA 5
DATA_START = ROOT_DIR_START + ROOT_DIR_SECTORS   # LBA 7

def create_boot_sector():
    """BPB付きブートセクタ作成"""
    bs = bytearray(SECTOR_SIZE)
    
    # ジャンプ命令
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90
    
    # OEM名
    bs[3:11] = b'OS32TEST'
    
    # BPB
    struct.pack_into('<H', bs, 0x0B, SECTOR_SIZE)           # bytes_per_sector
    bs[0x0D] = SECTORS_PER_CLUSTER                          # sectors_per_cluster
    struct.pack_into('<H', bs, 0x0E, RESERVED_SECTORS)      # reserved_sectors
    bs[0x10] = NUM_FATS                                     # num_fats
    struct.pack_into('<H', bs, 0x11, ROOT_ENTRIES)          # root_entry_count
    struct.pack_into('<H', bs, 0x13, TOTAL_SECTORS)         # total_sectors_16
    bs[0x15] = 0xFE                                         # media type (PC-98 2HD)
    struct.pack_into('<H', bs, 0x16, FAT_SIZE)              # fat_size
    struct.pack_into('<H', bs, 0x18, SPT)                   # sectors_per_track
    struct.pack_into('<H', bs, 0x1A, HEADS)                 # num_heads
    struct.pack_into('<I', bs, 0x1C, 0)                     # hidden_sectors
    struct.pack_into('<I', bs, 0x20, 0)                     # total_sectors_32
    
    # ブートシグネチャ
    bs[SECTOR_SIZE - 2] = 0x55
    bs[SECTOR_SIZE - 1] = 0xAA
    
    return bs

def create_fat():
    """FAT12テーブル作成"""
    fat = bytearray(FAT_SIZE * SECTOR_SIZE)
    
    # メディアタイプ (FAT[0]) + 0xFFF (FAT[1])
    fat[0] = 0xFE  # メディアタイプ
    fat[1] = 0xFF
    fat[2] = 0xFF
    
    return fat

def fat12_set_entry(fat, cluster, value):
    """FAT12エントリ設定"""
    offset = cluster + (cluster // 2)
    val = fat[offset] | (fat[offset + 1] << 8)
    if cluster & 1:
        val = (val & 0x000F) | ((value & 0x0FFF) << 4)
    else:
        val = (val & 0xF000) | (value & 0x0FFF)
    fat[offset] = val & 0xFF
    fat[offset + 1] = (val >> 8) & 0xFF

def create_dir_entry(name8, ext3, size, start_cluster, attr=0x20):
    """32バイトのディレクトリエントリ作成"""
    entry = bytearray(32)
    n = name8.encode('ascii')[:8].ljust(8)
    e = ext3.encode('ascii')[:3].ljust(3)
    entry[0:8] = n
    entry[8:11] = e
    entry[11] = attr
    struct.pack_into('<H', entry, 0x1A, start_cluster)
    struct.pack_into('<I', entry, 0x1C, size)
    return entry

def add_file(disk, fat, root_dir, name, ext, data):
    """ファイルをディスクイメージに追加"""
    size = len(data)
    if size == 0:
        return
    
    # 必要なクラスタ数
    bytes_per_cluster = SECTORS_PER_CLUSTER * SECTOR_SIZE
    num_clusters = (size + bytes_per_cluster - 1) // bytes_per_cluster
    
    # 空きクラスタを探す
    first_cluster = -1
    prev_cluster = -1
    
    for _ in range(num_clusters):
        # 空きクラスタを見つける
        cluster = 2
        while cluster < TOTAL_SECTORS - DATA_START + 2:
            offset = cluster + (cluster // 2)
            val = fat[offset] | (fat[offset + 1] << 8)
            if cluster & 1:
                entry_val = (val >> 4) & 0x0FFF
            else:
                entry_val = val & 0x0FFF
            if entry_val == 0:
                break
            cluster += 1
        
        if first_cluster < 0:
            first_cluster = cluster
        if prev_cluster >= 0:
            fat12_set_entry(fat, prev_cluster, cluster)
        fat12_set_entry(fat, cluster, 0xFFF)  # EOC
        
        # データ書き込み
        lba = DATA_START + (cluster - 2) * SECTORS_PER_CLUSTER
        offset = lba * SECTOR_SIZE
        chunk_start = (cluster - first_cluster) * bytes_per_cluster
        chunk = data[chunk_start:chunk_start + bytes_per_cluster]
        disk[offset:offset + len(chunk)] = chunk
        
        prev_cluster = cluster
    
    # ディレクトリエントリを空きスロットに追加
    for i in range(ROOT_ENTRIES):
        off = i * 32
        if root_dir[off] == 0x00 or root_dir[off] == 0xE5:
            entry = create_dir_entry(name, ext, size, first_cluster)
            root_dir[off:off+32] = entry
            return
    
    print(f"警告: ディレクトリが満杯 ({name}.{ext})")

def main():
    print("=== FAT12テストフロッピー作成 (Python) ===")
    
    # ディスクイメージ初期化
    disk = bytearray(DISK_SIZE)
    
    # ブートセクタ
    bs = create_boot_sector()
    disk[0:SECTOR_SIZE] = bs
    
    # FAT
    fat = create_fat()
    
    # ルートディレクトリ
    root_dir = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    
    # ボリュームラベル
    vol_entry = create_dir_entry("OS32TEST", "   ", 0, 0, attr=0x08)
    root_dir[0:32] = vol_entry
    
    # テストファイル追加
    files = [
        ("HELLO", "TXT", b"Hello from FAT12 floppy!\n"),
        ("JP", "TXT", "こんにちは、FAT12！\nUTF-8日本語テスト\n".encode('utf-8')),
        ("TEST", "TXT", b"This is a test file for OS32 FAT12 driver.\nLine 2\nLine 3\n"),
        ("README", "TXT", b"OS32 FAT12 File System Test Disk\nCreated by create_fat12_d88.py\n"),
    ]
    
    for name, ext, data in files:
        print(f"  追加: {name}.{ext} ({len(data)} bytes)")
        add_file(disk, fat, root_dir, name, ext, data)
    
    # FATをディスクに書き込み (FAT#1, FAT#2)
    for f in range(NUM_FATS):
        base = (FAT_START + f * FAT_SIZE) * SECTOR_SIZE
        disk[base:base + len(fat)] = fat
    
    # ルートディレクトリをディスクに書き込み
    base = ROOT_DIR_START * SECTOR_SIZE
    disk[base:base + len(root_dir)] = root_dir
    
    # RAWイメージ保存
    raw_path = '/tmp/fat12_test.img'
    with open(raw_path, 'wb') as f:
        f.write(disk)
    print(f"\nRAWイメージ: {raw_path} ({len(disk)} bytes)")
    
    # D88に変換
    d88_path = '/mnt/c/WATCOM/src/os32/fat12_test.d88'
    
    import subprocess
    subprocess.run([
        'python3', '/mnt/c/WATCOM/src/os32/mkd88.py', raw_path, d88_path
    ], check=True)
    
    print(f"D88ファイル: {d88_path}")
    print("\nNP21/WでOS32起動後、FDD1を fat12_test.d88 に差し替えて:")
    print("  fat mount → fat ls → fat cat HELLO.TXT")

if __name__ == '__main__':
    main()
