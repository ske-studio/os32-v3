#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mkfat12.py — PC-98 2HD (1024B/sector) FAT12 フロッピーイメージ作成ツール

PC-98 2HD仕様:
  77シリンダ × 2ヘッド × 8セクタ/トラック = 1232セクタ
  1024バイト/セクタ
  総容量: 1,261,568バイト (1232KB)

FAT12レイアウト:
  セクタ 0:      ブートセクタ (BPB + IPLコード用の空間)
  セクタ 1-2:    FAT1 (2セクタ)
  セクタ 3-4:    FAT2 (2セクタ)
  セクタ 5-10:   ルートディレクトリ (192エントリ × 32B = 6144B = 6セクタ)
  セクタ 11〜:   データ領域 (クラスタ2から開始)

使用例 (レガシー: ルート直下配置):
  python3 mkfat12.py -o os_raw.img -b boot_fat.bin \\
      LOADER.BIN=loader.bin KERNEL.BIN=kernel.bin

使用例 (ツリーモード: サブディレクトリ対応):
  python3 mkfat12.py -o os_raw.img -b boot_fat.bin --tree \\
      /kernel.bin=kernel.bin \\
      /sys/shell.bin=programs/shell.bin \\
      /bin/grep.bin=programs/grep.bin
"""

import sys
import os
import struct
import argparse

# ---------------------------------------------------------------- ジオメトリ
#
# **既定 (`2hd`) は動かさない。** 製品の `images/os32_boot.d88` はこれで作る。
#
# `144` は 1.44MB (PC/AT 標準の 2HD)。PC-98 でも DA/UA 0x30 台の
# 「1.44MB 対応両用インタフェース」から起動できる (PC9800Bible 表 2-34、
# undocumented/memsys.md の `0000:0584h DISK_BOOT`)。
#
# **`144` のルートエントリ数は標準の 224 ではなく 192。** IPL はルート
# ディレクトリを 0x6000 へ**一括で**読むので、224 だと 14 セクタ × 512B =
# 7168B で終端が 0x7C00 = SP にちょうど当たり、**スタックと衝突する**
# (`boot/boot_fat.asm`)。192 なら 12 セクタ = 6144B で 2HD と同じ余裕が残る。
# エントリ数は BPB で宣言するので、ホスト側のマウントには影響しない。
GEOMETRIES = {
    '2hd': dict(bps=1024, spc=1, root=192, total=1232, media=0xFE,
                fat=2, spt=8, heads=2,
                label='PC-98 2HD (1232KB)'),
    '144': dict(bps=512, spc=1, root=192, total=2880, media=0xF0,
                fat=9, spt=18, heads=2,
                label='1.44MB (2HD 512B/sector)'),
}
GEOMETRY_NAME = '2hd'

# PC-98 2HD FAT12パラメータ (既定。`select_geometry()` が差し替える)
BYTES_PER_SECTOR = 1024
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 1        # ブートセクタ
NUM_FATS = 2
ROOT_ENTRY_COUNT = 192      # ルートディレクトリエントリ数
TOTAL_SECTORS = 1232        # 77cyl × 2head × 8sect
MEDIA_TYPE = 0xFE           # PC-98 2HD
FAT_SIZE = 2                # FATあたりセクタ数
SECTORS_PER_TRACK = 8
NUM_HEADS = 2
HIDDEN_SECTORS = 0

# 計算値
FAT_START = RESERVED_SECTORS                       # セクタ1
ROOT_DIR_START = FAT_START + NUM_FATS * FAT_SIZE   # セクタ5
ROOT_DIR_SECTORS = (ROOT_ENTRY_COUNT * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR  # 6
DATA_START = ROOT_DIR_START + ROOT_DIR_SECTORS     # セクタ11
TOTAL_DATA_CLUSTERS = TOTAL_SECTORS - DATA_START   # 1221

# FAT12特殊値
FAT12_FREE = 0x000
FAT12_EOC  = 0xFFF
FAT12_MEDIA = 0xFF0 | MEDIA_TYPE  # メディアバイト


def select_geometry(name):
    """ジオメトリを選び、派生値まで作り直す。**他の関数より先に呼ぶこと。**

    定数はすべて関数の中でしか読まれないので、ここで束ね直せば全体が追随する
    (モジュール直下やデフォルト引数で使っている箇所は無い)。
    """
    global GEOMETRY_NAME, BYTES_PER_SECTOR, SECTORS_PER_CLUSTER, ROOT_ENTRY_COUNT
    global TOTAL_SECTORS, MEDIA_TYPE, FAT_SIZE, SECTORS_PER_TRACK, NUM_HEADS
    global FAT_START, ROOT_DIR_START, ROOT_DIR_SECTORS, DATA_START
    global TOTAL_DATA_CLUSTERS, FAT12_MEDIA

    g = GEOMETRIES[name]
    GEOMETRY_NAME = name
    BYTES_PER_SECTOR = g['bps']
    SECTORS_PER_CLUSTER = g['spc']
    ROOT_ENTRY_COUNT = g['root']
    TOTAL_SECTORS = g['total']
    MEDIA_TYPE = g['media']
    FAT_SIZE = g['fat']
    SECTORS_PER_TRACK = g['spt']
    NUM_HEADS = g['heads']

    FAT_START = RESERVED_SECTORS
    ROOT_DIR_START = FAT_START + NUM_FATS * FAT_SIZE
    ROOT_DIR_SECTORS = (ROOT_ENTRY_COUNT * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    DATA_START = ROOT_DIR_START + ROOT_DIR_SECTORS
    TOTAL_DATA_CLUSTERS = TOTAL_SECTORS - DATA_START
    FAT12_MEDIA = 0xFF0 | MEDIA_TYPE

    # FAT が全クラスタを表せること (FAT12 は 1 エントリ 1.5 バイト)。
    need = (TOTAL_DATA_CLUSTERS + 2) * 3 // 2
    have = FAT_SIZE * BYTES_PER_SECTOR
    if need > have:
        raise SystemExit(
            "mkfat12: %s: FAT が足りない (必要 %d B / 確保 %d B)" % (name, need, have))
    # ルートディレクトリがセクタ境界で割り切れること (IPL が一括で読むため)。
    if (ROOT_ENTRY_COUNT * 32) % BYTES_PER_SECTOR:
        raise SystemExit("mkfat12: %s: ルートDirがセクタ境界に揃っていない" % name)

# ディレクトリエントリあたりのバイト数
DIR_ENTRY_SIZE = 32


def make_bpb():
    """BPB (BIOS Parameter Block) を生成 (オフセット0x00〜0x23, 36バイト)"""
    bpb = bytearray(36)
    # ジャンプ命令 (JMP SHORT xx; NOP) — 後でブートコードで上書き
    bpb[0] = 0xEB  # JMP SHORT
    bpb[1] = 0x22  # +34 (BPBの直後にジャンプ)
    bpb[2] = 0x90  # NOP
    # OEM名 (8バイト)
    oem = b'OS32IPL '
    bpb[3:11] = oem
    # BPBフィールド
    struct.pack_into('<H', bpb, 0x0B, BYTES_PER_SECTOR)
    bpb[0x0D] = SECTORS_PER_CLUSTER
    struct.pack_into('<H', bpb, 0x0E, RESERVED_SECTORS)
    bpb[0x10] = NUM_FATS
    struct.pack_into('<H', bpb, 0x11, ROOT_ENTRY_COUNT)
    struct.pack_into('<H', bpb, 0x13, TOTAL_SECTORS)
    bpb[0x15] = MEDIA_TYPE
    struct.pack_into('<H', bpb, 0x16, FAT_SIZE)
    struct.pack_into('<H', bpb, 0x18, SECTORS_PER_TRACK)
    struct.pack_into('<H', bpb, 0x1A, NUM_HEADS)
    struct.pack_into('<I', bpb, 0x1C, HIDDEN_SECTORS)
    struct.pack_into('<I', bpb, 0x20, 0)  # total_sectors_32 (16bitで足りるので0)
    return bytes(bpb)


def fat12_get(fat_data, cluster):
    """FAT12エントリを読み取り"""
    offset = cluster + cluster // 2
    if offset + 1 >= len(fat_data):
        return FAT12_EOC
    val = fat_data[offset] | (fat_data[offset + 1] << 8)
    if cluster & 1:
        return (val >> 4) & 0x0FFF
    else:
        return val & 0x0FFF


def fat12_set(fat_data, cluster, value):
    """FAT12エントリを書き込み"""
    offset = cluster + cluster // 2
    if offset + 1 >= len(fat_data):
        return
    val = fat_data[offset] | (fat_data[offset + 1] << 8)
    if cluster & 1:
        val = (val & 0x000F) | ((value & 0x0FFF) << 4)
    else:
        val = (val & 0xF000) | (value & 0x0FFF)
    fat_data[offset] = val & 0xFF
    fat_data[offset + 1] = (val >> 8) & 0xFF


def name_to_83(name):
    """ファイル名を8.3形式 (11バイト) に変換"""
    name = name.upper()
    if '.' in name:
        base, ext = name.rsplit('.', 1)
    else:
        base, ext = name, ''
    base = base[:8].ljust(8)
    ext = ext[:3].ljust(3)
    return (base + ext).encode('ascii')


def make_dir_entry(name83, attr, cluster, size, date=0x5A21, time=0x0000):
    """32バイトのディレクトリエントリを生成"""
    entry = bytearray(32)
    entry[0:11] = name83
    entry[0x0B] = attr
    # 時刻・日付
    struct.pack_into('<H', entry, 0x16, time)
    struct.pack_into('<H', entry, 0x18, date)
    struct.pack_into('<H', entry, 0x1A, cluster & 0xFFFF)
    struct.pack_into('<I', entry, 0x1C, size)
    return bytes(entry)


class Fat12Builder:
    """FAT12イメージビルダー (サブディレクトリ対応)"""

    def __init__(self):
        self.image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)
        self.fat = bytearray(FAT_SIZE * BYTES_PER_SECTOR)
        self.root_dir = bytearray(ROOT_DIR_SECTORS * BYTES_PER_SECTOR)
        self.next_cluster = 2
        self.root_dir_index = 0
        # サブディレクトリのクラスタ → (dir_data bytearray, entry_index int)
        self.subdir_info = {}

        # FAT初期化
        self.fat[0] = MEDIA_TYPE  # 0xFE
        self.fat[1] = 0xFF
        self.fat[2] = 0xFF

    def _alloc_cluster(self):
        """次の空きクラスタを割り当て"""
        if self.next_cluster >= TOTAL_DATA_CLUSTERS + 2:
            raise RuntimeError("ディスク容量不足")
        c = self.next_cluster
        self.next_cluster += 1
        return c

    def _cluster_to_offset(self, cluster):
        """クラスタ番号→イメージ内オフセット"""
        sector_lba = DATA_START + (cluster - 2) * SECTORS_PER_CLUSTER
        return sector_lba * BYTES_PER_SECTOR

    def _write_file_data(self, data):
        """ファイルデータをクラスタチェーンに書き込み、最初のクラスタ番号を返す"""
        if len(data) == 0:
            return 0

        file_size = len(data)
        cluster_bytes = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
        clusters_needed = max(1, (file_size + cluster_bytes - 1) // cluster_bytes)

        first_cluster = self._alloc_cluster()
        prev_cluster = first_cluster
        remaining = file_size
        offset = 0

        for i in range(clusters_needed):
            if i == 0:
                cluster = first_cluster
            else:
                cluster = self._alloc_cluster()
                fat12_set(self.fat, prev_cluster, cluster)

            # データ書き込み
            img_offset = self._cluster_to_offset(cluster)
            chunk = min(remaining, cluster_bytes)
            self.image[img_offset:img_offset + chunk] = data[offset:offset + chunk]
            offset += chunk
            remaining -= chunk

            fat12_set(self.fat, cluster, FAT12_EOC)
            prev_cluster = cluster

        return first_cluster

    def _add_root_entry(self, name83, attr, cluster, size):
        """ルートディレクトリにエントリを追加"""
        if self.root_dir_index >= ROOT_ENTRY_COUNT:
            raise RuntimeError("ルートディレクトリエントリ不足")
        entry = make_dir_entry(name83, attr, cluster, size)
        idx = self.root_dir_index * DIR_ENTRY_SIZE
        self.root_dir[idx:idx + DIR_ENTRY_SIZE] = entry
        self.root_dir_index += 1

    def _add_subdir_entry(self, parent_cluster, name83, attr, cluster, size):
        """サブディレクトリにエントリを追加 (クラスタ拡張対応)"""
        info = self.subdir_info[parent_cluster]
        dir_data = info['data']
        entry_idx = info['index']

        cluster_bytes = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
        max_entries = len(dir_data) // DIR_ENTRY_SIZE

        # 現在のデータ領域が足りない場合はクラスタを追加
        if entry_idx >= max_entries:
            new_cluster = self._alloc_cluster()
            fat12_set(self.fat, new_cluster, FAT12_EOC)

            # 既存の最後のクラスタからチェーンを繋ぐ
            last_cluster = info.get('last_cluster', parent_cluster)
            fat12_set(self.fat, last_cluster, new_cluster)
            info['last_cluster'] = new_cluster

            # データ領域を拡張
            new_data = bytearray(cluster_bytes)
            dir_data.extend(new_data)
            info['data'] = dir_data

            # 新しいクラスタ領域をイメージに初期化書き込み
            new_offset = self._cluster_to_offset(new_cluster)
            self.image[new_offset:new_offset + cluster_bytes] = new_data

            # 拡張後の最大エントリ数を再計算
            max_entries = len(dir_data) // DIR_ENTRY_SIZE

        entry = make_dir_entry(name83, attr, cluster, size)
        idx = entry_idx * DIR_ENTRY_SIZE
        dir_data[idx:idx + DIR_ENTRY_SIZE] = entry
        info['index'] = entry_idx + 1

        # イメージに書き戻し (全クラスタ分)
        # 最初のクラスタ
        img_offset = self._cluster_to_offset(parent_cluster)
        write_len = min(cluster_bytes, len(dir_data))
        self.image[img_offset:img_offset + write_len] = dir_data[:write_len]

        # 追加クラスタ群の書き戻し
        remaining = len(dir_data) - cluster_bytes
        chain_cluster = fat12_get(self.fat, parent_cluster)
        data_offset = cluster_bytes
        while remaining > 0 and chain_cluster >= 2 and chain_cluster < FAT12_EOC:
            c_offset = self._cluster_to_offset(chain_cluster)
            write_len = min(cluster_bytes, remaining)
            self.image[c_offset:c_offset + write_len] = dir_data[data_offset:data_offset + write_len]
            data_offset += write_len
            remaining -= write_len
            chain_cluster = fat12_get(self.fat, chain_cluster)

    def _ensure_directory(self, dir_path, parent_cluster=None):
        """
        ディレクトリを確保 (存在しなければ作成)。
        dir_path: 正規化されたパス (例: "/sys", "/bin")
        parent_cluster: 親ディレクトリのクラスタ (None=ルート)
        戻り値: ディレクトリのクラスタ番号 (0 = ルートディレクトリ)
        """
        if dir_path == '/' or dir_path == '':
            return 0  # ルートディレクトリ

        # パス分解
        parts = [p for p in dir_path.split('/') if p]
        current_cluster = 0  # ルートから開始

        for i, part in enumerate(parts):
            part_path = '/' + '/'.join(parts[:i+1])
            name83 = name_to_83(part)

            # 既に作成済みか確認
            existing = self._find_dir_in_parent(current_cluster, name83)
            if existing is not None:
                current_cluster = existing
                continue

            # 新規ディレクトリ作成
            dir_cluster = self._alloc_cluster()
            fat12_set(self.fat, dir_cluster, FAT12_EOC)

            # ディレクトリデータ領域初期化
            cluster_bytes = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
            dir_data = bytearray(cluster_bytes)

            # '.' エントリ (自分自身)
            dot_entry = make_dir_entry(b'.          ', 0x10, dir_cluster, 0)
            dir_data[0:32] = dot_entry

            # '..' エントリ (親)
            dotdot_entry = make_dir_entry(b'..         ', 0x10, current_cluster, 0)
            dir_data[32:64] = dotdot_entry

            # サブディレクトリ情報を登録
            self.subdir_info[dir_cluster] = {
                'data': dir_data,
                'index': 2,  # '.' と '..' の次
            }

            # イメージにディレクトリデータを書き込み
            img_offset = self._cluster_to_offset(dir_cluster)
            self.image[img_offset:img_offset + cluster_bytes] = dir_data

            # 親ディレクトリにエントリを追加
            if current_cluster == 0:
                self._add_root_entry(name83, 0x10, dir_cluster, 0)
            else:
                self._add_subdir_entry(current_cluster, name83, 0x10, dir_cluster, 0)

            print(f"  [DIR] {part_path}/ -> cluster {dir_cluster}")
            current_cluster = dir_cluster

        return current_cluster

    def _find_dir_in_parent(self, parent_cluster, name83):
        """親ディレクトリ内からname83に一致するディレクトリを探す"""
        if parent_cluster == 0:
            # ルートディレクトリを検索
            for i in range(self.root_dir_index):
                idx = i * DIR_ENTRY_SIZE
                entry_name = bytes(self.root_dir[idx:idx + 11])
                entry_attr = self.root_dir[idx + 0x0B]
                if entry_name == name83 and (entry_attr & 0x10):
                    cluster = struct.unpack_from('<H', self.root_dir, idx + 0x1A)[0]
                    return cluster
        else:
            # サブディレクトリを検索
            if parent_cluster in self.subdir_info:
                info = self.subdir_info[parent_cluster]
                dir_data = info['data']
                for i in range(info['index']):
                    idx = i * DIR_ENTRY_SIZE
                    entry_name = bytes(dir_data[idx:idx + 11])
                    entry_attr = dir_data[idx + 0x0B]
                    if entry_name == name83 and (entry_attr & 0x10):
                        cluster = struct.unpack_from('<H', dir_data, idx + 0x1A)[0]
                        return cluster
        return None

    def add_file_tree(self, guest_path, local_path):
        """ゲストパス指定でファイルを追加 (サブディレクトリ自動作成)"""
        if not os.path.exists(local_path):
            print(f"  警告: {local_path} が見つかりません。スキップ。")
            return

        with open(local_path, 'rb') as f:
            data = f.read()

        # パスを正規化
        guest_path = guest_path.replace('\\', '/')
        if not guest_path.startswith('/'):
            guest_path = '/' + guest_path

        # ディレクトリ部分とファイル名を分離
        parts = guest_path.rsplit('/', 1)
        if len(parts) == 2 and parts[0]:
            dir_path = parts[0]
            file_name = parts[1]
        else:
            dir_path = '/'
            file_name = parts[-1]

        # ディレクトリを確保
        dir_cluster = self._ensure_directory(dir_path)

        # ファイルデータ書き込み
        first_cluster = self._write_file_data(data)

        # ディレクトリエントリ追加
        name83 = name_to_83(file_name)
        if dir_cluster == 0:
            self._add_root_entry(name83, 0x20, first_cluster, len(data))
        else:
            self._add_subdir_entry(dir_cluster, name83, 0x20, first_cluster, len(data))

        cluster_end = self.next_cluster - 1
        clusters_used = cluster_end - first_cluster + 1 if first_cluster > 0 else 0
        print(f"  {guest_path:40s} -> cls {first_cluster}-{cluster_end} "
              f"({len(data)} bytes, {clusters_used} clusters)")

    def add_file_flat(self, fat_name, local_path):
        """レガシーモード: ルートディレクトリ直下にファイルを追加"""
        if not os.path.exists(local_path):
            print(f"  警告: {local_path} が見つかりません。スキップ。")
            return

        with open(local_path, 'rb') as f:
            data = f.read()

        first_cluster = self._write_file_data(data)
        name83 = name_to_83(fat_name)
        self._add_root_entry(name83, 0x20, first_cluster, len(data))

        cluster_end = self.next_cluster - 1
        clusters_used = cluster_end - first_cluster + 1 if first_cluster > 0 else 0
        print(f"  {fat_name:12s} -> クラスタ {first_cluster}-{cluster_end} "
              f"({len(data)} bytes, {clusters_used} clusters)")

    def build(self, boot_bin=None):
        """イメージを完成させる"""
        # ブートセクタ構築
        boot_sector = bytearray(BYTES_PER_SECTOR)
        bpb = make_bpb()

        if boot_bin and os.path.exists(boot_bin):
            with open(boot_bin, 'rb') as f:
                boot_code = f.read()
            if len(boot_code) > BYTES_PER_SECTOR:
                boot_code = boot_code[:BYTES_PER_SECTOR]
            boot_sector[:len(boot_code)] = boot_code
            # BPBを上書き (ジャンプ命令はブートコード側を使用)
            boot_sector[0x03:0x24] = bpb[0x03:0x24]
        else:
            boot_sector[:len(bpb)] = bpb
            boot_sector[BYTES_PER_SECTOR - 2] = 0x55
            boot_sector[BYTES_PER_SECTOR - 1] = 0xAA

        # イメージに書き込み
        self.image[0:BYTES_PER_SECTOR] = boot_sector

        # FAT1
        fat_offset = FAT_START * BYTES_PER_SECTOR
        self.image[fat_offset:fat_offset + len(self.fat)] = self.fat

        # FAT2 (コピー)
        fat2_offset = (FAT_START + FAT_SIZE) * BYTES_PER_SECTOR
        self.image[fat2_offset:fat2_offset + len(self.fat)] = self.fat

        # ルートディレクトリ
        root_offset = ROOT_DIR_START * BYTES_PER_SECTOR
        self.image[root_offset:root_offset + len(self.root_dir)] = self.root_dir

        return bytes(self.image)

    def print_usage(self):
        """使用状況を表示"""
        used = self.next_cluster - 2
        total = TOTAL_DATA_CLUSTERS
        cluster_bytes = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
        print(f"\n  クラスタ使用: {used}/{total} "
              f"({used * cluster_bytes // 1024}KB / {total * cluster_bytes // 1024}KB, "
              f"残り {(total - used) * cluster_bytes // 1024}KB)")


def print_info():
    """FAT12パラメータ情報を表示"""
    print("FAT12 パラメータ (%s = %s):"
          % (GEOMETRY_NAME, GEOMETRIES[GEOMETRY_NAME]['label']))
    print(f"  セクタサイズ:       {BYTES_PER_SECTOR} bytes")
    print(f"  セクタ/クラスタ:    {SECTORS_PER_CLUSTER}")
    print(f"  予約セクタ:         {RESERVED_SECTORS}")
    print(f"  FAT数:              {NUM_FATS}")
    print(f"  FATサイズ:          {FAT_SIZE} sectors/FAT")
    print(f"  ルートDirエントリ:  {ROOT_ENTRY_COUNT}")
    print(f"  ルートDirセクタ:    {ROOT_DIR_SECTORS}")
    print(f"  総セクタ:           {TOTAL_SECTORS}")
    print(f"  データ開始セクタ:   {DATA_START}")
    print(f"  データクラスタ数:   {TOTAL_DATA_CLUSTERS}")
    print(f"  セクタ/トラック:    {SECTORS_PER_TRACK}")
    print(f"  ヘッド数:           {NUM_HEADS}")
    print(f"  イメージサイズ:     {TOTAL_SECTORS * BYTES_PER_SECTOR} bytes")
    cluster_bytes = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
    print(f"  利用可能容量:       {TOTAL_DATA_CLUSTERS * cluster_bytes} bytes "
          f"({TOTAL_DATA_CLUSTERS * cluster_bytes // 1024} KB)")


def main():
    parser = argparse.ArgumentParser(description='PC-98 2HD FAT12フロッピーイメージ作成')
    parser.add_argument('-o', '--output', required=True, help='出力イメージファイル')
    parser.add_argument('-b', '--boot', help='ブートセクタバイナリ (省略時はBPBのみ)')
    parser.add_argument('-d', '--d88', help='D88出力ファイル (mkd88.py連携)')
    parser.add_argument('-i', '--info', action='store_true', help='FAT12パラメータ表示')
    parser.add_argument('-g', '--geometry', choices=sorted(GEOMETRIES), default='2hd',
                        help='ジオメトリ (既定 2hd = PC-98 1232KB / 144 = 1.44MB)')
    parser.add_argument('--tree', action='store_true',
                       help='ツリーモード: /guest/path=host_path 形式でサブディレクトリ対応')
    parser.add_argument('files', nargs='*',
                       help='追加ファイル (レガシー: FAT名=ローカルパス / ツリー: /guest/path=host_path)')

    args = parser.parse_args()
    select_geometry(args.geometry)

    if args.info:
        print_info()
        return

    print("=== FAT12 イメージ作成 (%s) ===" % GEOMETRIES[args.geometry]['label'])
    print_info()
    print()

    builder = Fat12Builder()

    # ファイルリスト解析
    for spec in args.files:
        if '=' in spec:
            left, local_path = spec.split('=', 1)
        else:
            left = os.path.basename(spec).upper()
            local_path = spec

        if args.tree:
            # ツリーモード: left はゲストパス (例: /sys/shell.bin)
            builder.add_file_tree(left, local_path)
        else:
            # レガシーモード: left はFAT名 (例: SHELL.BIN)
            builder.add_file_flat(left, local_path)

    builder.print_usage()

    print(f"\nブートセクタ: {args.boot or '(BPBのみ)'}")

    # イメージ生成
    image = builder.build(boot_bin=args.boot)

    # 書き出し
    with open(args.output, 'wb') as f:
        f.write(image)
    print(f"\n出力: {args.output} ({len(image)} bytes)")

    # D88変換
    if args.d88 and args.geometry != '2hd':
        raise SystemExit(
            "mkfat12: --geometry %s では D88 を作れない。\n"
            "  1.44MB の D88 は fd_type=0x21 かつ**全セクタの rpm_flg=1** が要り、\n"
            "  tools/mkd88.py はまだ 2HD (0x20 / rpm_flg=0) しか書かない。\n"
            "  NP21/W は生イメージ (1,474,560 バイトちょうど) をサイズで 1.44MB と\n"
            "  認識するので、当面は -o の生出力を使うこと。" % args.geometry)
    if args.d88:
        mkd88_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mkd88.py')
        if os.path.exists(mkd88_path):
            import subprocess
            subprocess.run([sys.executable, mkd88_path, args.output, args.d88], check=True)
            print(f"D88:  {args.d88}")
        else:
            print(f"警告: mkd88.py が見つかりません ({mkd88_path})")


if __name__ == '__main__':
    main()
