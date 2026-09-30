#!/bin/bash
# ======================================================================
# create_fat12_d88.sh — FAT12フォーマットのD88テスト用フロッピーイメージ作成
#
# PC-98 2HD: 77cyl × 2head × 8sect × 1024B = 1,261,568 B
# ======================================================================

set -e

IMGFILE="/tmp/fat12_test.img"
D88FILE="/mnt/c/WATCOM/src/os32/fat12_test.d88"
MOUNTPOINT="/tmp/fat12_mnt"

echo "=== FAT12テストフロッピー作成 ==="

# 1. RAWイメージ作成 (1,261,568 bytes = 1232 × 1024)
echo "[1] RAWイメージ作成..."
dd if=/dev/zero of=${IMGFILE} bs=1024 count=1232 2>/dev/null

# 2. FAT12でフォーマット
# PC-98 2HD: セクタサイズ1024, 8セクタ/トラック, 2ヘッド, 77シリンダ
echo "[2] FAT12フォーマット..."
mkfs.fat -F 12 -S 1024 -s 1 -R 1 -f 2 -r 64 -n "OS32TEST" ${IMGFILE}

# 3. テストファイルを書き込み (mtoolsを使用)
echo "[3] テストファイル書き込み..."

# mtoolsの設定
export MTOOLS_SKIP_CHECK=1
export MTOOLS_FAT_COMPATIBILITY=1

# mtools用一時設定ファイル
MTOOLSRC="/tmp/mtoolsrc_fat12"
cat > ${MTOOLSRC} << EOF
drive a:
    file="${IMGFILE}"
    mformat_only
EOF
export MTOOLSRC

# テストファイルを作成
echo "Hello from FAT12 floppy!" > /tmp/fat12_hello.txt
echo -e "こんにちは、FAT12！\nUTF-8日本語テスト" > /tmp/fat12_jp.txt
echo "This is a test file for OS32 FAT12 driver." > /tmp/fat12_test.txt

# ファイルをコピー
mcopy -i ${IMGFILE} /tmp/fat12_hello.txt ::HELLO.TXT
mcopy -i ${IMGFILE} /tmp/fat12_jp.txt ::JP.TXT
mcopy -i ${IMGFILE} /tmp/fat12_test.txt ::TEST.TXT

echo "  ファイル一覧:"
mdir -i ${IMGFILE} ::

# 4. D88形式に変換
echo "[4] D88形式に変換..."
python3 /mnt/c/WATCOM/src/os32/mkd88.py ${IMGFILE} ${D88FILE}

echo ""
echo "=== 完了 ==="
echo "D88ファイル: ${D88FILE}"
echo "NP21/WのFDD2にセットして \"fat mount\" → \"fat ls\" でテスト"
