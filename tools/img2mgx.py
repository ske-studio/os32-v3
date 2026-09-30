#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
img2mgx.py -- 画像 → MGX (漫画専用モノクロ画像形式) 変換ツール

MGX は PC-98 (OS32) 向けの漫画専用 1bpp 画像形式。
エンコードはホスト側のみ、ゲスト側はデコード (ビューワ) のみを提供する。

  入力: 任意の画像 (PNG/JPG/TIFF...)。多階調ならホスト側で縮小 + 網点化。
        既に 1bpp かつ 640x400 以下ならそのまま素通し。
  出力: 32B ヘッダ + zlib ストリーム (RFC1950) の .mgx

圧縮は PNG と同じ deflate をそのまま使う。PNG のスキャンラインフィルタ段は
1bpp では逆効果 (LZ77 が同一走査線をそのまま長いマッチとして拾えるため、
XOR フィルタがその繰り返しを壊す) と実測されたので採用していない。
詳細と実測値は docs/MGX_FORMAT.md を参照。

使い方:
  python3 tools/img2mgx.py input.png -o output.mgx
  python3 tools/img2mgx.py page.jpg -o P001.MGX --dither cluster --stats --verify

依存: Pillow, numpy (必須) / zopfli (任意、あれば 3〜5% 小さくなる)
"""

import os
import sys
import glob
import zlib
import struct
import argparse

try:
    from PIL import Image, ImageOps, ImageFilter
    import numpy as np
except ImportError:
    print("Error: Pillow and numpy are required. "
          "Install with: pip install pillow numpy", file=sys.stderr)
    sys.exit(1)

try:
    import zopfli.zlib
    HAS_ZOPFLI = True
except ImportError:
    HAS_ZOPFLI = False


# ====================================================================== #
#  1. フォーマット定数 (programs/libos32mgx/libos32mgx.h と同期させること)  #
# ====================================================================== #

MGX_MAGIC      = b"MGX1"
MGX_HDR_SIZE   = 48
MGX_VERSION    = 1
MGX_PAL_ENTRIES = 16

MGX_MAX_WIDTH  = 640
MGX_MAX_HEIGHT = 400

# codec
MGX_CODEC_STORED = 0    # 生 1bpp (無圧縮)
MGX_CODEC_ZLIB   = 1    # RFC1950 zlib ストリーム

# flags
MGX_FLAG_ROT90   = 0x02   # エンコーダが 90 度回転した

GRAY_LEVELS = 16

# dither (情報用)
MGX_DITHER_NONE    = 0
MGX_DITHER_CLUSTER = 1
MGX_DITHER_BAYER4  = 2
MGX_DITHER_BAYER8  = 3
MGX_DITHER_FS      = 4

DITHER_IDS = {
    'none':    MGX_DITHER_NONE,
    'cluster': MGX_DITHER_CLUSTER,
    'bayer4':  MGX_DITHER_BAYER4,
    'bayer8':  MGX_DITHER_BAYER8,
    'fs':      MGX_DITHER_FS,
}
DITHER_NAMES = dict((v, k) for k, v in DITHER_IDS.items())


# ====================================================================== #
#  2. ディザ行列                                                          #
# ====================================================================== #

BAYER4 = np.array([[0, 8, 2, 10],
                   [12, 4, 14, 6],
                   [3, 11, 1, 9],
                   [15, 7, 13, 5]], dtype=np.float64) / 16.0

BAYER8 = None   # 遅延生成


def _bayer8():
    """Bayer 4x4 を再帰的に 8x8 へ拡張する"""
    b = BAYER4 * 16.0
    top = np.hstack([4 * b, 4 * b + 2])
    bot = np.hstack([4 * b + 3, 4 * b + 1])
    return (np.vstack([top, bot]) + 0.5) / 256.0


def cluster_matrix(n):
    """n x n の 45 度クラスタドットスクリーン (漫画のスクリーントーンの標準角度)。

    スポット関数 f = cos(2pi x/n) + cos(2pi y/n) は 45 度格子上に極大を持つ。
    その値の順位で閾値を割り当てると、濃度に応じて 45 度に並んだ網点が育つ。

    注: 「中心からの距離」で順位を付けると 0 度の同心円ドットになり、
    絵の細部と干渉してモアレ (見た目のノイズ) が出る。45 度にするのは
    印刷の網点が伝統的に 45 度である理由と同じで、格子が目に付きにくい。
    """
    yy, xx = np.mgrid[0:n, 0:n]
    f = (np.cos(2.0 * np.pi * (xx + 0.5) / n)
         + np.cos(2.0 * np.pi * (yy + 0.5) / n))
    order = np.argsort(-f.ravel(), kind='stable')
    m = np.empty(n * n, dtype=np.float64)
    m[order] = np.arange(n * n)
    return ((m + 0.5) / (n * n)).reshape(n, n)


def halftone(gray, mode, cell=6, threshold=128):
    """グレースケール PIL 画像 -> 1 = 墨 の uint8 配列"""
    global BAYER8

    if mode == 'fs':
        # 誤差拡散は Pillow の実装をそのまま使う (mode '1' は 0=黒)
        a = np.asarray(gray.convert('1'), dtype=np.uint8)
        return (a == 0).astype(np.uint8)

    a = np.asarray(gray, dtype=np.float64) / 255.0
    h, w = a.shape

    if mode == 'none':
        return (a < (threshold / 255.0)).astype(np.uint8)

    if mode == 'cluster':
        m = cluster_matrix(cell)
    elif mode == 'bayer4':
        m = BAYER4
    elif mode == 'bayer8':
        if BAYER8 is None:
            BAYER8 = _bayer8()
        m = BAYER8
    else:
        raise ValueError("unknown dither mode %r" % mode)

    mh, mw = m.shape
    tile = np.tile(m, (h // mh + 1, w // mw + 1))[:h, :w]
    return (a < tile).astype(np.uint8)


def dither_matrix(mode, cell):
    """順序ディザ行列を返す (0..1 の閾値オフセット)"""
    global BAYER8
    if mode == 'cluster':
        return cluster_matrix(cell)
    if mode == 'bayer4':
        return BAYER4
    if mode == 'bayer8':
        if BAYER8 is None:
            BAYER8 = _bayer8()
        return BAYER8
    return None


def quantize_gray(gray, mode, cell=8, levels=GRAY_LEVELS):
    """グレースケール PIL 画像 -> 0..levels-1 の階調インデックス配列 (均等レベル)"""
    return quantize_to_levels(gray, mode, uniform_levels(levels), cell)


def uniform_levels(n):
    """0..15 を n 段に均等配置したハード階調のリスト"""
    if n <= 1:
        return [0]
    return [int(round(i * 15.0 / (n - 1))) for i in range(n)]


def quantize_to_levels(gray, mode, levels, cell=8):
    """グレースケール PIL 画像 -> 使用階調リスト levels 内のインデックス配列

    levels は 0..15 の昇順リスト (均等でなくてよい)。
    隣り合う 2 レベルの間を順序ディザで振り分ける。

    **階調数を減らすときディザは必須。** 単純に近い方へ丸めると、空などの
    ゆるいグラデーションに硬いバンド境界が走って非常に目立つ。ディザすると
    局所平均が保たれるので、同じ階調数でもトーンとして成立する。
    (PSNR はディザ版を低く評価するが、それは誤差がバンド境界に集中して
     目立つか拡散して見えないかを捉えられないため。目視では明確にディザ版が良い)
    """
    a = np.asarray(gray, dtype=np.float64) / 255.0 * 15.0
    L = np.asarray(levels, dtype=np.float64)
    if len(L) < 2:
        return np.zeros(a.shape, dtype=np.uint8)

    if mode == 'fs':
        return _fs_quantize_levels(a, L)

    h, w = a.shape
    k = np.clip(np.searchsorted(L, a, side='right') - 1, 0, len(L) - 2)
    lo, hi = L[k], L[k + 1]
    frac = np.where(hi > lo, (a - lo) / np.maximum(hi - lo, 1e-9), 0.0)

    m = dither_matrix(mode, cell)
    if m is None:                       # 'none' = ディザ無し (単純丸め)
        return (k + (frac > 0.5)).astype(np.uint8)

    mh, mw = m.shape
    t = np.tile(m, (h // mh + 1, w // mw + 1))[:h, :w]
    return (k + (frac > t)).astype(np.uint8)


def _fs_quantize_levels(a, L):
    """Floyd-Steinberg (任意レベル集合)。逐次処理なので遅い。"""
    e = a.copy()
    h, w = e.shape
    out = np.zeros((h, w), dtype=np.uint8)
    for y in range(h):
        row = e[y]
        for x in range(w):
            old = row[x]
            i = int(np.abs(L - old).argmin())
            out[y, x] = i
            err = old - L[i]
            if x + 1 < w:
                row[x + 1] += err * 7.0 / 16.0
            if y + 1 < h:
                if x > 0:
                    e[y + 1, x - 1] += err * 3.0 / 16.0
                e[y + 1, x] += err * 5.0 / 16.0
                if x + 1 < w:
                    e[y + 1, x + 1] += err * 1.0 / 16.0
    return out


def _fs_quantize(a, top):
    """後方互換用 (均等レベルの FS)"""
    return _fs_quantize_levels(a, np.asarray(uniform_levels(int(top) + 1),
                                             dtype=np.float64))


# ---------------------------------------------------------------------- #
#  ビット深度の自動判定                                                    #
# ---------------------------------------------------------------------- #

def dp_best_levels(hist, n):
    """0..15 から n 個の代表階調を選ぶ最適解と、その重み付き二乗誤差。

    1 次元 k-means を動的計画法で厳密に解く (値が 16 個しかないので一瞬)。
    局所最適に落ちる Lloyd 法と違い、必ず最良の配置が得られる。

    戻り値: (昇順の階調リスト, 二乗誤差の総和)
    """
    V = GRAY_LEVELS
    hist = np.asarray(hist, dtype=np.float64)

    # cost[j][i] = 値 j..i-1 を 1 クラスタにまとめたときの最小二乗誤差
    cost = np.full((V + 1, V + 1), np.inf)
    rep = np.zeros((V + 1, V + 1), dtype=int)
    for j in range(V):
        for i in range(j + 1, V + 1):
            vals = np.arange(j, i, dtype=np.float64)
            wt = hist[j:i]
            if wt.sum() == 0:
                cost[j][i] = 0.0
                rep[j][i] = j
                continue
            c = np.array([(wt * (vals - c0) ** 2).sum() for c0 in range(V)])
            rep[j][i] = int(c.argmin())
            cost[j][i] = float(c.min())

    dp = np.full((n + 1, V + 1), np.inf)
    back = np.zeros((n + 1, V + 1), dtype=int)
    dp[0][0] = 0.0
    for k in range(1, n + 1):
        for i in range(1, V + 1):
            for j in range(k - 1, i):
                v = dp[k - 1][j] + cost[j][i]
                if v < dp[k][i]:
                    dp[k][i] = v
                    back[k][i] = j

    lv = []
    i = V
    for k in range(n, 0, -1):
        j = back[k][i]
        lv.append(rep[j][i])
        i = j
    lv = sorted(set(int(v) for v in lv))
    while len(lv) < n:                  # 重複が出たら空き階調で埋める
        for c in range(V):
            if c not in lv:
                lv = sorted(lv + [c])
                break
    return lv[:n], float(dp[n][V])


def tone_histogram(gray):
    """前処理済みグレースケール -> 0..15 のヒストグラム"""
    a = np.asarray(gray, dtype=np.float64) / 255.0 * 15.0
    q = np.clip(np.round(a), 0, 15).astype(int)
    return np.bincount(q.ravel(), minlength=GRAY_LEVELS)


def tone_rms(hist, n):
    """n 階調に最適配置したときの階調誤差 RMS (0..15 スケール)"""
    total = float(np.asarray(hist).sum())
    if total <= 0:
        return 0.0
    _, sse = dp_best_levels(hist, n)
    return float(np.sqrt(sse / total))


def choose_bpp(gray, tolerance):
    """階調誤差 RMS が tolerance 以下になる最小の bpp を選ぶ。

    「実使用階調数を数える」方式は使えない — 前処理のアンシャープや
    JPEG ノイズが平坦性を壊し、完全に 4 階調の原稿でも 16 階調に散るため。
    階調誤差 RMS はノイズでは大きく動かないので、JPEG q75 + シャープを
    通した 4 階調原稿でも正しく bpp=2 と判定できる。

    戻り値: (bpp, [各 bpp の RMS])
    """
    hist = tone_histogram(gray)
    rms = [tone_rms(hist, 1 << n) for n in (1, 2, 3, 4)]
    for i, n in enumerate((1, 2, 3, 4)):
        if rms[i] <= tolerance:
            return n, rms
    return 4, rms


def to_gray_code(idx):
    """階調インデックス -> Gray 符号。

    隣り合う階調が 1 ビットしか違わなくなるので、プレーンごとに見たときの
    ビット反転が減り、プレーン分離データの deflate が効きやすくなる。
    デコーダ側はパレットの並べ替えで吸収するので展開時の処理は増えない。
    """
    return (idx ^ (idx >> 1)).astype(np.uint8)


def to_planes(idx, nplanes=4):
    """階調インデックス -> PC-98 プレーン (plane0..nplanes-1 を連結)

    そのまま gfx_fb.planes[] へ転送できる並びにしておく。
    nplanes を減らすと展開量も転送量もそのぶん減る。
    """
    return b''.join(np.packbits((idx >> p) & 1, axis=1).tobytes()
                    for p in range(nplanes))


# ====================================================================== #
#  3. 画像の読み込みと縮小                                                #
# ====================================================================== #

def preprocess(gray, autocontrast, gamma, sharpen, posterize=0):
    """網点化の前に階調を整える。

    1. autocontrast — 原稿が低コントラスト (空が灰色など) だと、中間調が
       そのまま最大密度のドット field になって「ノイズ」に見える。
       ハイライトを白に、シャドウを黒に寄せておく。
    2. gamma / sharpen — 線画を立てる。
    3. posterize — 階調を N 段に量子化する。**これが漫画らしさの要**。

    本物の漫画は「平坦なトーンを面に貼る」ものであって、連続的な
    グラデーションを網点化したものではない。元絵の階調をそのまま
    網点化すると画面のほぼ全面に濃度の違うドットが乗り続けてしまう。
    先に段数を減らしておくと、同じ面が 1 種類の網点密度に揃うので
    実際のトーン貼りに近づき、平坦部が増えるぶん圧縮率も上がる。
    """
    if autocontrast > 0:
        gray = ImageOps.autocontrast(gray, cutoff=autocontrast)

    if gamma is not None and abs(gamma - 1.0) > 1e-6:
        lut = [min(255, int(round(255.0 * ((i / 255.0) ** (1.0 / gamma)))))
               for i in range(256)]
        gray = gray.point(lut)

    if sharpen > 0:
        gray = gray.filter(ImageFilter.UnsharpMask(radius=1.2,
                                                   percent=sharpen,
                                                   threshold=2))

    if posterize >= 2:
        a = np.asarray(gray, dtype=np.float64) / 255.0
        q = np.round(a * (posterize - 1)) / (posterize - 1)
        gray = Image.fromarray((q * 255.0 + 0.5).astype(np.uint8))

    return gray


def load_and_prepare(path, max_w, max_h, dither, cell, gamma,
                     threshold, rotate90, no_resize,
                     autocontrast, sharpen, posterize=0, bpp='auto',
                     tone_tol=0.75):
    """入力画像 -> (階調インデックス配列, dither_id, rotated, bpp, levels, rms)

    bpp>=2 なら 0..2^bpp-1 のグレー階調、bpp=1 なら 1 = 墨 の 2 値。
    bpp='auto' なら階調誤差 RMS が tone_tol 以下になる最小の bpp を選ぶ。
    既に 1bpp かつ上限内の入力は網点化せず素通しし、bpp=1 に落とす。

    levels は「どのハード階調 (0..15) を使うか」の昇順リスト。
    パレット表にそのまま書く。
    """
    im = Image.open(path)
    was_1bpp = (im.mode == '1')

    if rotate90:
        im = im.transpose(Image.ROTATE_270)

    # --- 1bpp 素通し (階調が無いので bpp=1 に落とす) ---
    if was_1bpp and im.width <= max_w and im.height <= max_h:
        a = np.asarray(im, dtype=np.uint8)
        # 値 1 = 墨(階調 0)、値 0 = 紙(階調 15)
        return ((a == 0).astype(np.uint8), MGX_DITHER_NONE, rotate90,
                1, [15, 0], 0.0)

    if no_resize:
        if not was_1bpp:
            raise ValueError("--no-resize は 1bpp 入力にのみ使えます "
                             "(input mode=%s)" % im.mode)
        raise ValueError("--no-resize だが %dx%d が上限 %dx%d を超えています"
                         % (im.width, im.height, max_w, max_h))

    # --- 多階調: グレースケール化 -> アスペクト保持縮小 -> 網点化 ---
    gray = im.convert('L')
    scale = min(max_w / float(gray.width), max_h / float(gray.height))
    if scale < 1.0:
        nw = max(1, int(round(gray.width * scale)))
        nh = max(1, int(round(gray.height * scale)))
        gray = gray.resize((nw, nh), Image.LANCZOS)

    if bpp == 1:
        # 2 値は網点で階調を表現する (従来どおり)
        gray = preprocess(gray, autocontrast, gamma, sharpen, posterize)
        bits = halftone(gray, dither, cell=cell, threshold=threshold)
        return bits, DITHER_IDS[dither], rotate90, 1, [15, 0], 0.0

    # 多階調は posterize を使わない (階調を潰す意味が無い)
    gray = preprocess(gray, autocontrast, gamma, sharpen, 0)

    if bpp == 'auto':
        bpp, all_rms = choose_bpp(gray, tone_tol)
        if bpp == 1:
            # 2 階調まで落ちるなら網点の方が見栄えが良い
            bits = halftone(gray, dither if dither != 'bayer8' else 'cluster',
                            cell=cell, threshold=threshold)
            return bits, DITHER_IDS[dither], rotate90, 1, [15, 0], all_rms[0]

    hist = tone_histogram(gray)
    levels, sse = dp_best_levels(hist, 1 << bpp)
    idx = quantize_to_levels(gray, dither, levels, cell=cell)
    err = float(np.sqrt(sse / max(1.0, float(hist.sum()))))
    return idx, DITHER_IDS[dither], rotate90, bpp, levels, err


# ====================================================================== #
#  4. エンコード                                                          #
# ====================================================================== #

def compress_payload(raw, level=9, use_zopfli=True, iterations=15):
    """生 1bpp -> zlib ストリーム (RFC1950)。

    zopfli があれば使う。出力は普通の zlib ストリームなので
    zlib.decompress() でも展開でき、末尾に Adler-32 が付く。
    """
    best = zlib.compress(raw, level)
    if use_zopfli and HAS_ZOPFLI:
        try:
            z = zopfli.zlib.compress(raw, numiterations=iterations)
            if len(z) < len(best):
                best = z
        except Exception:
            pass
    return best


def encode(idx, dither_id, rotated, use_zopfli=True, force_stored=False,
           iterations=15, bpp=4, graycode=True, levels=None):
    """階調インデックス配列 -> MGX バイト列

    graycode: True    = Gray 符号と線形の両方を試して小さい方を採る (既定)
              'force' = Gray 符号に固定 (テストデータ生成用)
              False   = 線形に固定
    """
    h, w = idx.shape
    if w > MGX_MAX_WIDTH or h > MGX_MAX_HEIGHT:
        raise ValueError("%dx%d は上限 %dx%d を超えています"
                         % (w, h, MGX_MAX_WIDTH, MGX_MAX_HEIGHT))

    pitch = (w + 7) // 8
    npal = 1 << bpp
    if levels is None:
        levels = [15, 0] if bpp == 1 else uniform_levels(npal)
    assert len(levels) == npal, (len(levels), npal)

    pal = [0] * MGX_PAL_ENTRIES

    # 並べ替えはパレット表に畳み込む。table[プレーン値] = グレー階調。
    cand = []
    if graycode and npal > 2:
        gtab = [0] * npal
        for g in range(npal):
            gtab[g ^ (g >> 1)] = levels[g]     # 値 g^(g>>1) は levels[g]
        cand.append((gtab, to_planes(to_gray_code(idx), bpp)))
    if graycode != 'force' or npal <= 2:
        cand.append((list(levels), to_planes(idx.astype(np.uint8), bpp)))
    # 絵によっては線形の方が小さいので両方試す
    probe = sorted((len(zlib.compress(r, 6)), i)
                   for i, (_, r) in enumerate(cand))
    tab, raw = cand[probe[0][1]]
    pal[:npal] = tab
    assert len(raw) == pitch * h * bpp, (len(raw), pitch * h * bpp)

    if force_stored:
        codec, payload = MGX_CODEC_STORED, raw
    else:
        payload = compress_payload(raw, use_zopfli=use_zopfli,
                                   iterations=iterations)
        if len(payload) >= len(raw):
            codec, payload = MGX_CODEC_STORED, raw
        else:
            codec = MGX_CODEC_ZLIB

    flags = MGX_FLAG_ROT90 if rotated else 0
    hdr = (MGX_MAGIC
           + struct.pack('<BBHHBBII', MGX_VERSION, flags, w, h,
                         codec, dither_id, len(payload), len(raw))
           + struct.pack('<BB', bpp, npal)
           + b'\x00' * 10
           + bytes(v & 0x0F for v in pal))
    assert len(hdr) == MGX_HDR_SIZE, len(hdr)
    return hdr + payload, raw


# ====================================================================== #
#  5. デコード (--verify 用 / 参照実装)                                    #
# ====================================================================== #

def decode(data):
    """MGX バイト列 -> (info dict, 生 1bpp バイト列)

    ゲスト側 C デコーダ (programs/libos32mgx/) と同じ手順を踏む。
    """
    if len(data) < MGX_HDR_SIZE:
        raise ValueError("truncated header")
    if data[0:4] != MGX_MAGIC:
        raise ValueError("bad magic %r" % (bytes(data[0:4]),))

    (ver, flags, w, h, codec, dither,
     data_size, raw_size) = struct.unpack_from('<BBHHBBII', data, 4)
    bpp, npal = struct.unpack_from('<BB', data, 20)
    pal = list(data[32:48])

    if ver != MGX_VERSION:
        raise ValueError("unsupported version %d" % ver)
    if not (0 < w <= MGX_MAX_WIDTH) or not (0 < h <= MGX_MAX_HEIGHT):
        raise ValueError("bad size %dx%d" % (w, h))
    if bpp not in (1, 2, 3, 4):
        raise ValueError("bad bpp %d" % bpp)
    if npal != (1 << bpp):
        raise ValueError("npal %d inconsistent with bpp %d" % (npal, bpp))
    if raw_size != ((w + 7) // 8) * h * bpp:
        raise ValueError("raw_size %d inconsistent with %dx%d bpp=%d"
                         % (raw_size, w, h, bpp))

    payload = data[MGX_HDR_SIZE:MGX_HDR_SIZE + data_size]
    if len(payload) != data_size:
        raise ValueError("truncated payload")

    if codec == MGX_CODEC_STORED:
        raw = payload
    elif codec == MGX_CODEC_ZLIB:
        raw = zlib.decompress(payload)      # Adler-32 もここで検証される
    else:
        raise ValueError("unknown codec %d" % codec)

    if len(raw) != raw_size:
        raise ValueError("decoded %d bytes, expected %d" % (len(raw), raw_size))

    info = dict(version=ver, flags=flags, width=w, height=h, codec=codec,
                dither=dither, data_size=data_size, raw_size=raw_size,
                bpp=bpp, npal=npal, palette=pal)
    return info, raw


def packbits_size(data):
    """PackBits (programs/libos32gfx/draw/gfx_dump.c の write_packbits) 後のサイズ。

    VDP 形式との比較を出すためだけに使う。
    """
    n = len(data)
    out = 0
    i = 0
    while i < n:
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 2:
            out += 2
            i += run
        else:
            j = i + 1
            while j < n and (j + 1 >= n or data[j] != data[j + 1]) \
                    and j - i < 128:
                j += 1
            out += 1 + (j - i)
            i = j
    return out


def vdp_equivalent_size(raw):
    """同じ絵を VDP DUMP で持った場合のサイズ (256B ヘッダ + 4 プレーン PackBits)。

    墨/紙の 2 色なのでプレーン 0 だけがビットマップ、1..3 は全ゼロになる。
    """
    return 256 + packbits_size(raw) + 3 * packbits_size(bytes(len(raw)))


def raw_to_pgm(w, h, raw, info):
    """生データ -> 8bit PGM (目視確認用)"""
    pitch = (w + 7) // 8
    plane_sz = pitch * h
    idx = np.zeros((h, w), dtype=np.uint8)
    for p in range(info['bpp']):
        pl = np.frombuffer(raw[p * plane_sz:(p + 1) * plane_sz],
                           dtype=np.uint8).reshape(h, pitch)
        idx |= (np.unpackbits(pl, axis=1)[:, :w] << p)
    # パレット表 (プレーン値 -> グレー階調 0..15) をそのまま引く
    lut = np.array(info['palette'], dtype=np.uint8)
    body = (lut[idx].astype(np.float64) / 15.0 * 255.0 + 0.5).astype(np.uint8)
    return b"P5\n%d %d\n255\n" % (w, h) + body.tobytes()


# ====================================================================== #
#  6. CLI                                                                 #
# ====================================================================== #

def parse_size(s):
    try:
        w, h = s.lower().split('x')
        return int(w), int(h)
    except Exception:
        raise argparse.ArgumentTypeError("WxH 形式で指定してください (例 640x400)")


def process_one(src, dst, args):
    idx, dither_id, rotated, bpp, levels, rms = load_and_prepare(
        src, args.max_size[0], args.max_size[1], args.dither, args.tone_cell,
        args.gamma, args.threshold, args.rotate90, args.no_resize,
        args.autocontrast, args.sharpen, args.posterize, args.bpp,
        args.auto_tone_error)

    blob, raw = encode(idx, dither_id, rotated,
                       use_zopfli=not args.no_zopfli,
                       force_stored=args.stored,
                       iterations=args.zopfli_iterations,
                       bpp=bpp, graycode=not args.no_graycode,
                       levels=levels)

    with open(dst, 'wb') as f:
        f.write(blob)

    h, w = idx.shape
    if args.stats:
        codec = blob[0x0A]
        print("  %-28s %dx%d  bpp=%d (%d 階調)%s"
              % (os.path.basename(src), w, h, bpp, 1 << bpp,
                 "  [auto: 階調誤差 RMS %.2f <= %.2f]" % (rms, args.auto_tone_error)
                 if args.bpp == 'auto' else ""))
        print("     palette         : %s" % ','.join(map(str, levels)))
        print("     dither          : %s%s%s"
              % (DITHER_NAMES.get(dither_id, '?'),
                 (" cell=%d" % args.tone_cell)
                 if dither_id == MGX_DITHER_CLUSTER else "",
                 (" posterize=%d" % args.posterize)
                 if (bpp == 1 and args.posterize >= 2) else ""))
        if bpp == 1:
            print("     ink coverage    : %5.1f%%"
                  % (100.0 * float(idx.sum()) / (w * h)))
        print("     raw             : %6d B (%d プレーン)" % (len(raw), bpp))
        print("     mgx total       : %6d B  (%.1f%% of raw, codec=%s)"
              % (len(blob), 100.0 * len(blob) / len(raw),
                 'ZLIB' if codec == MGX_CODEC_ZLIB else 'STORED'))
        print("     vs zlib -9      : %6d B"
              % (MGX_HDR_SIZE + len(zlib.compress(raw, 9))))

    if args.verify:
        info, dec = decode(blob)
        if dec != raw:
            raise RuntimeError("VERIFY FAILED: %s" % src)
        if (info['width'], info['height']) != (w, h):
            raise RuntimeError("VERIFY FAILED (size): %s" % src)
        print("     verify          : OK (%d B roundtrip)" % len(dec))

    if args.dump_pgm:
        info, dec = decode(blob)
        pgm = args.dump_pgm if not args.batch else \
            os.path.splitext(dst)[0] + '.pgm'
        if pgm is True:
            pgm = os.path.splitext(dst)[0] + '.pgm'
        with open(pgm, 'wb') as f:
            f.write(raw_to_pgm(w, h, dec, info))
        print("     dump-pgm        : %s" % pgm)

    return len(raw), len(blob)


def main():
    ap = argparse.ArgumentParser(
        description='画像 → MGX (漫画専用モノクロ画像形式) 変換',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="例:\n"
               "  python3 tools/img2mgx.py page.jpg -o P001.MGX --stats --verify\n"
               "  python3 tools/img2mgx.py 'scans/*.png' -o out/ --batch\n")
    ap.add_argument('input', help='入力画像 (--batch 時は glob パターン)')
    ap.add_argument('-o', '--output', required=True,
                    help='出力 .mgx (--batch 時は出力ディレクトリ)')
    ap.add_argument('--batch', action='store_true',
                    help='input を glob として扱い、複数ページを一括変換')
    ap.add_argument('--bpp', default='auto',
                    choices=['auto', '1', '2', '3', '4'],
                    help='格納するプレーン数。auto (既定) は階調誤差 RMS が '
                         '--auto-tone-error 以下になる最小値を選ぶ。'
                         '4=16階調 3=8階調 2=4階調 1=網点2値。'
                         '小さいほどファイルもデコード時間も減る')
    ap.add_argument('--auto-tone-error', type=float, default=0.75,
                    metavar='F',
                    help='--bpp auto の閾値。0..15 スケールの階調誤差 RMS '
                         '(既定 0.75)。小さくすると高 bpp 寄りになる')
    ap.add_argument('--no-graycode', action='store_true',
                    help='bpp=4 で Gray 符号を使わない (圧縮率が落ちる)')
    ap.add_argument('--max-size', type=parse_size, default=(640, 400),
                    metavar='WxH', help='上限解像度 (既定 640x400)')
    ap.add_argument('--dither', default=None,
                    choices=['cluster', 'bayer4', 'bayer8', 'fs', 'none'],
                    help='ディザ方式 (既定: bpp=4 なら bayer8, bpp=1 なら cluster)')
    ap.add_argument('--tone-cell', type=int, default=8,
                    choices=[4, 6, 8, 10, 12],
                    help='cluster の網点セルサイズ (既定 8)')
    ap.add_argument('--autocontrast', type=int, default=2, metavar='PCT',
                    help='網点化前のオートコントラスト cutoff%% (0 で無効, 既定 2)')
    ap.add_argument('--sharpen', type=int, default=110, metavar='PCT',
                    help='網点化前のアンシャープマスク強度%% (0 で無効, 既定 110)')
    ap.add_argument('--posterize', type=int, default=3, metavar='N',
                    help='網点化前に階調を N 段へ量子化 (0/1 で無効, 既定 3)。'
                         '本物のトーン貼りに近づき、全面がドットになるのを防ぐ')
    ap.add_argument('--gamma', type=float, default=None,
                    help='網点化前のガンマ補正 (>1 で明るく)')
    ap.add_argument('--threshold', type=int, default=128,
                    help='--dither none の閾値 (既定 128)')
    ap.add_argument('--rotate90', action='store_true',
                    help='90 度回転してから収める')
    ap.add_argument('--no-resize', action='store_true',
                    help='縮小しない (1bpp かつ上限内の入力のみ)')
    ap.add_argument('--stored', action='store_true',
                    help='無圧縮で出力 (デバッグ用)')
    ap.add_argument('--no-zopfli', action='store_true',
                    help='zopfli を使わず zlib -9 のみ')
    ap.add_argument('--zopfli-iterations', type=int, default=15,
                    help='zopfli の反復回数 (既定 15)')
    ap.add_argument('--stats', action='store_true', help='統計を表示')
    ap.add_argument('--verify', action='store_true',
                    help='書き出した .mgx を復号して元と一致するか検証')
    ap.add_argument('--dump-pgm', nargs='?', const=True, default=None,
                    metavar='PATH', help='復号結果を PGM で書き出す')
    args = ap.parse_args()

    if args.bpp != 'auto':
        args.bpp = int(args.bpp)
    if args.dither is None:
        args.dither = 'cluster' if args.bpp == 1 else 'bayer8'

    if not HAS_ZOPFLI and not args.no_zopfli and args.stats:
        print("note: zopfli が無いので zlib -9 を使います "
              "(pip install zopfli で 3〜5%% 小さくなります)")

    try:
        if args.batch:
            srcs = sorted(glob.glob(args.input))
            if not srcs:
                print("Error: %r にマッチする入力がありません" % args.input,
                      file=sys.stderr)
                return 1
            if not os.path.isdir(args.output):
                os.makedirs(args.output, exist_ok=True)
            tot_raw = tot_out = 0
            for s in srcs:
                base = os.path.splitext(os.path.basename(s))[0] + '.MGX'
                r, o = process_one(s, os.path.join(args.output, base), args)
                tot_raw += r
                tot_out += o
            print("%d pages: %d B -> %d B (%.1f%%)"
                  % (len(srcs), tot_raw, tot_out,
                     100.0 * tot_out / max(1, tot_raw)))
        else:
            process_one(args.input, args.output, args)
    except (ValueError, RuntimeError, IOError) as e:
        print("img2mgx: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
