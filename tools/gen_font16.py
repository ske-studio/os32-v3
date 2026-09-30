#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_font16.py — TTF から OS32 の KCG ビットマップフォントを作る

旧 gen_kcg_font.py の置き換え。作り直した理由は ANK (半角) の潰れで、
IPAex 系は欧文がプロポーショナルなため 'W' (14px) や 'M' (13px) を
8px セルへ中央寄せすると左右が切り落とされ、W が A に、M が V に見えていた。

このツールは字幅をセルに合わせて畳む:
  - セルに収まる字はそのまま (自然な字形を保つ)
  - はみ出す字だけ横方向に縮めてから焼く (画数を落とさない)
縮小は「大きめに描いてから面積平均で縮める」ので、単純な間引きより
線が消えにくい。

出力は KCG キャッシュと同じ生バイナリ (.kcgfont):
  ヘッダ 16B: "KCG1" + 展開後サイズ(u32 LE) + フラグ(bit0=LZ4) + 予約
  ペイロード:
    漢字キャッシュ  94*94 * 32B   (16x16, 1bpp)
    漢字 fetched    94*94 B       (全て 0x01)
    ANK キャッシュ  256 * 16B     (8x16, 1bpp)
    ANK fetched     256 B         (全て 0x01)

使い方:
  python3 gen_font16.py <input.ttf> <output.kcgfont> [--no-lz4]
  python3 gen_font16.py <input.ttf> --preview out.png   (焼く前の見た目確認)
"""

import sys
import os
import struct
from PIL import Image, ImageFont, ImageDraw

# --- KCG のセル寸法 ---
ANK_W, ANK_H = 8, 16
KANJI_W, KANJI_H = 16, 16
KANJI_ROWS = 94      # JIS 上位 0x21-0x7E
KANJI_COLS = 94      # JIS 下位 0x21-0x7E
KANJI_COUNT = KANJI_ROWS * KANJI_COLS   # 8,836
ANK_COUNT = 256

KANJI_CACHE_SIZE   = KANJI_COUNT * 32
KANJI_FETCHED_SIZE = KANJI_COUNT
ANK_CACHE_SIZE     = ANK_COUNT * 16
ANK_FETCHED_SIZE   = ANK_COUNT
PAYLOAD_SIZE = (KANJI_CACHE_SIZE + KANJI_FETCHED_SIZE +
                ANK_CACHE_SIZE + ANK_FETCHED_SIZE)

MAGIC = b"KCG1"
FLAG_LZ4 = 0x01

# --- 焼き付けの調整値 ---
# 大きめに描いてから縮めるときの倍率。線の重なりを平均で拾うため。
SS = 4
# 2値化のしきい値 (0-255)。低いほど太る。
# tools で振って決めた値 (scratchpad の sweep 参照):
#   ANK   は s15/t60 が読みやすい。t90 以上だと細って消える字が出る。
#   漢字 は s16/t100。t70 以下は画数の多い字 (譚・資) が黒く潰れ、
#   t130 以上は明朝の細い横画が消える。
ANK_THRESHOLD   = 60
KANJI_THRESHOLD = 85
# --- 縦位置はベースラインで揃える ---
# 字ごとの ink box を基準に置くと、descender を持つ 'g' 'j' 'y' や
# 天地の低い '.' ',' '-' が字ごとにずれて、行がガタつく。
# 全部の字を「共通のベースライン行」に乗せることで揃える。
#
# size 15 のとき、ANK はベースラインから上 12px・下 4px = 16px でセルに
# ちょうど収まる (実測)。漢字は上 13px・下 2px。
# よってベースラインをセル上端から 12 行目に置けば両方が収まり、
# しかも半角と全角が同じ行に乗る。
ANK_BASELINE   = 12
KANJI_BASELINE = 13


def jis_to_unicode(hi, lo):
    """JIS X 0208 の区点 → Unicode 1文字。未定義なら None"""
    try:
        return bytes([hi | 0x80, lo | 0x80]).decode('euc_jp')
    except (UnicodeDecodeError, ValueError):
        return None


def ank_to_unicode(code):
    """ANK コード → Unicode 1文字。未定義なら None"""
    if 0x20 <= code <= 0x7E:
        return chr(code)
    if 0xA1 <= code <= 0xDF:
        return chr(0xFF61 + (code - 0xA1))   # 半角カタカナ
    return None


def pack_1bpp(img, cell_w, cell_h, threshold):
    """グレースケール画像を 1bpp パック (MSB = 左端) にする"""
    px = img.load()
    out = bytearray()
    for y in range(cell_h):
        for bx in range(cell_w // 8):
            b = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < cell_w and px[x, y] >= threshold:
                    b |= (0x80 >> bit)
            out.append(b)
    return bytes(out)


def render_fitted(font, ch, cell_w, cell_h, threshold, baseline):
    """1文字をベースラインに乗せてセルに焼き、1bpp で返す。

    縦位置は必ず baseline 行を基準にする。字ごとの ink box で置くと
    'g' や '.' が上下にばらついて行がガタつくため。
    横はセル幅からはみ出す字だけ縮める (画数を残すため)。
    """
    empty = bytes(cell_h * (cell_w // 8))
    if ch is None:
        return empty

    # anchor='ls' = 原点が「左・ベースライン」。y が負なら baseline より上。
    bbox = font.getbbox(ch, anchor='ls')
    if bbox is None:
        return empty
    gw = bbox[2] - bbox[0]
    gh = bbox[3] - bbox[1]
    if gw <= 0 or gh <= 0:
        return empty      # 空白文字

    # 1. 字面ちょうどの画布へ描く (1px の余白は縁のアンチエイリアス用)
    big = Image.new('L', (gw + 2, gh + 2), 0)
    ImageDraw.Draw(big).text((-bbox[0] + 1, -bbox[1] + 1), ch,
                             font=font, fill=255, anchor='ls')
    src = big.crop((1, 1, 1 + gw, 1 + gh))

    # 2. 横がはみ出す字だけ畳む。縦は縮めない
    #    (縮めるとベースラインが合わなくなり、揃える意味がなくなる)
    if gw > cell_w:
        src = src.resize((cell_w, gh), Image.LANCZOS)

    # 3. ベースラインを基準にセルへ置く
    cell = Image.new('L', (cell_w, cell_h), 0)
    x = (cell_w - src.width) // 2
    y = baseline + bbox[1]        # bbox[1] は baseline からの相対 (負)

    # セルからはみ出す分は切る (j の足など、ごく一部の字だけ)
    sx0 = sy0 = 0
    sx1, sy1 = src.width, src.height
    if y < 0:
        sy0 = -y
        y = 0
    if y + (sy1 - sy0) > cell_h:
        sy1 = sy0 + (cell_h - y)
    if x < 0:
        sx0 = -x
        x = 0
    if sy1 <= sy0 or sx1 <= sx0:
        return empty
    cell.paste(src.crop((sx0, sy0, sx1, sy1)), (x, y))

    return pack_1bpp(cell, cell_w, cell_h, threshold)


def build_ank(font):
    """ANK 256 文字 (8x16)"""
    cache = bytearray()
    n = 0
    for code in range(256):
        ch = ank_to_unicode(code)
        bmp = render_fitted(font, ch, ANK_W, ANK_H, ANK_THRESHOLD,
                            ANK_BASELINE)
        cache.extend(bmp)
        if ch is not None and any(bmp):
            n += 1
    return bytes(cache), n


def build_kanji(font):
    """漢字 94x94 面 (16x16)"""
    cache = bytearray()
    n = 0
    for hi in range(0x21, 0x7F):
        for lo in range(0x21, 0x7F):
            ch = jis_to_unicode(hi, lo)
            bmp = render_fitted(font, ch, KANJI_W, KANJI_H, KANJI_THRESHOLD,
                                KANJI_BASELINE)
            cache.extend(bmp)
            if ch is not None and any(bmp):
                n += 1
    return bytes(cache), n


# ---------------------------------------------------------------- preview

def unpack_1bpp(data, off, cell_w, cell_h):
    """1bpp セルを (w,h) の 0/1 リストへ"""
    rows = []
    stride = cell_w // 8
    for y in range(cell_h):
        row = []
        for bx in range(stride):
            b = data[off + y * stride + bx]
            for bit in range(8):
                row.append(1 if (b & (0x80 >> bit)) else 0)
        rows.append(row)
    return rows


def preview(ank_cache, kanji_cache, out_png, scale=3):
    """焼き上がりを PNG にする。実機と同じ 1bpp を拡大表示するだけなので、
       ここで読めれば実機でも読める"""
    lines_ank = [
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789 !\"#$%&'()*+,-./",
        ":;<=>?@[\\]^_`{|}~",
        # ベースラインが揃っているかの確認用。
        # descender (gjpqy) と天地の低い記号 (.,_-) が同じ行で
        # 暴れていないかを見る
        "gjpqy.,_-'\"^~ HxHgHpH|H",
        "Weeks Magic MONDAY Wizard",
        "W1 SUN TURN 1  Lv1 HP 50/50",
        "1: Attack  2: Heavy Attack",
        "DUNGEON B4 Loot 460 G",
    ]
    kanji_lines = [
        "対戦双六冒険譚",
        "村を統治し資産王を目指せ",
        "言霊　装備　城　迷宮　神社",
        "月火水木金土日　週　手番",
    ]

    cols = max(max(len(s) for s in lines_ank),
               max(len(s) for s in kanji_lines) * 2)
    w = cols * ANK_W
    h = (len(lines_ank) * ANK_H) + (len(kanji_lines) * KANJI_H) + 8
    img = Image.new('L', (w, h), 0)
    px = img.load()

    y0 = 0
    for line in lines_ank:
        for i, ch in enumerate(line):
            code = ord(ch)
            if code > 0xFF:
                continue
            rows = unpack_1bpp(ank_cache, code * 16, ANK_W, ANK_H)
            for yy, row in enumerate(rows):
                for xx, on in enumerate(row):
                    if on:
                        px[i * ANK_W + xx, y0 + yy] = 255
        y0 += ANK_H

    y0 += 8
    for line in kanji_lines:
        for i, ch in enumerate(line):
            try:
                euc = ch.encode('euc_jp')
            except UnicodeEncodeError:
                continue
            if len(euc) != 2:
                continue
            hi, lo = euc[0] & 0x7F, euc[1] & 0x7F
            idx = (hi - 0x21) * KANJI_COLS + (lo - 0x21)
            if idx < 0 or idx >= KANJI_COUNT:
                continue
            rows = unpack_1bpp(kanji_cache, idx * 32, KANJI_W, KANJI_H)
            for yy, row in enumerate(rows):
                for xx, on in enumerate(row):
                    if on:
                        px[i * KANJI_W + xx, y0 + yy] = 255
        y0 += KANJI_H

    # 実寸 (1x) を上に、拡大を下に並べる。
    # 実機は 640x400 の等倍なので、1x で読めるかが唯一の判断基準。
    big = img.resize((w * scale, h * scale), Image.NEAREST)
    sheet = Image.new('L', (max(w, big.width), h + 8 + big.height), 0)
    sheet.paste(img, (0, 0))
    sheet.paste(big, (0, h + 8))
    sheet.save(out_png)
    print(f"プレビュー: {out_png}  (上=実寸 {w}x{h} / 下={scale} 倍)")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    ttf_path = sys.argv[1]
    if not os.path.exists(ttf_path):
        print(f"Error: {ttf_path} not found")
        sys.exit(1)

    # 漢字は 16px 版面いっぱい、ANK は大文字が 11px になる寸法。
    # IPAex 明朝は仮想ボディに対して字面が小さめなので、
    # セル寸法よりわずかに大きい値を入れて版面を埋める。
    kanji_font = ImageFont.truetype(ttf_path, 16)
    ank_font = ImageFont.truetype(ttf_path, 15)

    print(f"入力: {ttf_path}")
    print("ANK レンダリング中 (256)...")
    ank_cache, ank_n = build_ank(ank_font)
    print(f"  {ank_n} 文字")

    print(f"漢字レンダリング中 ({KANJI_COUNT})...")
    kanji_cache, kanji_n = build_kanji(kanji_font)
    print(f"  {kanji_n} 文字")

    if sys.argv[2] == '--preview':
        out_png = sys.argv[3] if len(sys.argv) > 3 else 'font_preview.png'
        preview(ank_cache, kanji_cache, out_png)
        return

    out_path = sys.argv[2]
    use_lz4 = "--no-lz4" not in sys.argv

    payload = bytearray()
    payload.extend(kanji_cache)
    payload.extend(b'\x01' * KANJI_FETCHED_SIZE)
    payload.extend(ank_cache)
    payload.extend(b'\x01' * ANK_FETCHED_SIZE)
    assert len(payload) == PAYLOAD_SIZE, (len(payload), PAYLOAD_SIZE)

    flags = 0
    body = bytes(payload)
    if use_lz4:
        try:
            import lz4.block
            body = lz4.block.compress(body, store_size=False)
            flags |= FLAG_LZ4
            print(f"LZ4: {PAYLOAD_SIZE} -> {len(body)} bytes")
        except ImportError:
            print("警告: lz4 が無いので無圧縮で書き出す")

    with open(out_path, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<I', PAYLOAD_SIZE))
        f.write(struct.pack('<I', flags))
        f.write(struct.pack('<I', 0))
        f.write(body)

    print(f"出力: {out_path}  ({16 + len(body)} bytes)")


if __name__ == '__main__':
    main()
