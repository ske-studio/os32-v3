#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
img2vbz.py — 画像/SVG → VBZ ベクター変換ツール

入力:
  - 画像ファイル (PNG/JPG等) → Potrace でベクタートレース → VBZ
  - SVGファイル (.svg) → パスデータを直接パース → VBZ

出力: PC-98 16色 ベジェ曲線ベクターデータ (.VBZ)

使い方:
  python3 tools/img2vbz.py input.png -o output.vbz
  python3 tools/img2vbz.py input.svg -o output.vbz
  python3 tools/img2vbz.py input.jpg -o output.vbz --colors 8

依存: Pillow, numpy (画像モードのみ), potrace (画像モードのみ)
"""

import argparse
import struct
import subprocess
import tempfile
import os
import sys
import re
import math
import xml.etree.ElementTree as ET

# Pillow/numpy は画像モードでのみ必要
try:
    from PIL import Image
    import numpy as np
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

# ======================================================================
# Potrace バイナリパス
# ======================================================================
POTRACE_BIN = os.path.expanduser("~/opt/potrace/bin/potrace")

# ======================================================================
# VBZ フォーマット定数
# ======================================================================
VBZ_MAGIC = b"VBZ1"
VBZ_HEADER_SIZE = 128

CMD_MOVETO    = 0x00
CMD_LINETO    = 0x01
CMD_BEZIER3   = 0x02
CMD_CLOSEPATH = 0x03

FLAG_FILL   = 0x01
FLAG_STROKE = 0x02

# ======================================================================
# CSS/SVG 名前付き色テーブル (基本色)
# ======================================================================
NAMED_COLORS = {
    'black': (0,0,0), 'white': (255,255,255), 'red': (255,0,0),
    'green': (0,128,0), 'blue': (0,0,255), 'yellow': (255,255,0),
    'cyan': (0,255,255), 'magenta': (255,0,255), 'orange': (255,165,0),
    'purple': (128,0,128), 'pink': (255,192,203), 'brown': (165,42,42),
    'gray': (128,128,128), 'grey': (128,128,128),
    'lime': (0,255,0), 'navy': (0,0,128), 'teal': (0,128,128),
    'olive': (128,128,0), 'maroon': (128,0,0), 'aqua': (0,255,255),
    'silver': (192,192,192), 'fuchsia': (255,0,255),
    'darkred': (139,0,0), 'darkgreen': (0,100,0), 'darkblue': (0,0,139),
    'lightgray': (211,211,211), 'lightgrey': (211,211,211),
    'lightblue': (173,216,230), 'lightgreen': (144,238,144),
    'darkgray': (169,169,169), 'darkgrey': (169,169,169),
    'gold': (255,215,0), 'coral': (255,127,80),
    'salmon': (250,128,114), 'tomato': (255,99,71),
    'skyblue': (135,206,235), 'steelblue': (70,130,180),
    'chocolate': (210,105,30), 'sienna': (160,82,45),
    'tan': (210,180,140), 'wheat': (245,222,179),
    'ivory': (255,255,240), 'beige': (245,245,220),
    'khaki': (240,230,140), 'indigo': (75,0,130),
    'violet': (238,130,238), 'plum': (221,160,221),
    'crimson': (220,20,60), 'turquoise': (64,224,208),
    'forestgreen': (34,139,34), 'seagreen': (46,139,87),
    'limegreen': (50,205,50), 'springgreen': (0,255,127),
    'midnightblue': (25,25,112), 'royalblue': (65,105,225),
    'dodgerblue': (30,144,255), 'deepskyblue': (0,191,255),
    'cornflowerblue': (100,149,237), 'cadetblue': (95,158,160),
    'orangered': (255,69,0), 'darkorange': (255,140,0),
    'sandybrown': (244,164,96), 'rosybrown': (188,143,143),
    'firebrick': (178,34,34), 'indianred': (205,92,92),
    'peru': (205,133,63), 'burlywood': (222,184,135),
    'lemonchiffon': (255,250,205), 'lavender': (230,230,250),
    'mistyrose': (255,228,225), 'mintcream': (245,255,250),
    'aliceblue': (240,248,255), 'honeydew': (240,255,240),
    'ghostwhite': (248,248,255), 'whitesmoke': (245,245,245),
    'snow': (255,250,250), 'seashell': (255,245,238),
    'linen': (250,240,230), 'floralwhite': (255,250,240),
    'oldlace': (253,245,230), 'antiquewhite': (250,235,215),
    'papayawhip': (255,239,213), 'blanchedalmond': (255,235,205),
    'bisque': (255,228,196), 'peachpuff': (255,218,185),
    'navajowhite': (255,222,173), 'moccasin': (255,228,181),
    'cornsilk': (255,248,220), 'none': None,
    'transparent': None,
}


# ======================================================================
# PC-98 パレット変換
# ======================================================================
def rgb24_to_pc98(r, g, b):
    """24bit RGB を PC-98 の 4bit (0-15) 値に変換"""
    return (round(r * 15.0 / 255.0),
            round(g * 15.0 / 255.0),
            round(b * 15.0 / 255.0))

# ======================================================================
# 2D アフィン変換行列 (SVG transform 対応)
# ======================================================================
# 行列表現: (a, b, c, d, e, f)
# | a c e |   | x |   | ax + cy + e |
# | b d f | × | y | = | bx + dy + f |
# | 0 0 1 |   | 1 |   |      1      |

def mat_identity():
    """単位行列"""
    return (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mat_multiply(m1, m2):
    """行列の積 m1 × m2"""
    a1, b1, c1, d1, e1, f1 = m1
    a2, b2, c2, d2, e2, f2 = m2
    return (
        a1*a2 + c1*b2,
        b1*a2 + d1*b2,
        a1*c2 + c1*d2,
        b1*c2 + d1*d2,
        a1*e2 + c1*f2 + e1,
        b1*e2 + d1*f2 + f1
    )


def mat_translate(tx, ty):
    return (1.0, 0.0, 0.0, 1.0, tx, ty)


def mat_scale(sx, sy):
    return (sx, 0.0, 0.0, sy, 0.0, 0.0)


def mat_rotate(angle_deg, cx=0, cy=0):
    rad = math.radians(angle_deg)
    cos_a = math.cos(rad)
    sin_a = math.sin(rad)
    if cx == 0 and cy == 0:
        return (cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0)
    # rotate around (cx, cy) = translate(-cx,-cy) → rotate → translate(cx,cy)
    m = mat_translate(cx, cy)
    m = mat_multiply(m, (cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0))
    m = mat_multiply(m, mat_translate(-cx, -cy))
    return m


def mat_skewx(angle_deg):
    t = math.tan(math.radians(angle_deg))
    return (1.0, 0.0, t, 1.0, 0.0, 0.0)


def mat_skewy(angle_deg):
    t = math.tan(math.radians(angle_deg))
    return (1.0, t, 0.0, 1.0, 0.0, 0.0)


def mat_apply(m, x, y):
    """行列を座標に適用"""
    a, b, c, d, e, f = m
    return (a*x + c*y + e, b*x + d*y + f)


def parse_transform(transform_str):
    """SVG transform 属性文字列を変換行列に変換"""
    if not transform_str:
        return mat_identity()

    result = mat_identity()
    # 複数のtransform関数を順に適用
    for match in re.finditer(
            r'(translate|scale|rotate|matrix|skewX|skewY)\s*\(([^)]+)\)',
            transform_str):
        func = match.group(1)
        args = [float(x) for x in re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', match.group(2))]

        if func == 'translate':
            tx = args[0] if len(args) >= 1 else 0
            ty = args[1] if len(args) >= 2 else 0
            result = mat_multiply(result, mat_translate(tx, ty))
        elif func == 'scale':
            sx = args[0] if len(args) >= 1 else 1
            sy = args[1] if len(args) >= 2 else sx
            result = mat_multiply(result, mat_scale(sx, sy))
        elif func == 'rotate':
            angle = args[0] if len(args) >= 1 else 0
            cx = args[1] if len(args) >= 3 else 0
            cy = args[2] if len(args) >= 3 else 0
            result = mat_multiply(result, mat_rotate(angle, cx, cy))
        elif func == 'matrix':
            if len(args) >= 6:
                result = mat_multiply(result, tuple(args[:6]))
        elif func == 'skewX':
            result = mat_multiply(result, mat_skewx(args[0] if args else 0))
        elif func == 'skewY':
            result = mat_multiply(result, mat_skewy(args[0] if args else 0))

    return result


def apply_transform_to_cmds(cmds, matrix):
    """変換行列をパスコマンドの全座標に適用"""
    if matrix == mat_identity():
        return cmds

    result = []
    for cmd_type, cmd_data in cmds:
        if cmd_type == 'moveto' or cmd_type == 'lineto':
            x, y = cmd_data
            result.append((cmd_type, mat_apply(matrix, x, y)))
        elif cmd_type == 'bezier3':
            p1, p2, p3 = cmd_data
            result.append((cmd_type, (
                mat_apply(matrix, p1[0], p1[1]),
                mat_apply(matrix, p2[0], p2[1]),
                mat_apply(matrix, p3[0], p3[1])
            )))
        elif cmd_type == 'closepath':
            result.append((cmd_type, cmd_data))
    return result


# ======================================================================
# SVG色パーサー
# ======================================================================
def parse_svg_color(color_str):
    """SVGの色指定をRGBタプル (0-255) に変換。Noneは塗りなし"""
    if not color_str:
        return None
    color_str = color_str.strip().lower()

    if color_str in ('none', 'transparent', ''):
        return None

    # #RRGGBB / #RGB
    m = re.match(r'^#([0-9a-f]{6})$', color_str)
    if m:
        h = m.group(1)
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
    m = re.match(r'^#([0-9a-f]{3})$', color_str)
    if m:
        h = m.group(1)
        return (int(h[0]*2, 16), int(h[1]*2, 16), int(h[2]*2, 16))

    # rgb(r, g, b) / rgb(r%, g%, b%)
    m = re.match(r'^rgb\(\s*([\d.]+)(%?)\s*,\s*([\d.]+)(%?)\s*,\s*([\d.]+)(%?)\s*\)$', color_str)
    if m:
        r = float(m.group(1))
        g = float(m.group(3))
        b = float(m.group(5))
        if m.group(2) == '%':
            r = r * 255 / 100
            g = g * 255 / 100
            b = b * 255 / 100
        return (int(min(255, r)), int(min(255, g)), int(min(255, b)))

    # 名前付き色
    if color_str in NAMED_COLORS:
        return NAMED_COLORS[color_str]

    return None


# ======================================================================
# SVG パスデータパーサー
# ======================================================================
def tokenize_svg_path(d):
    """SVGパス文字列をトークンに分割"""
    return re.findall(
        r'[MmLlCcHhVvSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?',
        d)


def parse_svg_path(d, scale_x=1.0, scale_y=1.0, offset_x=0.0, offset_y=0.0):
    """
    SVGパスデータをパースし、VBZコマンドリストに変換。
    座標変換: x' = x * scale_x + offset_x
              y' = y * scale_y + offset_y
    """
    tokens = tokenize_svg_path(d)
    commands = []
    cx, cy = 0.0, 0.0
    sx, sy = 0.0, 0.0
    last_cp_x, last_cp_y = 0.0, 0.0  # S コマンド用
    last_cmd = ''
    i = 0

    def tx(x, y):
        return (x * scale_x + offset_x, y * scale_y + offset_y)

    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            cmd = t
            i += 1
        else:
            # 暗黙の繰り返し
            cmd = last_cmd
            if cmd == 'M':
                cmd = 'L'
            elif cmd == 'm':
                cmd = 'l'

        if cmd == 'M':
            x, y = float(tokens[i]), float(tokens[i+1]); i += 2
            commands.append(('moveto', tx(x, y)))
            cx, cy = x, y; sx, sy = x, y

        elif cmd == 'm':
            x, y = float(tokens[i]), float(tokens[i+1]); i += 2
            cx += x; cy += y
            commands.append(('moveto', tx(cx, cy)))
            sx, sy = cx, cy

        elif cmd == 'L':
            x, y = float(tokens[i]), float(tokens[i+1]); i += 2
            commands.append(('lineto', tx(x, y)))
            cx, cy = x, y

        elif cmd == 'l':
            x, y = float(tokens[i]), float(tokens[i+1]); i += 2
            cx += x; cy += y
            commands.append(('lineto', tx(cx, cy)))

        elif cmd == 'H':
            x = float(tokens[i]); i += 1
            cx = x
            commands.append(('lineto', tx(cx, cy)))

        elif cmd == 'h':
            x = float(tokens[i]); i += 1
            cx += x
            commands.append(('lineto', tx(cx, cy)))

        elif cmd == 'V':
            y = float(tokens[i]); i += 1
            cy = y
            commands.append(('lineto', tx(cx, cy)))

        elif cmd == 'v':
            y = float(tokens[i]); i += 1
            cy += y
            commands.append(('lineto', tx(cx, cy)))

        elif cmd == 'C':
            x1, y1 = float(tokens[i]), float(tokens[i+1])
            x2, y2 = float(tokens[i+2]), float(tokens[i+3])
            x, y = float(tokens[i+4]), float(tokens[i+5]); i += 6
            commands.append(('bezier3', (tx(x1, y1), tx(x2, y2), tx(x, y))))
            last_cp_x, last_cp_y = x2, y2
            cx, cy = x, y

        elif cmd == 'c':
            dx1, dy1 = float(tokens[i]), float(tokens[i+1])
            dx2, dy2 = float(tokens[i+2]), float(tokens[i+3])
            dx, dy = float(tokens[i+4]), float(tokens[i+5]); i += 6
            x1, y1 = cx + dx1, cy + dy1
            x2, y2 = cx + dx2, cy + dy2
            x, y = cx + dx, cy + dy
            commands.append(('bezier3', (tx(x1, y1), tx(x2, y2), tx(x, y))))
            last_cp_x, last_cp_y = x2, y2
            cx, cy = x, y

        elif cmd == 'S':
            # 滑らか3次ベジェ — CP1は前回CP2の反射
            rx, ry = 2*cx - last_cp_x, 2*cy - last_cp_y
            x2, y2 = float(tokens[i]), float(tokens[i+1])
            x, y = float(tokens[i+2]), float(tokens[i+3]); i += 4
            commands.append(('bezier3', (tx(rx, ry), tx(x2, y2), tx(x, y))))
            last_cp_x, last_cp_y = x2, y2
            cx, cy = x, y

        elif cmd == 's':
            rx, ry = 2*cx - last_cp_x, 2*cy - last_cp_y
            dx2, dy2 = float(tokens[i]), float(tokens[i+1])
            dx, dy = float(tokens[i+2]), float(tokens[i+3]); i += 4
            x2, y2 = cx + dx2, cy + dy2
            x, y = cx + dx, cy + dy
            commands.append(('bezier3', (tx(rx, ry), tx(x2, y2), tx(x, y))))
            last_cp_x, last_cp_y = x2, y2
            cx, cy = x, y

        elif cmd == 'Q':
            # 2次ベジェ → 3次ベジェに昇格
            qx, qy = float(tokens[i]), float(tokens[i+1])
            x, y = float(tokens[i+2]), float(tokens[i+3]); i += 4
            c1x = cx + 2.0/3.0 * (qx - cx)
            c1y = cy + 2.0/3.0 * (qy - cy)
            c2x = x + 2.0/3.0 * (qx - x)
            c2y = y + 2.0/3.0 * (qy - y)
            commands.append(('bezier3', (tx(c1x, c1y), tx(c2x, c2y), tx(x, y))))
            last_cp_x, last_cp_y = qx, qy
            cx, cy = x, y

        elif cmd == 'q':
            dqx, dqy = float(tokens[i]), float(tokens[i+1])
            dx, dy = float(tokens[i+2]), float(tokens[i+3]); i += 4
            qx, qy = cx + dqx, cy + dqy
            x, y = cx + dx, cy + dy
            c1x = cx + 2.0/3.0 * (qx - cx)
            c1y = cy + 2.0/3.0 * (qy - cy)
            c2x = x + 2.0/3.0 * (qx - x)
            c2y = y + 2.0/3.0 * (qy - y)
            commands.append(('bezier3', (tx(c1x, c1y), tx(c2x, c2y), tx(x, y))))
            last_cp_x, last_cp_y = qx, qy
            cx, cy = x, y

        elif cmd == 'A' or cmd == 'a':
            # 楕円弧 → 直線セグメントで近似
            rel = (cmd == 'a')
            rx_a = float(tokens[i]); ry_a = float(tokens[i+1])
            rotation = float(tokens[i+2])
            large = int(float(tokens[i+3])); sweep = int(float(tokens[i+4]))
            ex, ey = float(tokens[i+5]), float(tokens[i+6]); i += 7
            if rel:
                ex += cx; ey += cy
            # 簡易: 直線近似
            commands.append(('lineto', tx(ex, ey)))
            cx, cy = ex, ey

        elif cmd in ('Z', 'z'):
            commands.append(('closepath', None))
            cx, cy = sx, sy

        else:
            i += 1  # 不明トークンスキップ

        last_cmd = cmd

    return commands


# ======================================================================
# SVG基本図形→パスデータ変換
# ======================================================================
def rect_to_path(x, y, w, h, rx=0, ry=0):
    """<rect> をパスコマンドに変換"""
    if rx <= 0 and ry <= 0:
        return f"M{x},{y} L{x+w},{y} L{x+w},{y+h} L{x},{y+h} Z"
    # 角丸矩形
    if rx <= 0: rx = ry
    if ry <= 0: ry = rx
    rx = min(rx, w/2); ry = min(ry, h/2)
    return (f"M{x+rx},{y} L{x+w-rx},{y} "
            f"Q{x+w},{y} {x+w},{y+ry} L{x+w},{y+h-ry} "
            f"Q{x+w},{y+h} {x+w-rx},{y+h} L{x+rx},{y+h} "
            f"Q{x},{y+h} {x},{y+h-ry} L{x},{y+ry} "
            f"Q{x},{y} {x+rx},{y} Z")


def circle_to_path(cx, cy, r):
    """<circle> をパスコマンドに変換 (4つの3次ベジェで近似)"""
    k = 0.5522847498  # (4/3) * tan(pi/8)
    kr = k * r
    return (f"M{cx},{cy-r} "
            f"C{cx+kr},{cy-r} {cx+r},{cy-kr} {cx+r},{cy} "
            f"C{cx+r},{cy+kr} {cx+kr},{cy+r} {cx},{cy+r} "
            f"C{cx-kr},{cy+r} {cx-r},{cy+kr} {cx-r},{cy} "
            f"C{cx-r},{cy-kr} {cx-kr},{cy-r} {cx},{cy-r} Z")


def ellipse_to_path(cx, cy, rx, ry):
    """<ellipse> をパスコマンドに変換"""
    kx = 0.5522847498 * rx
    ky = 0.5522847498 * ry
    return (f"M{cx},{cy-ry} "
            f"C{cx+kx},{cy-ry} {cx+rx},{cy-ky} {cx+rx},{cy} "
            f"C{cx+rx},{cy+ky} {cx+kx},{cy+ry} {cx},{cy+ry} "
            f"C{cx-kx},{cy+ry} {cx-rx},{cy+ky} {cx-rx},{cy} "
            f"C{cx-rx},{cy-ky} {cx-kx},{cy-ry} {cx},{cy-ry} Z")


def polygon_to_path(points_str):
    """<polygon> をパスコマンドに変換"""
    pts = re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)', points_str)
    if len(pts) < 4:
        return ""
    d = f"M{pts[0]},{pts[1]}"
    for i in range(2, len(pts)-1, 2):
        d += f" L{pts[i]},{pts[i+1]}"
    d += " Z"
    return d


def polyline_to_path(points_str):
    """<polyline> をパスコマンドに変換"""
    pts = re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)', points_str)
    if len(pts) < 4:
        return ""
    d = f"M{pts[0]},{pts[1]}"
    for i in range(2, len(pts)-1, 2):
        d += f" L{pts[i]},{pts[i+1]}"
    return d


def line_to_path(x1, y1, x2, y2):
    """<line> をパスコマンドに変換"""
    return f"M{x1},{y1} L{x2},{y2}"


# ======================================================================
# SVG ファイル読み込み
# ======================================================================
def convert_svg(input_path, output_path, target_width=640, target_height=400):
    """SVGファイルをパースしてVBZに変換"""
    print(f"入力: {input_path} (SVGモード)")

    tree = ET.parse(input_path)
    root = tree.getroot()

    # SVG名前空間
    ns = ''
    m = re.match(r'\{(.+)\}', root.tag)
    if m:
        ns = m.group(1)

    def tag(name):
        return f'{{{ns}}}{name}' if ns else name

    # viewBox / width / height からスケールを計算
    vb = root.get('viewBox')
    svg_w = root.get('width', '')
    svg_h = root.get('height', '')

    if vb:
        parts = re.split(r'[\s,]+', vb.strip())
        vb_x, vb_y = float(parts[0]), float(parts[1])
        vb_w, vb_h = float(parts[2]), float(parts[3])
    else:
        vb_x, vb_y = 0, 0
        vb_w = float(re.sub(r'[^0-9.]', '', svg_w)) if svg_w else target_width
        vb_h = float(re.sub(r'[^0-9.]', '', svg_h)) if svg_h else target_height

    # アスペクト比を維持してフィット
    scale = min(target_width / vb_w, target_height / vb_h)
    offset_x = -vb_x * scale
    offset_y = -vb_y * scale
    out_w = int(vb_w * scale)
    out_h = int(vb_h * scale)

    print(f"  viewBox: ({vb_x},{vb_y}) {vb_w}x{vb_h}")
    print(f"  スケール: {scale:.3f} → {out_w}x{out_h}")

    # 色→パレットインデックスのマッピング
    palette_map = {}  # RGB → index
    palette = []

    def get_color_index(rgb):
        if rgb is None:
            return -1
        if rgb in palette_map:
            return palette_map[rgb]
        if len(palette) >= 16:
            # 最も近い色を探す
            best_idx = 0
            best_dist = float('inf')
            for idx, (pr, pg, pb) in enumerate(palette):
                pr8 = pr * 255 // 15
                pg8 = pg * 255 // 15
                pb8 = pb * 255 // 15
                d = (pr8-rgb[0])**2 + (pg8-rgb[1])**2 + (pb8-rgb[2])**2
                if d < best_dist:
                    best_dist = d
                    best_idx = idx
            palette_map[rgb] = best_idx
            return best_idx
        idx = len(palette)
        palette.append(rgb24_to_pc98(*rgb))
        palette_map[rgb] = idx
        return idx

    # SVG要素を再帰的に走査
    all_paths = []  # (color_idx, flags, path_cmds)

    def get_attr(elem, attr, inherit=None):
        """属性を取得 (style属性内も探す)"""
        val = elem.get(attr)
        if val:
            return val
        style = elem.get('style', '')
        for part in style.split(';'):
            part = part.strip()
            if ':' in part:
                k, v = part.split(':', 1)
                if k.strip() == attr:
                    return v.strip()
        return inherit

    def process_element(elem, inherited_fill=None, inherited_stroke=None,
                        parent_transform=None):
        """SVG要素を処理してパスデータを抽出"""
        if parent_transform is None:
            parent_transform = mat_identity()

        # この要素のtransformを取得して親と合成
        elem_transform = parse_transform(elem.get('transform'))
        current_transform = mat_multiply(parent_transform, elem_transform)

        # fill/stroke を継承チェーン付きで取得
        fill_str = get_attr(elem, 'fill', inherited_fill)
        stroke_str = get_attr(elem, 'stroke', inherited_stroke)

        local_tag = elem.tag.replace(f'{{{ns}}}', '') if ns else elem.tag
        path_d = None

        if local_tag == 'path':
            path_d = elem.get('d', '')
        elif local_tag == 'rect':
            x = float(elem.get('x', 0))
            y = float(elem.get('y', 0))
            w = float(elem.get('width', 0))
            h = float(elem.get('height', 0))
            rx = float(elem.get('rx', 0))
            ry = float(elem.get('ry', 0))
            if w > 0 and h > 0:
                path_d = rect_to_path(x, y, w, h, rx, ry)
        elif local_tag == 'circle':
            cx_c = float(elem.get('cx', 0))
            cy_c = float(elem.get('cy', 0))
            r = float(elem.get('r', 0))
            if r > 0:
                path_d = circle_to_path(cx_c, cy_c, r)
        elif local_tag == 'ellipse':
            cx_c = float(elem.get('cx', 0))
            cy_c = float(elem.get('cy', 0))
            rx = float(elem.get('rx', 0))
            ry = float(elem.get('ry', 0))
            if rx > 0 and ry > 0:
                path_d = ellipse_to_path(cx_c, cy_c, rx, ry)
        elif local_tag == 'polygon':
            pts = elem.get('points', '')
            if pts:
                path_d = polygon_to_path(pts)
        elif local_tag == 'polyline':
            pts = elem.get('points', '')
            if pts:
                path_d = polyline_to_path(pts)
        elif local_tag == 'line':
            x1 = float(elem.get('x1', 0))
            y1 = float(elem.get('y1', 0))
            x2 = float(elem.get('x2', 0))
            y2 = float(elem.get('y2', 0))
            path_d = line_to_path(x1, y1, x2, y2)

        # パスデータがあれば変換
        if path_d:
            # fill パス
            fill_rgb = parse_svg_color(fill_str) if fill_str else (0, 0, 0)
            fill_idx = get_color_index(fill_rgb)

            # stroke パス
            stroke_rgb = parse_svg_color(stroke_str) if stroke_str else None
            stroke_idx = get_color_index(stroke_rgb) if stroke_rgb else -1

            # パスをパース (SVG座標系)
            cmds = parse_svg_path(path_d)
            if cmds:
                # 要素のtransform + viewBoxスケーリングを適用
                vb_matrix = mat_multiply(
                    mat_translate(offset_x, offset_y),
                    mat_scale(scale, scale)
                )
                final_matrix = mat_multiply(vb_matrix, current_transform)
                cmds = apply_transform_to_cmds(cmds, final_matrix)

                subpaths = split_subpaths(cmds)
                for sp in subpaths:
                    # fill が有効なら塗りつぶしパスを出力
                    if fill_idx >= 0:
                        all_paths.append((fill_idx, FLAG_FILL, sp))
                    # stroke が有効ならアウトラインパスを出力
                    if stroke_idx >= 0:
                        all_paths.append((stroke_idx, FLAG_STROKE, sp))

        # 子要素を再帰処理
        for child in elem:
            process_element(child, fill_str, stroke_str, current_transform)

    process_element(root)

    # 未使用スロットを埋める
    while len(palette) < 16:
        palette.append((0, 0, 0))

    print(f"  パレット: {min(len(palette_map), 16)}色")
    for idx, (r, g, b) in enumerate(palette[:16]):
        if r or g or b:
            print(f"    [{idx:2d}] R={r:2d} G={g:2d} B={b:2d}")
    print(f"  合計パス数: {len(all_paths)}")

    # VBZ出力
    color_paths = []
    for color_idx, flags, cmds in all_paths:
        color_paths.append((color_idx, flags, [cmds]))

    vbz_data = pack_vbz(out_w, out_h, palette, color_paths, 0)
    with open(output_path, 'wb') as f:
        f.write(vbz_data)

    print(f"出力: {output_path} ({len(vbz_data)} bytes)")


def split_subpaths(cmds):
    """closepathでサブパスを分割"""
    subpaths = []
    current = []
    for cmd_type, cmd_data in cmds:
        current.append((cmd_type, cmd_data))
        if cmd_type == 'closepath':
            if len(current) >= 3:
                subpaths.append(current)
            current = []
    if current and len(current) >= 2:
        subpaths.append(current)
    return subpaths if subpaths else [cmds]


# ======================================================================
# 色量子化 (画像モード用)
# ======================================================================
def quantize_image(img, num_colors):
    """画像を num_colors 色に減色 (従来のMEDIANCUT方式)"""
    if img.mode == 'RGBA':
        bg = Image.new('RGB', img.size, (255, 255, 255))
        bg.paste(img, mask=img.split()[3])
        img = bg
    elif img.mode != 'RGB':
        img = img.convert('RGB')

    quantized = img.quantize(colors=num_colors, method=Image.Quantize.MEDIANCUT)
    palette_data = quantized.getpalette()

    idx_arr = np.array(quantized)
    used_indices = sorted(set(idx_arr.flatten().tolist()))
    actual_colors = min(len(used_indices), len(palette_data) // 3, num_colors)

    palette = []
    for i in range(actual_colors):
        r = palette_data[i * 3]
        g = palette_data[i * 3 + 1]
        b = palette_data[i * 3 + 2]
        palette.append(rgb24_to_pc98(r, g, b))

    while len(palette) < num_colors:
        palette.append((0, 0, 0))

    return quantized, palette


# ======================================================================
# 彩度ブースト前処理
# ======================================================================
def saturation_boost(img, factor=1.3):
    """画像の彩度を強調する。factor=1.0で変化なし、>1.0で彩度UP"""
    if factor <= 1.0:
        return img
    from PIL import ImageEnhance
    enhancer = ImageEnhance.Color(img)
    return enhancer.enhance(factor)


# ======================================================================
# Floyd-Steinberg ディザリング
# ======================================================================
def floyd_steinberg_dither(img, palette_pc98):
    """
    Floyd-Steinberg 誤差拡散ディザリング。
    元画像 (RGB) とPC-98パレットを受け取り、ディザリング済みインデックス画像を返す。
    """
    if img.mode == 'RGBA':
        bg = Image.new('RGB', img.size, (255, 255, 255))
        bg.paste(img, mask=img.split()[3])
        img = bg
    elif img.mode != 'RGB':
        img = img.convert('RGB')

    w, h = img.size
    # float配列にコピー (誤差加算のため)
    pixels = np.array(img, dtype=np.float64)  # (H, W, 3)

    # PC-98パレットを24bit RGBに展開
    pal_rgb = np.array([
        [r * 255.0 / 15.0, g * 255.0 / 15.0, b * 255.0 / 15.0]
        for r, g, b in palette_pc98
    ], dtype=np.float64)  # (K, 3)

    result = np.zeros((h, w), dtype=np.uint8)

    for y in range(h):
        for x in range(w):
            old = np.clip(pixels[y, x], 0, 255)

            # 最近傍パレット色を探す
            diff = pal_rgb - old
            dists = np.sum(diff * diff, axis=1)
            best = int(np.argmin(dists))
            result[y, x] = best

            new_color = pal_rgb[best]
            err = old - new_color

            # 誤差を隣接ピクセルに拡散
            if x + 1 < w:
                pixels[y, x + 1] += err * (7.0 / 16.0)
            if y + 1 < h:
                if x - 1 >= 0:
                    pixels[y + 1, x - 1] += err * (3.0 / 16.0)
                pixels[y + 1, x] += err * (5.0 / 16.0)
                if x + 1 < w:
                    pixels[y + 1, x + 1] += err * (1.0 / 16.0)

    # Pillow P mode画像を生成
    idx_img = Image.fromarray(result, mode='P')
    flat_palette = []
    for r, g, b in palette_pc98:
        r8 = int(round(r * 255.0 / 15.0))
        g8 = int(round(g * 255.0 / 15.0))
        b8 = int(round(b * 255.0 / 15.0))
        flat_palette.extend([r8, g8, b8])
    while len(flat_palette) < 768:
        flat_palette.extend([0, 0, 0])
    idx_img.putpalette(flat_palette)

    return idx_img


# ======================================================================
# 簡易K-means (numpy のみ、scikit-learn不要)
# ======================================================================
def kmeans_simple(pixels, k, max_iter=20):
    """
    簡易K-meansクラスタリング。
    pixels: (N, 3) のRGB配列
    k: クラスタ数
    戻り値: (centroids, labels)
      centroids: (k, 3) 代表色
      labels: (N,) 各ピクセルの所属クラスタ
    """
    n = len(pixels)
    if n == 0:
        return np.zeros((k, 3), dtype=np.float64), np.zeros(0, dtype=np.int32)
    if n <= k:
        centroids = np.zeros((k, 3), dtype=np.float64)
        centroids[:n] = pixels
        return centroids, np.arange(n, dtype=np.int32)

    # 初期セントロイド: 均等間隔サンプリング
    indices = np.linspace(0, n - 1, k, dtype=np.int64)
    centroids = pixels[indices].astype(np.float64)

    labels = np.zeros(n, dtype=np.int32)

    for iteration in range(max_iter):
        # 各ピクセルを最近傍セントロイドに割り当て
        # (N,1,3) - (1,k,3) → (N,k,3) → sum → (N,k)
        diff = pixels[:, np.newaxis, :].astype(np.float64) - centroids[np.newaxis, :, :]
        dists = np.sum(diff * diff, axis=2)
        new_labels = np.argmin(dists, axis=1).astype(np.int32)

        # 収束チェック
        if np.array_equal(new_labels, labels) and iteration > 0:
            break
        labels = new_labels

        # セントロイド更新
        for c in range(k):
            mask = (labels == c)
            if np.any(mask):
                centroids[c] = np.mean(pixels[mask].astype(np.float64), axis=0)

    return centroids, labels


# ======================================================================
# 色相バランス量子化 (Hue-Balanced Quantization)
# ======================================================================
NUM_HUE_SECTORS = 12
ACHROMATIC_SAT_THRESHOLD = 38  # 0-255 (≈15%)
MIN_GROUP_RATIO = 0.005  # 全ピクセルの0.5%以上で有効

def quantize_huebalance(img, num_colors):
    """
    色相バランス量子化。色相の多様性を基準にパレットスロットを配分し、
    面積の小さい色相も確実にパレットに含める。
    """
    if img.mode == 'RGBA':
        bg = Image.new('RGB', img.size, (255, 255, 255))
        bg.paste(img, mask=img.split()[3])
        img = bg
    elif img.mode != 'RGB':
        img = img.convert('RGB')

    rgb_array = np.array(img)  # (H, W, 3)
    h, w = rgb_array.shape[:2]
    pixels = rgb_array.reshape(-1, 3)  # (N, 3)
    total_pixels = len(pixels)

    # ---- Step 1: HSV変換 & 色相セクター分類 ----
    hsv_img = img.convert('HSV')
    hsv_array = np.array(hsv_img).reshape(-1, 3)  # H:0-255, S:0-255, V:0-255

    hue = hsv_array[:, 0].astype(np.int32)  # 0-255 (Pillow HSV)
    sat = hsv_array[:, 1]
    val = hsv_array[:, 2]

    # 無彩色マスク
    achromatic_mask = sat < ACHROMATIC_SAT_THRESHOLD
    chromatic_mask = ~achromatic_mask

    # 色相セクター割当 (Pillow H: 0-255 → 0-359°)
    hue_deg = hue * 360 // 256  # 0-359
    sector = (hue_deg * NUM_HUE_SECTORS // 360) % NUM_HUE_SECTORS

    sector_names = [
        "赤", "橙", "黄", "黄緑", "緑", "青緑",
        "シアン", "青", "青紫", "紫", "赤紫", "ピンク"
    ]

    # ---- Step 2: 有効色相グループ検出 ----
    groups = {}  # group_id -> pixel_indices
    group_names = {}

    # 無彩色グループ
    achro_indices = np.where(achromatic_mask)[0]
    if len(achro_indices) > 0:
        groups['achro'] = achro_indices
        group_names['achro'] = '無彩色'

    # 有彩色: セクターごとに集計
    min_pixels = int(total_pixels * MIN_GROUP_RATIO)
    for s in range(NUM_HUE_SECTORS):
        s_mask = chromatic_mask & (sector == s)
        s_indices = np.where(s_mask)[0]
        if len(s_indices) >= min_pixels:
            gid = f'hue_{s}'
            groups[gid] = s_indices
            group_names[gid] = sector_names[s]

    # 有効グループがなければ全てを1グループとして扱う
    if not groups:
        groups['all'] = np.arange(total_pixels)
        group_names['all'] = '全色'

    print(f"  色相グループ:")
    for gid, indices in groups.items():
        pct = 100.0 * len(indices) / total_pixels
        print(f"    {group_names[gid]:6s}: {len(indices):6d} px ({pct:5.1f}%)")

    # ---- Step 3: パレットスロット配分 ----
    MAX_ACHRO_SLOTS = 4  # 無彩色のスロット上限 (過剰配分防止)

    num_groups = len(groups)
    if num_groups >= num_colors:
        # グループ数がスロット数以上 → 各グループ1ずつ(上位スロット数分)
        sorted_groups = sorted(groups.keys(), key=lambda g: len(groups[g]), reverse=True)
        sorted_groups = sorted_groups[:num_colors]
        slots = {g: 1 for g in sorted_groups}
    else:
        # 各グループに最低1スロット
        slots = {g: 1 for g in groups}
        # 無彩色には最低2スロット (黒と白/灰)
        if 'achro' in slots and num_colors >= num_groups + 1:
            slots['achro'] = 2

        remaining = num_colors - sum(slots.values())

        if remaining > 0:
            # 残りスロットを面積比で追加配分
            total_in_groups = sum(len(groups[g]) for g in groups)
            group_ratios = [(g, len(groups[g]) / total_in_groups) for g in groups]
            group_ratios.sort(key=lambda x: x[1], reverse=True)

            # 面積比に応じて追加スロットを配分
            for g, ratio in group_ratios:
                extra = int(remaining * ratio)
                if extra > 0:
                    slots[g] += extra
                    remaining -= extra

            # 端数を最大グループに追加
            if remaining > 0:
                largest = group_ratios[0][0]
                slots[largest] += remaining

        # 無彩色グループのスロット上限キャップ
        if 'achro' in slots and slots['achro'] > MAX_ACHRO_SLOTS:
            excess = slots['achro'] - MAX_ACHRO_SLOTS
            slots['achro'] = MAX_ACHRO_SLOTS
            # 超過分を有彩色グループに再配分 (面積比で)
            chromatic_groups = [g for g in groups if g != 'achro' and g in slots]
            if chromatic_groups:
                chroma_total = sum(len(groups[g]) for g in chromatic_groups)
                for g in chromatic_groups:
                    extra = max(1, int(excess * len(groups[g]) / chroma_total))
                    if excess > 0:
                        give = min(extra, excess)
                        slots[g] += give
                        excess -= give

    print(f"  スロット配分:")
    for gid, n in slots.items():
        print(f"    {group_names[gid]:6s}: {n} スロット")

    # ---- Step 4: グループ内K-means ----
    palette_rgb = []  # 最終パレット (24bit RGB)
    pixel_labels = np.zeros(total_pixels, dtype=np.int32)  # 全ピクセルのパレットインデックス
    palette_offset = 0

    for gid in groups:
        if gid not in slots:
            continue
        k = slots[gid]
        indices = groups[gid]
        group_pixels = pixels[indices]  # (M, 3)

        if k == 1:
            # 1スロットなら平均色
            centroid = np.mean(group_pixels.astype(np.float64), axis=0)
            centroids = centroid.reshape(1, 3)
            local_labels = np.zeros(len(indices), dtype=np.int32)
        else:
            centroids, local_labels = kmeans_simple(group_pixels, k)

        # パレットに追加
        for c in range(len(centroids)):
            r, g, b = int(round(centroids[c][0])), int(round(centroids[c][1])), int(round(centroids[c][2]))
            r = max(0, min(255, r))
            g = max(0, min(255, g))
            b = max(0, min(255, b))
            palette_rgb.append((r, g, b))

        # ラベルをグローバルインデックスに変換
        for i, idx in enumerate(indices):
            pixel_labels[idx] = palette_offset + local_labels[i]

        palette_offset += k

    # 残りのグループに属さないピクセル (slots から除外されたグループ)
    # → 最近傍パレットに割り当て
    unassigned_groups = [g for g in groups if g not in slots]
    if unassigned_groups:
        pal_array = np.array(palette_rgb, dtype=np.float64)
        for gid in unassigned_groups:
            indices = groups[gid]
            group_pixels = pixels[indices].astype(np.float64)
            diff = group_pixels[:, np.newaxis, :] - pal_array[np.newaxis, :, :]
            dists = np.sum(diff * diff, axis=2)
            nearest = np.argmin(dists, axis=1)
            for i, idx in enumerate(indices):
                pixel_labels[idx] = nearest[i]

    # ---- Step 5: 全ピクセルを最近傍パレットに再マッピング ----
    # (K-meansのグループ境界付近の精度向上のため)
    pal_array = np.array(palette_rgb, dtype=np.float64)
    diff = pixels[:, np.newaxis, :].astype(np.float64) - pal_array[np.newaxis, :, :]
    dists = np.sum(diff * diff, axis=2)
    pixel_labels = np.argmin(dists, axis=1).astype(np.int32)

    # パレットをPC-98形式に変換
    palette_pc98 = []
    for r, g, b in palette_rgb:
        palette_pc98.append(rgb24_to_pc98(r, g, b))
    while len(palette_pc98) < num_colors:
        palette_pc98.append((0, 0, 0))

    # インデックス画像を生成 (Pillow P mode互換)
    idx_array_2d = pixel_labels.reshape(h, w)

    # quantized_img を生成 (Pillow Image, mode='P')
    quantized_img = Image.fromarray(idx_array_2d.astype(np.uint8), mode='P')
    flat_palette = []
    for r, g, b in palette_rgb:
        flat_palette.extend([r, g, b])
    while len(flat_palette) < 768:
        flat_palette.extend([0, 0, 0])
    quantized_img.putpalette(flat_palette)

    return quantized_img, palette_pc98


# ======================================================================
# Potrace 画像トレース (画像モード用)
# ======================================================================
def potrace_trace(binary_mask, width, height, turdsize=2):
    """2値マスクをPotraceでベクタートレースしパスリストを返す"""
    pbm_path = tempfile.mktemp(suffix='.pbm')
    svg_path = pbm_path + '.svg'

    try:
        row_bytes = (width + 7) // 8
        pbm_data = bytearray()
        pbm_data.extend(f"P4\n{width} {height}\n".encode())
        for y in range(height):
            row = bytearray(row_bytes)
            for x in range(width):
                if binary_mask[y, x]:
                    row[x >> 3] |= (0x80 >> (x & 7))
            pbm_data.extend(row)

        with open(pbm_path, 'wb') as f:
            f.write(pbm_data)

        result = subprocess.run(
            [POTRACE_BIN, '-b', 'svg', '--flat',
             '-r', '72', '-u', '1',
             '-t', str(turdsize),
             pbm_path, '-o', svg_path],
            capture_output=True, timeout=30)

        if result.returncode != 0 or not os.path.exists(svg_path):
            return []

        return parse_potrace_svg(svg_path, height)

    finally:
        for p in [pbm_path, svg_path]:
            if os.path.exists(p):
                os.unlink(p)


def parse_potrace_svg(svg_path, img_height):
    """Potrace SVGを読んでパスリストを返す"""
    paths = []
    with open(svg_path, 'r') as f:
        content = f.read()

    # transformからスケール取得
    scale_match = re.search(r'scale\(([-\d.]+),([-\d.]+)\)', content)
    abs_scale = abs(float(scale_match.group(1))) if scale_match else 1.0

    path_matches = re.findall(r'<path\s+d="([^"]+)"', content)
    for d_str in path_matches:
        sc = 1.0 / abs_scale if abs_scale > 1 else 1.0
        cmds = parse_svg_path(d_str, sc, -sc, 0, img_height)
        if cmds:
            subpaths = split_subpaths(cmds)
            paths.extend(subpaths)

    return paths


# ======================================================================
# VBZ バイナリ出力
# ======================================================================
def pack_vbz(width, height, palette, color_paths, bg_color_idx=0):
    """VBZバイナリデータを生成。color_paths = [(color_idx, flags, [path_cmds, ...]), ...]"""
    data = bytearray()

    all_paths = []
    for color_idx, flags, paths in color_paths:
        for path in paths:
            all_paths.append((color_idx, flags, path))

    # パスをバウンディングボックス面積の降順でソート (painter's algorithm)
    # 面積が大きいパス→先に描画、小さいディテール→後から上書き
    def path_bbox_area(entry):
        xs = []
        ys = []
        for cmd_type, cmd_data in entry[2]:
            if cmd_type in ('moveto', 'lineto') and cmd_data:
                xs.append(cmd_data[0])
                ys.append(cmd_data[1])
            elif cmd_type == 'bezier3' and cmd_data:
                for pt in cmd_data:
                    xs.append(pt[0])
                    ys.append(pt[1])
        if not xs:
            return 0
        return (max(xs) - min(xs)) * (max(ys) - min(ys))

    all_paths.sort(key=path_bbox_area, reverse=True)

    num_paths = len(all_paths)
    num_colors = len(palette)

    header = bytearray(VBZ_HEADER_SIZE)
    header[0:4] = VBZ_MAGIC
    struct.pack_into('<HH', header, 4, width, height)
    struct.pack_into('<H', header, 8, num_paths)
    header[0x0A] = num_colors
    header[0x0B] = bg_color_idx

    for i in range(min(num_colors, 16)):
        r, g, b = palette[i]
        header[0x0C + i * 3 + 0] = r
        header[0x0C + i * 3 + 1] = g
        header[0x0C + i * 3 + 2] = b

    data.extend(header)

    for color_idx, flags, path_cmds in all_paths:
        num_cmds = len(path_cmds)
        data.extend(struct.pack('<BBH', color_idx, flags, num_cmds))

        for cmd_type, cmd_data in path_cmds:
            if cmd_type == 'moveto':
                x, y = cmd_data
                data.append(CMD_MOVETO)
                data.extend(struct.pack('<hh', int(round(x)), int(round(y))))
            elif cmd_type == 'lineto':
                x, y = cmd_data
                data.append(CMD_LINETO)
                data.extend(struct.pack('<hh', int(round(x)), int(round(y))))
            elif cmd_type == 'bezier3':
                p1, p2, p3 = cmd_data
                data.append(CMD_BEZIER3)
                data.extend(struct.pack('<hhhhhh',
                    int(round(p1[0])), int(round(p1[1])),
                    int(round(p2[0])), int(round(p2[1])),
                    int(round(p3[0])), int(round(p3[1]))))
            elif cmd_type == 'closepath':
                data.append(CMD_CLOSEPATH)

    return bytes(data)


# ======================================================================
# 画像→VBZ 変換 (Potrace経由)
# ======================================================================
def convert_image(input_path, output_path, target_width=640, target_height=400,
                  num_colors=16, epsilon=2.0, quantize_method='huebalance',
                  sat_boost=1.3, dither=False, crop_rect=None, turdsize=2):
    """ラスター画像をPotraceでベクタートレースしてVBZに変換"""
    if not HAS_PIL:
        print("Error: 画像モードには Pillow と numpy が必要です")
        print("  pip install pillow numpy")
        sys.exit(1)

    if not os.path.exists(POTRACE_BIN):
        print(f"Error: potrace not found at {POTRACE_BIN}")
        sys.exit(1)

    print(f"入力: {input_path} (画像モード)")
    img = Image.open(input_path)
    print(f"  元サイズ: {img.size[0]}x{img.size[1]}, モード: {img.mode}")

    # クロップ (リサイズ前に適用)
    if crop_rect:
        cx, cy, cw, ch = crop_rect
        img = img.crop((cx, cy, cx + cw, cy + ch))
        print(f"  クロップ: ({cx},{cy}) {cw}x{ch}")

    orig_w, orig_h = img.size
    scale = min(target_width / orig_w, target_height / orig_h)
    new_w = int(orig_w * scale)
    new_h = int(orig_h * scale)
    img = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
    print(f"  リサイズ: {new_w}x{new_h}")

    # 彩度ブースト前処理
    if sat_boost > 1.0:
        img = saturation_boost(img, sat_boost)
        print(f"  彩度ブースト: x{sat_boost:.1f}")

    # 量子化方式選択
    print(f"  量子化方式: {quantize_method}")
    if quantize_method == 'huebalance':
        quantized, palette = quantize_huebalance(img, num_colors)
    else:
        quantized, palette = quantize_image(img, num_colors)

    print(f"  パレット: {len(palette)}色")
    for i, (r, g, b) in enumerate(palette):
        if r or g or b:
            print(f"    [{i:2d}] R={r:2d} G={g:2d} B={b:2d}")

    # Floyd-Steinbergディザリング (パレット確定後に元画像に対して適用)
    if dither:
        print("  ディザリング: Floyd-Steinberg")
        quantized = floyd_steinberg_dither(img, palette)

    idx_array = np.array(quantized)

    color_counts = [(int(np.sum(idx_array == i)), i) for i in range(len(palette))]
    color_counts.sort(reverse=True)
    bg_color = color_counts[0][1]
    print(f"  背景色: [{bg_color}] ({color_counts[0][0]} pixels)")

    color_paths = []
    total_paths = 0

    for count, color_idx in color_counts:
        if color_idx == bg_color or count < 20:
            continue
        print(f"  色[{color_idx}] Potrace実行中... ({count} px)", end="", flush=True)
        mask = (idx_array == color_idx).astype(np.uint8)
        paths = potrace_trace(mask, new_w, new_h, turdsize=turdsize)
        if paths:
            color_paths.append((color_idx, FLAG_FILL, paths))
            total_paths += len(paths)
            print(f" → {len(paths)} paths")
        else:
            print(" → 0 paths")

    print(f"  合計パス数: {total_paths}")

    vbz_data = pack_vbz(new_w, new_h, palette, color_paths, bg_color)
    with open(output_path, 'wb') as f:
        f.write(vbz_data)

    print(f"出力: {output_path} ({len(vbz_data)} bytes)")


# ======================================================================
# メイン
# ======================================================================
def main():
    parser = argparse.ArgumentParser(
        description="画像/SVG → VBZ ベクター変換 (OS32用)")
    parser.add_argument('input', help='入力ファイル (PNG/JPG/SVG)')
    parser.add_argument('-o', '--output', required=True, help='出力VBZ')
    parser.add_argument('--width', type=int, default=640, help='出力幅')
    parser.add_argument('--height', type=int, default=400, help='出力高さ')
    parser.add_argument('--colors', type=int, default=16, help='色数 (画像モード, 2-16)')
    parser.add_argument('--epsilon', type=float, default=2.0, help='簡略化閾値')
    parser.add_argument('-t', '--turdsize', type=int, default=2,
                        help='Potraceノイズ除去閾値 (デフォルト: 2, 大きいほど小パス除去)')
    parser.add_argument('--quantize', choices=['mediancut', 'huebalance'],
                        default='huebalance',
                        help='量子化方式 (デフォルト: huebalance)')
    parser.add_argument('--saturation-boost', type=float, default=1.3,
                        help='彩度ブースト倍率 (デフォルト: 1.3, 1.0で無効)')
    parser.add_argument('--dither', action='store_true',
                        help='Floyd-Steinberg ディザリングを有効化')
    parser.add_argument('--crop', type=str, default=None,
                        help='入力画像のクロップ領域 (X,Y,W,H)')
    args = parser.parse_args()

    # --crop 引数のパース
    crop_rect = None
    if args.crop:
        try:
            parts = [int(x) for x in args.crop.split(',')]
            if len(parts) != 4:
                raise ValueError
            crop_rect = tuple(parts)
        except ValueError:
            print("Error: --crop は X,Y,W,H 形式 (例: --crop 100,50,400,300)")
            sys.exit(1)

    ext = os.path.splitext(args.input)[1].lower()

    if ext == '.svg':
        # SVGモード — Pillow/Potrace不要
        convert_svg(args.input, args.output,
                    target_width=args.width,
                    target_height=args.height)
    else:
        # 画像モード — Pillow + Potrace
        if args.colors < 2 or args.colors > 16:
            print("Error: --colors は 2-16")
            sys.exit(1)
        convert_image(args.input, args.output,
                      target_width=args.width,
                      target_height=args.height,
                      num_colors=args.colors,
                      epsilon=args.epsilon,
                      quantize_method=args.quantize,
                      sat_boost=args.saturation_boost,
                      dither=args.dither,
                      crop_rect=crop_rect,
                      turdsize=args.turdsize)


if __name__ == '__main__':
    main()
