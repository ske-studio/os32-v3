"""T7b-b4 S5: libos32gui の text() を字形 engine の mask 経路で描く (設計 t7bb34 §3-1・§5)。

製品の draw.rs・font.rs・gstate.rs・clip.rs・surface.rs をそのまま組み、libos32gfx の
画素口 (gfx_pixel / gfx_fill_rect / gfx_surface_* / gfx_fb) とKAPI (mem_alloc・
font_source_*・kcg_read_*・unicode_to_jis・gfx_screen_info) を host の贋物にして、
実 font (Inconsolata・JF Dot 東雲 16) で次を確かめる (tools/tests/gui_text_host.rs):

- 画素一致: 欧文 95 字・和文・Latin-1・¥・半角カナ・欠字を、font crate の Face と
  RasterScratch で直に引いた期待画像と 1 画素ずつ比べる (PLANAR4 と PACKED8 の両方)。
- clip (画素単位、全角も)・透過・オフスクリーン全角・cell 外 0 画素。
- 幅: text() の戻り値 == measure_text()。baseline: 欧文 13・和文 14、ASCII 95 字が
  cell に収まる、KCG に落ちた字 (亜・g) の baseline が東雲・Inconsolata と揃う。
- trim 後の再生成・EV_FONT (reload) 後に旧世代の entry を描かない・KCG だけの設定・
  Engine が取れないときの全字 KCG。
- 旧 KCG 描画 (kcg_draw_utf8 / kcg_set_scale) を draw.rs が呼ばない。
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import os32api_host  # noqa: E402
from test_gui_trim import build_font_crate, font_env  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
GUI = ROOT / 'userland/rust/libos32gui/src'
DRAW = 'userland/rust/libos32gui/src/draw.rs'

# 変異 1 本 = (名前, 置き換え前, 置き換え後, RED の印)。どれも draw.rs。
MUTATIONS = [
    # b4② cell 契約: 半角 cell の右へはみ出す列を落とさない → 隣の cell を汚す。
    ('cell-clip-dropped', "    let wmask: u16 = if w >= KANJI_W { 0xFFFF } else { 0xFF00 };",
     "    let wmask: u16 = 0xFFFF;", 'cell-contract'),
    # b4③ 送り: 全角を cell 幅でなく半角で送る。
    ('pen-not-cell-width', "        penx += cw;\n        i += len;\n    }\n    if let Some(e) = eng {",
     "        penx += if len > 1 { ANK_W } else { cw };\n        i += len;\n    }\n    if let Some(e) = eng {",
     'advance'),
    # b4④ fallback baseline: KCG の漢字を 1 行下げる。
    ('kcg-y-plus-1', "    for (r, row) in rows.iter_mut().enumerate() {\n"
     "        *row = (u16::from(pat[2 * r]) << 8) | u16::from(pat[2 * r + 1]);",
     "    for (r, row) in rows.iter_mut().enumerate().skip(1) {\n"
     "        *row = (u16::from(pat[2 * r - 2]) << 8) | u16::from(pat[2 * r - 1]);",
     'fallback-baseline'),
    # b4④ 行 clip の off-by-one (PACKED8 の行の直書き口)。
    ('row-clip-off-by-one', "        if ly < self.clip.y as i32 || ly >= self.clip.bottom() {",
     "        if ly < self.clip.y as i32 || ly > self.clip.bottom() {", 'clip'),
    # b4④ baseline: TTF の字を cell の先頭から置く (ascent を無視)。
    ('ttf-ignores-ascent', "    let oy = i32::from(g.ascent) - i32::from(e.top);",
     "    let oy = 0 - i32::from(e.top) + i32::from(e.height);", 'pixel-match'),
    # §3-1 透過: 透過でも cell を塗る。
    ('transparent-fills', "    let bg = if style.has(GUI_STYLE_TRANSPARENT_BG) { None } else { Some(style.bg) };",
     "    let bg = Some(style.bg);", 'transparent'),
    # §3-1 0x5C は ROM の ¥ (Inconsolata の \ を描かない)。
    ('yen-to-ttf', "        if (0x20..=0x7E).contains(&b) && b != ANK_YEN {",
     "        if (0x20..=0x7E).contains(&b) {", 'yen'),
    # P3-4 U+00A5 も ROM の ¥ (東雲16 の U+00A5 は \ の形、S7 のゲスト受入で検出)。
    ('u00a5-to-ttf', "    if cp == CP_YEN {", "    if cp == CP_YEN && false {", 'yen-u00a5-rom'),
    # §2-4 借用は 1 字の間: release を抜く → 次の字から engine が KCG を返す。
    ('no-release', "            let rows = cell_from_glyph(&g, e.bitmap(&g), w);\n            e.release();",
     "            let rows = cell_from_glyph(&g, e.bitmap(&g), w);", 'pixel-match'),
    # §2-2.1 入口の検証: begin を呼ばない → engine を使わず全字 KCG。
    ('no-begin', "        Some(e) => e.begin(),", "        Some(_) => false,", 'pixel-match'),
]


def build_units(work):
    rlib = os32api_host.build(work)
    font_rlib, deps = build_font_crate(work)
    env = font_env(work)
    return rlib, font_rlib, deps, env


def compose(unit, draw_src):
    """製品ソースを #[path] で並べ、draw.rs だけ (変異を当てた) 写しを使う。"""
    (unit / 'draw.rs').write_text(draw_src)
    host = (ROOT / 'tools/tests/gui_text_host.rs').read_text()
    head, body = host.split('// ---- crate ----', 1)
    mods = ''.join(f'#[path="{GUI / n}.rs"] mod {n};\n' for n in ('gstate', 'clip', 'surface', 'font'))
    mods += f'#[path="{unit / "draw.rs"}"] mod draw;\n'
    (unit / 'main.rs').write_text(head + mods + body)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mutate', action='store_true')
    args = p.parse_args()
    draw = (ROOT / DRAW).read_text()
    for old in ('ffi::kcg_draw_utf8', 'ffi::kcg_set_scale', 'ffi::kcg_draw_'):
        assert old not in draw, f'draw.rs still calls {old}'
    with tempfile.TemporaryDirectory(prefix='t7b-gui-text-') as d:
        work = Path(d)
        rlib, font_rlib, deps, env = build_units(work)

        def run(m=None):
            src, name = draw, 'normal'
            if m:
                name, old, new, _ = m
                assert src.count(old) == 1, name
                src = src.replace(old, new)
            unit = work / name
            unit.mkdir()
            compose(unit, src)
            c = subprocess.run(['rustc', '--edition=2021', '--test', str(unit / 'main.rs'),
                                '--extern', 'os32api=' + str(rlib), '--extern', 'os32_font=' + str(font_rlib),
                                '-L', 'dependency=' + str(deps), '-o', str(unit / 't')],
                               capture_output=True, text=True, env=env)
            assert c.returncode == 0, c.stderr  # a compile failure never counts as RED
            return subprocess.run([str(unit / 't'), '--test-threads=1', '--nocapture'],
                                  capture_output=True, text=True, env=env)

        r = run()
        print(r.stdout, end='')
        assert r.returncode == 0, r.stdout + r.stderr
        if args.mutate:
            for m in MUTATIONS:
                r = run(m)
                assert r.returncode == 101 and m[3] in r.stdout, (m[0], r.stdout[-2000:], r.stderr[-2000:])
                print('RED ' + m[0] + ' rc=101 -> ' + m[3])
            print(f'PASS {len(MUTATIONS)}/{len(MUTATIONS)} draw.rs mutants')
    print('test_gui_text: OK')


if __name__ == '__main__':
    main()
