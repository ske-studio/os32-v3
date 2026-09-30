#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_kcg_font.py — TTF → KCGキャッシュ形式 1bitビットマップ変換ツール

TTFフォントを OS32 の KCG フォントキャッシュと同一フォーマットの
1bit ビットマップバイナリに事前レンダリングする。

出力フォーマット (.kcgfont):
  ヘッダ (16バイト):
    [0:4]   マジック "KCG1"
    [4:8]   展開後サイズ (u32 LE)
    [8:12]  フラグ (bit0: LZ4圧縮)
    [12:16] 予約 (0)
  ペイロード (LZ4圧縮可):
    [0         .. 282751]  漢字キャッシュ (94*94 * 32 bytes)
    [282752    .. 291587]  漢字fetched   (94*94 bytes, all 0x01)
    [291588    .. 295683]  ANKキャッシュ  (256 * 16 bytes)
    [295684    .. 295939]  ANK fetched   (256 bytes, all 0x01)
    合計: 295,940 bytes

使い方:
  python3 gen_kcg_font.py <input.ttf> <output.kcgfont> [--no-lz4]
"""

import sys
import os
import struct
from PIL import Image, ImageFont, ImageDraw

# KCG定数
ANK_W, ANK_H = 8, 16
KANJI_W, KANJI_H = 16, 16
KANJI_ROWS = 94    # JIS上位 0x21-0x7E
KANJI_COLS = 94    # JIS下位 0x21-0x7E
KANJI_COUNT = KANJI_ROWS * KANJI_COLS  # 8,836
ANK_COUNT = 256

# レンダリング閾値 (α値がこれ以上なら ON)
THRESHOLD = 96

# サイズ定数
KANJI_CACHE_SIZE   = KANJI_COUNT * 32   # 282,752
KANJI_FETCHED_SIZE = KANJI_COUNT        #   8,836
ANK_CACHE_SIZE     = ANK_COUNT * 16     #   4,096
ANK_FETCHED_SIZE   = ANK_COUNT          #     256
PAYLOAD_SIZE = KANJI_CACHE_SIZE + KANJI_FETCHED_SIZE + ANK_CACHE_SIZE + ANK_FETCHED_SIZE
# = 295,940

# マジック
MAGIC = b"KCG1"
FLAG_LZ4 = 0x01


def jis_to_unicode(hi, lo):
    """JIS X 0208 (ku-ten) → Unicode 文字"""
    try:
        euc_bytes = bytes([hi | 0x80, lo | 0x80])
        return euc_bytes.decode('euc_jp')
    except (UnicodeDecodeError, ValueError):
        return None


def ank_to_unicode(code):
    """ANK コード → Unicode 文字"""
    if 0x20 <= code <= 0x7E:
        return chr(code)
    elif 0xA1 <= code <= 0xDF:
        # 半角カタカナ → Unicode半角カタカナ
        return chr(0xFF61 + (code - 0xA1))
    else:
        return None


def render_glyph(font, ch, cell_w, cell_h):
    """1文字をレンダリングして 1bit パックバイトを返す"""
    img = Image.new('L', (cell_w, cell_h), 0)
    draw = ImageDraw.Draw(img)

    if ch is None:
        # 未定義文字は空白
        return bytes(cell_h * (cell_w // 8))

    # 文字の描画位置を計算 (中央寄せ)
    bbox = font.getbbox(ch)
    if bbox is None:
        return bytes(cell_h * (cell_w // 8))

    char_w = bbox[2] - bbox[0]
    char_h = bbox[3] - bbox[1]

    # セル内で中央に配置
    x = (cell_w - char_w) // 2 - bbox[0]
    y = (cell_h - char_h) // 2 - bbox[1]

    draw.text((x, y), ch, font=font, fill=255)

    # 1bit パック変換 (MSB = 左端)
    pixels = img.load()
    result = bytearray()

    for row in range(cell_h):
        for byte_col in range(cell_w // 8):
            byte_val = 0
            for bit in range(8):
                col = byte_col * 8 + bit
                if col < cell_w and pixels[col, row] >= THRESHOLD:
                    byte_val |= (0x80 >> bit)
            result.append(byte_val)

    return bytes(result)


def main():
    if len(sys.argv) < 3:
        print("Usage: gen_kcg_font.py <input.ttf> <output.kcgfont> [--no-lz4]")
        sys.exit(1)

    ttf_path = sys.argv[1]
    out_path = sys.argv[2]
    use_lz4 = "--no-lz4" not in sys.argv

    if not os.path.exists(ttf_path):
        print(f"Error: {ttf_path} not found")
        sys.exit(1)

    print(f"入力: {ttf_path}")
    print(f"出力: {out_path}")
    print(f"LZ4圧縮: {'有効' if use_lz4 else '無効'}")
    print(f"閾値: {THRESHOLD}")

    # フォント読み込み (漢字用: 15px, ANK用: 14px)
    kanji_font = ImageFont.truetype(ttf_path, 15)
    ank_font = ImageFont.truetype(ttf_path, 14)

    # === 漢字キャッシュ生成 (16x16, 32バイト/字) ===
    print(f"漢字レンダリング中 ({KANJI_COUNT} 文字)...")
    kanji_cache = bytearray()
    rendered_kanji = 0

    for hi in range(0x21, 0x7F):
        for lo in range(0x21, 0x7F):
            ch = jis_to_unicode(hi, lo)
            bitmap = render_glyph(kanji_font, ch, KANJI_W, KANJI_H)
            kanji_cache.extend(bitmap)
            if ch is not None:
                rendered_kanji += 1

        # 進捗表示
        pct = ((hi - 0x21 + 1) * 100) // KANJI_ROWS
        print(f"\r  JIS行 {hi:02X}/{0x7E:02X} ({pct}%) - {rendered_kanji} 文字", end="")

    print(f"\n  漢字: {rendered_kanji} 文字レンダリング完了")

    # 漢字 fetched フラグ (全て 0x01)
    kanji_fetched = bytes([0x01] * KANJI_FETCHED_SIZE)

    # === ANKキャッシュ生成 (8x16, 16バイト/字) ===
    print(f"ANKレンダリング中 ({ANK_COUNT} 文字)...")
    ank_cache = bytearray()
    rendered_ank = 0

    for code in range(ANK_COUNT):
        ch = ank_to_unicode(code)
        bitmap = render_glyph(ank_font, ch, ANK_W, ANK_H)
        ank_cache.extend(bitmap)
        if ch is not None:
            rendered_ank += 1

    print(f"  ANK: {rendered_ank} 文字レンダリング完了")

    # ANK fetched フラグ (全て 0x01)
    ank_fetched = bytes([0x01] * ANK_FETCHED_SIZE)

    # === ペイロード組み立て (キャッシュメモリレイアウトと同一) ===
    payload = bytes(kanji_cache) + kanji_fetched + bytes(ank_cache) + ank_fetched
    assert len(payload) == PAYLOAD_SIZE, f"Payload size mismatch: {len(payload)} != {PAYLOAD_SIZE}"

    print(f"ペイロード: {len(payload)} bytes ({len(payload) / 1024:.1f} KB)")

    # === LZ4圧縮 ===
    if use_lz4:
        try:
            import lz4.block
            compressed = lz4.block.compress(payload, store_size=False)
            ratio = len(compressed) * 100 / len(payload)
            print(f"LZ4圧縮: {len(compressed)} bytes ({ratio:.1f}%)")
            data = compressed
            flags = FLAG_LZ4
        except ImportError:
            print("警告: lz4 モジュール未インストール。圧縮なしで出力。")
            print("  pip install lz4 でインストール可能。")
            data = payload
            flags = 0
    else:
        data = payload
        flags = 0

    # === ヘッダ + データ書き出し ===
    header = struct.pack('<4sIII', MAGIC, PAYLOAD_SIZE, flags, 0)

    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(data)

    total_size = len(header) + len(data)
    print(f"出力: {out_path} ({total_size} bytes, {total_size / 1024:.1f} KB)")
    print("完了!")


if __name__ == '__main__':
    main()
