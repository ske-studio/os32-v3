"""T7b-a5: 16ppem 1bpp raster, 32px = 2x nearest, references and x87.

References are independent of the Rust code: Shinonome's own 16ppem EBDT
strike (the original dots; also the BDF original when present locally),
fontTools exact-rational metrics for every corpus glyph, FreeType 2 (only
when freetype-py is importable, e.g. via OS32_FREETYPE_PYTHONPATH; otherwise
a SKIP line). The x87 run compiles the product raster for i686-os32-none and
executes it on the host inside the product a2 guard (kernel/font_guard.c +
font_x87.asm, CW 037F). TASK_FONTPERF F5: the raster is integer only, so the
guest build and the host build must agree bit for bit (X87_TOLERANCE 0), and
the old float raster's dump (REF_F32, taken before F5) is compared pixel by
pixel when present (Shinonome must not change; the rest is recorded). The
final linked font_test.elf is checked by tools/check_isa386.py (acceptance 4).
"""
from fractions import Fraction
from pathlib import Path
import argparse
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

import host32
from test_font_reader import product_grep
from test_font_outline import shinonome

ROOT = Path(__file__).resolve().parents[2]
CRATE = ROOT / 'userland/lib/font'
FONTS = ROOT / 'assets/fonts'
PPEM = 16
THRESHOLD = 128          # FreeType coverage byte >= 128 is "black" for the flip count below
X87_TOLERANCE = 0        # F5 integer raster: guest code generation == host, bit for bit
# The float (pre-F5) raster's dump of the same corpora (design §6 acceptance).
REF_F32 = Path.home() / 'os32-tmp/run/fontperf/ref_f32/dump.txt'
# a1 edge model: chord error <= 1/8 px (32/255) plus FreeType's own conic split.
FREETYPE_TOLERANCE = 40
GUEST_RANGES = ((0x20, 0x7e), (0xa0, 0xff), (0x3000, 0x30ff), (0x4e00, 0x4fff))  # font_test CHECK_RANGES
BDF = Path.home() / 'os32-tmp/run/shinonome/bdf/shinonome-0.9.11/bdf/shnmk16.bdf'
# JIS X 0208 1-2-81/82/2-44 map (iso2022_jp) to U+00A2/A3/AC, where JF's cmap
# picks the half-width glyph (a1 Shinonome investigation); the BDF has full width.
BDF_CMAP_HALFWIDTH = {0xA2, 0xA3, 0xAC}
MUTATIONS = [
    # (label, file, old, new, failing test or python stage)
    ('bbox', 'raster.rs', 'if width > MAX_BBOX_PX || height > MAX_BBOX_PX',
     'if width > MAX_BBOX_PX + 1 || height > MAX_BBOX_PX + 1', 'bbox_limits'),
    ('flattened-edges', 'raster.rs', '        if self.edges > MAX_EDGES {',
     '        if self.edges > MAX_EDGES + 1 {', 'flattened_edge_limit'),
    ('dst-capacity', 'raster.rs', 'let target = dst.get_mut(..needed).ok_or(Error::Limit)?;\n        target.copy_from_slice(out);',
     'let n = needed.min(dst.len());\n        dst.get_mut(..n).ok_or(Error::Limit)?.copy_from_slice(out.get(..n).ok_or(Error::Limit)?);\n        if n < needed { return Err(Error::Limit); }',
     'destination_capacity'),
    ('early-coverage', 'raster.rs', '        self.coverage_len = 0;\n        self.edges = 0;\n',
     '', 'flattened_edge_limit'),
    ('nonzero-abs', 'raster.rs', 'let area = sum.unsigned_abs();', 'let area = sum.max(0) as u32;',
     'pixel_square_is_exact_and_doubles'),
    ('cover-threshold-64', 'limits.rs', 'COVERAGE_BLACK_NUM: u64 = 128;', 'COVERAGE_BLACK_NUM: u64 = 64;',
     'threshold_is_half_coverage_boundary'),
    ('nearest-2x', 'raster.rs', 'let bit = 0x80u8 >> ((column / factor) % 8);',
     'let bit = 0x80u8 >> (column % 8);', 'pixel_square_is_exact_and_doubles'),
    ('round-26.6', 'fixed.rs', '(value.abs() * scale + 0x8000) >> 16', '(value.abs() * scale) >> 16',
     'units_to_26_6_rounding'),
    ('short-loca-even', 'outline.rs', 'let padded = end.next_multiple_of(2);',
     'let padded = end;', 'odd_length_components_keep_even_short_loca'),
    ('i16-overflow', 'raster.rs', 'left: base.left.checked_mul(factor as i16).ok_or(Error::Bounds)?,',
     'left: base.left.wrapping_mul(factor as i16),', 'coordinate_overflow'),
    ('quad-n-fixed-1', 'raster.rs', 'let n = root4_ceil(QUAD_FLATNESS_SQ * d2, unit, MAX_EDGES as u32).ok_or(Error::Limit)?;',
     'let n = 1u32;', 'flattened_edge_limit'),
    ('cbox-on-curve-only', 'raster.rs', '        if let Some(c) = e.control {\n            visit(c)?;\n        }\n', '',
     'control_box_includes_off_curve_points'),
    ('span-split', 'raster.rs', 'if lo == hi || lo >> RASTER_SUBPIXEL_BITS == (hi - 1) >> RASTER_SUBPIXEL_BITS {', 'if true {',
     'diagonal_coverage_is_exact_area'),
    ('flip-y', 'raster.rs', 'y: (top * 64 - coord_26_6(p.y, scale)?) * per_26_6,',
     'y: (coord_26_6(p.y, scale)? - bottom * 64) * per_26_6,', 'control_box_includes_off_curve_points'),
    # Only real fonts see this one (synthetic hmtx is uniform): every Rust test
    # stays green and the corpus metric reference must catch it.
    ('advance-gid', 'raster.rs', 'i32::from(face.advance(gid)?)', 'i32::from(face.advance(gid.saturating_sub(1))?)',
     'py:metrics'),
]


def bundle_cps(jf):
    """Code points the .g16 bundle must hold (design §2), derived here independently:
    the cmap's Full-path cps (>= 0x80, not U+00A0..U+024F), gid != 0."""
    from fontTools.ttLib import TTFont
    font = TTFont(str(jf))
    order = font.getGlyphOrder()
    return sorted(cp for cp, name in font.getBestCmap().items()
                  if cp >= 0x80 and not 0xA0 <= cp <= 0x24F and order.index(name) != 0)


def corpora(work, jf):
    jis = []
    for a in range(0x21, 0x7f):
        for b in range(0x21, 0x7f):
            try:
                jis.append(ord((b'\x1b$B' + bytes([a, b]) + b'\x1b(B').decode('iso2022_jp')))
            except UnicodeDecodeError:
                pass
    assert len(jis) == 6879
    latin = list(range(0x20, 0x7f)) + list(range(0xa0, 0x100))
    # TASK_FONTPERF F1: the Shinonome corpus also covers every .g16 record, so the
    # Rust raster dump is the third reference of check_g16.
    ja = sorted(set(jis) | set(latin) | set(bundle_cps(jf)))
    paths = {}
    for name, cps in (('JIS', jis), ('LATIN', latin), ('JA', ja)):
        paths[name] = work / (name.lower() + '.txt')
        paths[name].write_text(''.join(f'{cp}\n' for cp in cps))
    return jis, latin, ja, paths


def build_run(crate, work, env, label, test=None):
    build = subprocess.run(['cargo', 'test', '--release', '--offline', '--manifest-path', str(crate / 'Cargo.toml'),
                            '--test', 'raster', '--no-run', '--message-format=json'],
                           env={**env, 'CARGO_TARGET_DIR': str(work / 'target')},
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    messages = [json.loads(line) for line in build.stdout.splitlines() if line.startswith('{')]
    diagnostics = build.stderr + ''.join(m.get('message', {}).get('rendered', '') for m in messages)
    assert build.returncode == 0, diagnostics  # compile failures are never RED
    exe = next(m['executable'] for m in messages if m.get('executable'))
    result = subprocess.run([exe, *([test, '--exact'] if test else []), '--nocapture', '--test-threads=1'],
                            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (work / (label + '.log')).write_text(diagnostics + result.stdout + result.stderr)
    return result


def parse_dump(path):
    faces, glyphs = {}, {}
    for line in path.read_text().splitlines():
        f = line.split()
        if f[0] == 'F':
            faces[f[1]] = dict(upem=int(f[2]), asc=int(f[3]), desc=int(f[4]))
        elif f[0] == 'G':
            w, h, pitch, left, top, adv = map(int, f[4:10])
            bits = bytes.fromhex(f[10]) if f[10] != '-' else b''
            cov = bytes.fromhex(f[11]) if f[11] != '-' else None
            glyphs[(f[1], int(f[2]))] = dict(gid=int(f[3]), w=w, h=h, pitch=pitch, left=left, top=top,
                                             adv=adv, bits=bits, cov=cov)
        else:
            raise AssertionError('fallback in corpus: ' + line)
    return faces, glyphs


def black(g):
    """Absolute black pixels: (x from pen, y row index upward from baseline)."""
    out = set()
    for r in range(g['h']):
        for c in range(g['w']):
            if g['bits'][r * g['pitch'] + c // 8] >> (7 - c % 8) & 1:
                out.add((g['left'] + c, g['top'] - r - 1))
    return out


def ebdt_dots(path):
    from fontTools.ttLib import TTFont
    font = TTFont(str(path))
    strike = next(i for i, s in enumerate(font['EBLC'].strikes)
                  if s.bitmapSizeTable.ppemX == PPEM and s.bitmapSizeTable.bitDepth == 1)
    metrics = {}
    for sub in font['EBLC'].strikes[strike].indexSubTables:
        for gid in range(sub.firstGlyphIndex, sub.lastGlyphIndex + 1):
            metrics[gid] = getattr(sub, 'metrics', None)
    order = font.getGlyphOrder()
    data = font['EBDT'].strikeData[strike]

    def dots(gid):
        if gid not in metrics or order[gid] not in data:
            return set()
        g = data[order[gid]]
        m = metrics[gid] or g.metrics
        bits = g.imageData
        assert g.getFormat() in (5, 7), g.getFormat() if hasattr(g, 'getFormat') else type(g)
        out = set()
        for r in range(m.height):
            for c in range(m.width):
                i = r * m.width + c
                if bits[i // 8] >> (7 - i % 8) & 1:
                    out.add((m.horiBearingX + c, m.horiBearingY - r - 1))
        return out
    return dots


def bdf_dots(path):
    glyphs, cur, rows = {}, None, None
    for line in path.read_text(encoding='latin-1').splitlines():
        if line.startswith('ENCODING'):
            cur = dict(enc=int(line.split()[1]))
        elif line.startswith('BBX') and cur is not None:
            cur['bbx'] = tuple(map(int, line.split()[1:]))
        elif line == 'BITMAP':
            rows = []
        elif line == 'ENDCHAR':
            w, h, x, y = cur['bbx']
            out = set()
            for r, row in enumerate(rows):
                v = int(row, 16)
                n = len(row) * 4
                for c in range(w):
                    if v >> (n - 1 - c) & 1:
                        out.add((x + c, y + h - r - 1))
            glyphs[cur['enc']] = out
            cur, rows = None, None
        elif rows is not None:
            rows.append(line.strip())
    return glyphs


def check_dots(jf, glyphs, ja, jis):
    dots = ebdt_dots(jf)
    exact = 0
    levels = set()
    for cp in ja:
        g = glyphs[('shinonome', cp)]
        assert black(g) == dots(g['gid']), f'Shinonome U+{cp:04X}: raster differs from the 16px EBDT dots'
        levels |= set(g['cov'] or b'')
        exact += 1
    # JF outlines sit 1/4 px off the pixel grid vertically: coverage is 0, 1/4,
    # 3/4 or 1, so the threshold decision has a wide margin (x87 cannot flip it).
    margin = min(abs(v - THRESHOLD) for v in levels)
    print(f'PASS Shinonome 16ppem raster == original 16px dots (EBDT strike) for all {exact} glyphs; '
          f'coverage levels {sorted(levels)}, threshold margin {margin}/255', flush=True)
    if not BDF.exists():
        print(f'SKIP BDF original comparison: {BDF} absent (EBDT is the in-file original)', flush=True)
        return
    bdf = bdf_dots(BDF)
    differ = set()
    for cp in jis:
        b = bytes([0x1b, 0x24, 0x42]) + chr(cp).encode('iso2022_jp')[3:5]
        code = b[3] << 8 | b[4]
        if black(glyphs[('shinonome', cp)]) != bdf[code]:
            differ.add(cp)
    assert differ == BDF_CMAP_HALFWIDTH, sorted(map(hex, differ))
    print(f'PASS Shinonome raster == BDF original shnmk16.bdf for {len(jis) - len(differ)}/{len(jis)} JIS, '
          f'only the known cmap half-width {sorted(map(hex, differ))}', flush=True)


def check_ref_f32(glyphs):
    """Design §6: pixel differences against the float raster's dump (REF_F32)."""
    if not REF_F32.exists():
        print(f'SKIP float-raster comparison: {REF_F32} absent', flush=True)
        return
    _, ref = parse_dump(REF_F32)
    assert set(ref) == set(glyphs), 'REF_F32 corpus differs'
    for name in ('shinonome', 'inconsolata', 'ipaexg', 'ipaexm'):
        keys = sorted(k for k in ref if k[0] == name)
        n = px = ink = place = 0
        differ = []
        for k in keys:
            a, b = ref[k], glyphs[k]
            assert (a['w'], a['h'], a['left'], a['top'], a['adv']) == (b['w'], b['h'], b['left'], b['top'], b['adv']), (
                name, f'U+{k[1]:04X}', 'placement differs from the float raster')
            d = black(a) ^ black(b)
            ink += len(black(a))
            if d:
                n += 1
                px += len(d)
                differ.append(f'U+{k[1]:04X}')
        assert name != 'shinonome' or n == 0, ('Shinonome differs from the float raster', differ[:20])
        print(f'RECORD integer vs float raster {name}: placement equal for {len(keys)}, differing glyphs {n}, '
              f'pixels {px} / {ink} ink; {" ".join(differ[:40])}', flush=True)


def ft_scale(upem):
    """FreeType FT_DivFix(16 * 64, upem) (16.16, round half up)."""
    return ((PPEM * 64 << 16) + upem // 2) // upem


def ft_mul(value, scale):
    """FreeType FT_MulFix: round half away from zero."""
    magnitude = (abs(value) * scale + 0x8000) >> 16
    return -magnitude if value < 0 else magnitude


def check_metrics(faces, glyphs, corpus, jf):
    """FreeType's unhinted 26.6 rules re-derived from fontTools' own points."""
    from fontTools.ttLib import TTFont
    for name, path in (('shinonome', jf), ('inconsolata', FONTS / 'Inconsolata-Regular.ttf'),
                       ('ipaexg', FONTS / 'ipaexg.ttf'), ('ipaexm', FONTS / 'ipaexm.ttf')):
        font = TTFont(str(path))
        upem = font['head'].unitsPerEm
        scale = ft_scale(upem)
        face = faces[name]
        assert face['upem'] == upem
        assert face['asc'] == ft_mul(font['hhea'].ascent, scale), name
        assert face['desc'] == ft_mul(font['hhea'].descent, scale), name
        glyf = font['glyf']
        order = font.getGlyphOrder()
        cmap = font.getBestCmap()
        n = 0
        for cp in corpus[name]:
            g = glyphs[(name, cp)]
            gname = cmap[cp]
            assert order[g['gid']] == gname, (name, cp)
            coords = list(glyf[gname].getCoordinates(glyf)[0])
            adv = font['hmtx'][gname][0]
            ref = dict(adv=ft_mul(adv, scale), w=0, h=0, left=0, top=0)
            if coords:
                assert all(float(v).is_integer() for p in coords for v in p), (name, cp, 'fractional point')
                xs = [ft_mul(int(x), scale) for x, _ in coords]
                ys = [ft_mul(int(y), scale) for _, y in coords]
                left, right = min(xs) >> 6, (max(xs) + 63) >> 6
                bottom, top = min(ys) >> 6, (max(ys) + 63) >> 6
                if right > left and top > bottom:
                    ref.update(w=right - left, h=top - bottom, left=left, top=top)
                else:
                    ref.update(left=left, top=top)
            got = {k: g[k] for k in ref}
            assert got == ref, (name, f'U+{cp:04X}', got, ref)
            n += 1
        print(f'PASS {name}: 26.6 advance, bitmap left/top(baseline)/width/height == FreeType rules '
              f'(FT_DivFix/FT_MulFix, cbox floor/ceil) on fontTools points for {n} glyphs; '
              f'ascender {face["asc"]} descender {face["desc"]} (26.6)', flush=True)


def check_freetype(glyphs, corpus, jf):
    """FreeType 2 unhinted smooth raster: same placement/advance; coverage within
    the a1 flattening model's chord error (1/8 px) plus FreeType's own."""
    extra = os.environ.get('OS32_FREETYPE_PYTHONPATH')
    if extra:
        sys.path.insert(0, extra)
    try:
        import freetype
    except ImportError:
        print('SKIP FreeType comparison: freetype-py not importable (set OS32_FREETYPE_PYTHONPATH)', flush=True)
        return
    version = '.'.join(map(str, freetype.version()))
    flags = freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_RENDER
    for name, path in (('shinonome', jf), ('inconsolata', FONTS / 'Inconsolata-Regular.ttf'),
                       ('ipaexg', FONTS / 'ipaexg.ttf')):
        face = freetype.Face(str(path))
        face.set_pixel_sizes(0, PPEM)
        flips = ink = worst = delta = 0
        for cp in corpus[name]:
            g = glyphs[(name, cp)]
            face.load_glyph(g['gid'], flags)
            slot = face.glyph
            bm = slot.bitmap
            buf = bm.buffer  # freetype-py converts the whole buffer per access
            assert slot.advance.x == g['adv'], (name, f'U+{cp:04X}', 'advance', slot.advance.x, g['adv'])
            if not g['w']:
                assert not any(buf), (name, f'U+{cp:04X}', 'FreeType has ink')
                continue
            assert (slot.bitmap_left, slot.bitmap_top, bm.width, bm.rows) == (g['left'], g['top'], g['w'], g['h']), (
                name, f'U+{cp:04X}', 'placement')
            n = 0
            for r in range(g['h']):
                for c in range(g['w']):
                    f = buf[r * bm.pitch + c]
                    o = g['cov'][r * g['w'] + c]
                    delta = max(delta, abs(o - f))
                    ink += f >= THRESHOLD or o >= THRESHOLD
                    if (f >= THRESHOLD) != (o >= THRESHOLD):
                        assert abs(f - THRESHOLD) <= FREETYPE_TOLERANCE, (name, f'U+{cp:04X}', f, o)
                        n += 1
            flips += n
            worst = max(worst, n)
        assert delta <= FREETYPE_TOLERANCE and flips * 50 <= ink, (name, delta, flips, ink)
        print(f'PASS FreeType {version} {name}: placement and 26.6 advance equal for all {len(corpus[name])}; '
              f'|coverage diff| <= {delta}/255 (limit {FREETYPE_TOLERANCE}), 1bpp flips {flips}/{ink} ink px '
              f'only near the threshold (worst glyph {worst})', flush=True)


def build_probe(work, crate):
    """The product raster for i686-os32-none (integer since F5) linked with the a2 guard."""
    fixture = work / 'x87'
    shutil.copytree(ROOT / 'tools/tests/font_raster_x87', fixture, ignore=shutil.ignore_patterns('target'))
    manifest = fixture / 'Cargo.toml'
    manifest.write_text(manifest.read_text().replace('../../../userland/lib/font', str(crate)))
    target = work / 'x87-target'
    build = subprocess.run(['cargo', 'build', '--release', '--offline',
                            '-Z', 'build-std=core,compiler_builtins',
                            '-Z', 'build-std-features=compiler-builtins-mem', '-Z', 'json-target-spec',
                            '--target', str(ROOT / 'sdk/rust/i686-os32-none.json'),
                            '--manifest-path', str(manifest)],
                           env={**os.environ, 'CARGO_TARGET_DIR': str(target)},
                           text=True, capture_output=True)
    assert build.returncode == 0, build.stderr
    lib = target / 'i686-os32-none/release/libfont_raster_x87.a'
    subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'kernel/font_x87.asm'), '-o', str(work / 'fp.o')], check=True)
    inc = ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include', 'kernel', 'exec', 'fs', 'lib',
                                          'gfx', 'drivers', 'arch/x86', 'platform/pc98', 'sdk/include/os32')]
    exe = work / 'x87-probe'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                    '-static', '-no-pie', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', *inc,
                    str(ROOT / 'tools/tests/font_raster_x87_host.c'), str(ROOT / 'kernel/font_guard.c'),
                    str(work / 'fp.o'), str(lib), '-o', str(exe)], check=True)
    # The probe is 386/387 code that really uses x87 (no SSE path to hide behind).
    isa = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/check_isa386.py'), str(exe)],
                         text=True, capture_output=True)
    assert isa.returncode == 0, isa.stdout + isa.stderr
    dis = subprocess.run([objdump(), '-d', str(exe)], text=True, capture_output=True, check=True).stdout
    x87 = len(re.findall(r'\t(?:fld|fst|fadd|fmul|fdiv|fsub)[a-z]*\s', dis))
    assert x87 > 0, 'probe has no x87 instructions'
    return exe


def objdump():
    sys.path.insert(0, str(ROOT / 'tools'))
    from check_privileged import objdump_bin
    found = objdump_bin()
    assert found, 'i386-elf-objdump (CROSS_DIR)'
    return found


def run_probe(exe, run, path, cps):
    """[(cp, gid, w, h, pitch, left, top, adv, bits, cov)] from the guarded x87 run."""
    run.mkdir(parents=True)
    shutil.copyfile(path, run / 'font.bin')
    (run / 'cps.bin').write_bytes(b''.join(struct.pack('<I', cp) for cp in cps))
    result = host32.run([str(exe)], cwd=str(run), capture_output=True, timeout=600)
    assert result.returncode == 0, (str(path), 'x87 probe rc (4 = caller CW not restored)', result.returncode)
    data = (run / 'out.bin').read_bytes()
    out, at = [], 0
    for cp in cps:
        rcp, gid, err = struct.unpack_from('<IHi', data, at)
        at += 10
        assert rcp == cp and err == 0, (str(path), cp, err)
        w, h, pitch, left, top, adv = struct.unpack_from('<HHHhhi', data, at)
        at += 14
        bits = data[at:at + pitch * h]
        at += pitch * h
        cov = data[at:at + w * h]
        at += w * h
        out.append((cp, gid, w, h, pitch, left, top, adv, bits, cov))
    assert at == len(data)
    return out


def run_x87(work, crate, glyphs, fonts):
    """Guest code generation (inside the guard, CW 037F) vs the host build: bit equal since F5."""
    exe = build_probe(work, crate)
    for name, path, cps in fonts:
        bit_equal = pixels = flips = max_delta = 0
        for cp, gid, w, h, pitch, left, top, adv, bits, cov in run_probe(exe, work / ('x87-' + name), path, cps):
            g = glyphs[(name, cp)]
            assert (gid, w, h, pitch, left, top, adv) == (g['gid'], g['w'], g['h'], g['pitch'], g['left'],
                                                          g['top'], g['adv']), (name, cp)
            if g['cov'] is not None:
                for i, (a, b) in enumerate(zip(cov, g['cov'])):
                    max_delta = max(max_delta, abs(a - b))
                    r, c = divmod(i, w)
                    xb = bits[r * pitch + c // 8] >> (7 - c % 8) & 1
                    sb = g['bits'][r * pitch + c // 8] >> (7 - c % 8) & 1
                    if xb != sb:
                        # A flip is only allowed where the SSE coverage sits on the threshold.
                        assert abs(b - THRESHOLD) <= X87_TOLERANCE, (name, f'U+{cp:04X}', a, b)
                        flips += 1
                assert max_delta <= X87_TOLERANCE, (name, f'U+{cp:04X}', 'x87 vs SSE coverage', max_delta)
            bit_equal += bits == g['bits']
            pixels += w * h
        print(f'PASS x87 (i686-os32-none, guard CW 037F, 64-bit precision) {name}: {len(cps)} glyphs, '
              f'placement/26.6 equal, coverage |x87-SSE| <= {max_delta}/255 (limit {X87_TOLERANCE}), '
              f'1bpp flips {flips}/{pixels} px only at the threshold, bit-identical glyphs {bit_equal}',
              flush=True)
    return exe


def guest_expect(work, exe, fonts):
    """EXPECT lines for `font_test -c` (same ranges, FNV-1a over the same bytes)."""
    from fontTools.ttLib import TTFont
    for name, path in fonts:
        font = TTFont(str(path))
        cmap, order = font.getBestCmap(), font.getGlyphOrder()
        cps = [cp for first, last in GUEST_RANGES for cp in range(first, last + 1)
               if cp in cmap and order.index(cmap[cp]) != 0]
        fnv, black = 0x811c9dc5, 0
        for cp, gid, w, h, pitch, left, top, adv, bits, cov in run_probe(exe, work / ('guest-' + name), path, cps):
            for byte in struct.pack('<IHHhhi', cp, w, h, left, top, adv) + bits:
                fnv = ((fnv ^ byte) * 16777619) & 0xffffffff
            black += sum(bin(b).count('1') for b in bits)
        print(f'EXPECT FONTCHK {name} glyphs={len(cps)} black={black} fallback=0 fnv={fnv:x}', flush=True)


def isa386():
    """Acceptance 4: final linked ELFs (font_test now links the product crate)."""
    elves = [ROOT / p for p in ('userland/tests/font_test.elf', 'userland/gshell.elf', 'userland/libos32gui.elf')]
    for elf in elves:
        assert elf.exists(), f'{elf}: build first (make all)'
    result = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/check_isa386.py'), *map(str, elves)],
                            text=True, capture_output=True)
    print(result.stdout, end='', flush=True)
    assert result.returncode == 0, result.stdout + result.stderr


# --- TASK_FONTPERF F1: the pre-drawn glyph bundle (.g16, tools/gen_g16.py) ---------
G16_ASCENT = 14          # font crate limits.rs ASCENT_JA_PX: cell row 0 = baseline - 14
G16_PRELOAD = 1600       # design §4
G16_GLYPHS = {}          # the normal pass's dump, reused by the g16 mutations
G16_MUTATIONS = [
    # (label, file under tools/, old, new): each must make check_g16 fail.
    ('g16-bearing-dropped', 'gen_g16.py', 'x = m.horiBearingX + c', 'x = c'),
    ('g16-rank-crc', 'gen_g16.py', 'zlib.crc32(rank)', 'zlib.crc32(rank[:-1])'),
    ('g16-unaligned', 'gen_g16.py', 'off_all = (pre_end + SECTOR - 1) // SECTOR * SECTOR', 'off_all = pre_end'),
    ('g16-head-adjust', 'gen_g16.py', "adjust = struct.unpack_from('>I', data, off + 8)[0]",
     "adjust = struct.unpack_from('>I', data, off + 12)[0]"),
    ('g16-preload-order', 'g16/gen_preload16.py', "(('jis1-8', jis_rows_1_8()), ('ui', ui_chars(ui)),",
     "(('ui', ui_chars(ui)), ('jis1-8', jis_rows_1_8()),"),
]


def g16_generate(tools, jf, work):
    """Run the generators of `tools` (the product copy or a mutant) into `work`."""
    work.mkdir(parents=True, exist_ok=True)
    pre, g16 = work / 'preload16.txt', work / 'bundle.g16'
    for cmd in (['g16/gen_preload16.py', '--ttf', str(jf), '-o', str(pre), '--ipadic', str(ROOT / 'assets/ipadic')],
                ['gen_g16.py', '--ttf', str(jf), '--preload', str(pre), '-o', str(g16)]):
        r = subprocess.run([sys.executable, '-B', str(tools / cmd[0]), *cmd[1:]], text=True, capture_output=True)
        assert r.returncode == 0, (cmd[0], r.returncode, r.stdout + r.stderr)
    return pre, g16


def cell_of(dots):
    """Absolute dots (x from pen, y up from baseline) -> 32-byte cell, None if outside."""
    out = bytearray(32)
    for x, y in dots:
        row = G16_ASCENT - 1 - y
        if not (0 <= x < 16 and 0 <= row < 16):
            return None
        out[row * 2 + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def jis_rows_1_8():
    """Design §4 part 1: JIS X 0208 rows 1-8 via the euc_jp codec, code order."""
    out = []
    for hi in range(0xA1, 0xA9):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([hi, lo]).decode('euc_jp')))
            except (UnicodeDecodeError, TypeError):
                pass
    return out


def check_g16(jf, glyphs, g16, pre):
    """Bundle == EBDT dots in the cell == Rust raster in the cell for every record;
    preload records == the list and == the all-section record; rank index round trip
    and CRC; head identity, size and sha256; 512-aligned all section. The file is
    parsed here from the design §2 offsets, not with gen_g16's own structures."""
    import hashlib
    import zlib
    from fontTools.ttLib import TTFont
    data = g16.read_bytes()
    raw = jf.read_bytes()
    (magic, version, hdr_len, cell_w, cell_h, ascent, flags, n_all, n_pre, ttf_size, adjust, modified,
     sha) = struct.unpack_from('<4sHHBBbBIIIIQ32s', data, 0)
    off_rank, rank_len, rank_crc, off_pre = struct.unpack_from('<IIII', data, 68)
    pre_rec, = struct.unpack_from('<H', data, 84)
    off_all, = struct.unpack_from('<I', data, 86)
    all_rec, = struct.unpack_from('<H', data, 90)
    assert (magic, version, hdr_len, cell_w, cell_h, ascent, flags) == (b'OSG1', 1, 128, 16, 16, G16_ASCENT, 1), (
        'g16 header', magic, version, hdr_len, cell_w, cell_h, ascent, flags)
    assert data[92:128] == bytes(36), 'g16 header padding'
    assert (pre_rec, all_rec, off_rank, rank_len, off_pre) == (36, 32, 128, 8704, 128 + 8704), 'g16 layout'
    # The TTF identity the guest checks (size + head.checkSumAdjustment + head.modified)
    # and the host-only sha256, read straight from the TTF bytes.
    font = TTFont(str(jf))
    head_off = next(struct.unpack_from('>I', raw, 12 + 16 * i + 8)[0]
                    for i in range(struct.unpack_from('>H', raw, 4)[0]) if raw[12 + 16 * i:16 + 16 * i] == b'head')
    assert ttf_size == len(raw), ('g16 ttf_size', ttf_size, len(raw))
    assert adjust == font['head'].checkSumAdjustment == struct.unpack_from('>I', raw, head_off + 8)[0], (
        'g16 head.checkSumAdjustment', adjust)
    assert modified == struct.unpack_from('>Q', raw, head_off + 28)[0], ('g16 head.modified', modified)
    assert sha == hashlib.sha256(raw).digest(), 'g16 ttf_sha256'
    # 1 record = 1 sector (F-9).
    pre_end = off_pre + n_pre * pre_rec
    assert off_all % 512 == 0 and off_all >= pre_end, ('g16 off_all alignment', off_all)
    assert data[pre_end:off_all] == bytes(off_all - pre_end), 'g16 gap is not zero'
    assert len(data) == off_all + n_all * all_rec, ('g16 size', len(data))
    # Rank index: CRC, then every BMP cp round trip.
    rank = data[off_rank:off_rank + rank_len]
    assert zlib.crc32(rank) == rank_crc, ('g16 rank_crc32', hex(zlib.crc32(rank)), hex(rank_crc))
    bitmap, cum = rank[:8192], struct.unpack_from('<256H', rank, 8192)
    cps = bundle_cps(jf)
    assert n_all == len(cps), ('g16 n_all', n_all, len(cps))
    present = set(cps)
    rec_of = {cp: i for i, cp in enumerate(cps)}
    for cp in range(0x10000):
        bit = bitmap[cp >> 3] >> (cp & 7) & 1
        assert bit == (cp in present), ('g16 rank bitmap', hex(cp))
        if bit:
            r = cum[cp >> 8] + sum(bitmap[c >> 3] >> (c & 7) & 1 for c in range(cp & ~0xFF, cp))
            assert r == rec_of[cp], ('g16 rank', hex(cp), r, rec_of[cp])
    # Every record == EBDT dots in the cell == Rust raster (dump) in the cell.
    dots = ebdt_dots(jf)
    records = {}
    for i, cp in enumerate(cps):
        rec = data[off_all + i * 32:off_all + i * 32 + 32]
        g = glyphs[('shinonome', cp)]
        want = cell_of(dots(g['gid']))
        assert want is not None, f'U+{cp:04X}: EBDT glyph outside the 16x16 cell'
        assert rec == want, f'g16 U+{cp:04X}: record != EBDT dots in the cell'
        assert rec == cell_of(black(g)), f'g16 U+{cp:04X}: record != Rust raster in the cell'
        records[cp] = rec
    # Preload section: the list in its order, each record a copy of the all section.
    listed = [int(f[0], 16) for f in (line.split() for line in pre.read_text(encoding='utf-8').splitlines())
              if f and not f[0].startswith('#')]
    got = []
    for i in range(n_pre):
        at = off_pre + i * pre_rec
        cp, = struct.unpack_from('<I', data, at)
        assert cp in records, ('g16 preload cp not in the bundle', hex(cp))
        assert data[at + 4:at + 36] == records[cp], ('g16 preload record', hex(cp))
        got.append(cp)
    assert got == listed, 'g16 preload section != preload16.txt'
    assert len(got) == len(set(got)) == G16_PRELOAD, ('g16 preload count', len(got), len(set(got)))
    head = [cp for cp in jis_rows_1_8() if cp in present]
    assert got[:len(head)] == head, 'g16 preload does not start with JIS rows 1-8 (code order)'
    print(f'PASS g16 bundle {len(data)} B: {n_all} records == EBDT 16px dots == Rust raster in the 16x16 cell; '
          f'preload {n_pre} (JIS rows 1-8 first, {len(head)}) == list, copies of the all section; rank index '
          f'round trip over the BMP, CRC32 {rank_crc:08x}; off_all {off_all} % 512 == 0; head/size/sha256 == TTF',
          flush=True)


def g16_mutations(work, jf, glyphs):
    """Each generator mutant must be caught by check_g16 (never touches tools/)."""
    for label, name, old, new in G16_MUTATIONS:
        tools = work / label / 'tools'
        (tools / 'g16').mkdir(parents=True)
        for rel in ('gen_g16.py', 'g16/gen_preload16.py', 'g16/ui_chars.txt'):
            shutil.copyfile(ROOT / 'tools' / rel, tools / rel)
        text = (tools / name).read_text()
        assert text.count(old) == 1, label
        (tools / name).write_text(text.replace(old, new))
        pre, g16 = g16_generate(tools, jf, work / label / 'out')
        try:
            check_g16(jf, glyphs, g16, pre)
        except AssertionError as error:
            print(f'RED g16 {label} (generated): {error!s:.160}', flush=True)
            continue
        raise AssertionError(f'g16 mutation {label} survived')


def verify(work, crate, env, jf, corpus, ja, jis, label, x87=True):
    """One full pass on `crate`: None on success, else (stage, detail)."""
    dump = work / (label + '-dump.txt')
    result = build_run(crate, work, {**env, 'OS32_RASTER_DUMP': str(dump)}, label)
    if result.returncode != 0:
        return 'rust', result.stdout[-4000:] + result.stderr[-4000:]
    if label == 'normal':
        print(result.stdout, end='', flush=True)
    faces, glyphs = parse_dump(dump)
    try:
        check_dots(jf, glyphs, ja, jis)
        if label == 'normal':
            pre, g16 = g16_generate(ROOT / 'tools', jf, work / 'g16')
            check_g16(jf, glyphs, g16, pre)
            G16_GLYPHS.update(glyphs)
        check_metrics(faces, glyphs, corpus, jf)
        if label == 'normal':
            check_ref_f32(glyphs)
        check_freetype(glyphs, corpus, jf)
        if x87:
            exe = run_x87(work / (label + '-x87'), crate, glyphs, [
                ('shinonome', jf, ja), ('inconsolata', FONTS / 'Inconsolata-Regular.ttf', corpus['inconsolata']),
                ('ipaexg', FONTS / 'ipaexg.ttf', jis)])
            guest_expect(work / (label + '-x87'), exe, [
                ('shinonome', jf), ('inconsolata', FONTS / 'Inconsolata-Regular.ttf'), ('ipaexg', FONTS / 'ipaexg.ttf')])
    except AssertionError as error:
        return 'python', error
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    for family in ('ipaex', 'inconsolata'):
        subprocess.run(['python3', str(ROOT / 'tools/fetch_fonts.py'), '--family', family], check=True)
    product_grep()
    with tempfile.TemporaryDirectory(prefix='font-raster-') as directory:
        work = Path(directory)
        jf = shinonome(work)
        jis, latin, ja, paths = corpora(work, jf)
        corpus = dict(shinonome=ja, inconsolata=latin, ipaexg=jis, ipaexm=jis)
        env = {**os.environ, 'OS32_FONT_SHINONOME': str(jf),
               **{'OS32_FONT_' + k: str(v) for k, v in paths.items()}}
        normal = verify(work, CRATE, env, jf, corpus, ja, jis, 'normal')
        assert normal is None, normal
        isa386()
        if args.mutate:
            g16_mutations(work / 'g16-mut', jf, G16_GLYPHS)
            copy = work / 'mut/userland/lib/font'
            shutil.copytree(CRATE, copy, ignore=shutil.ignore_patterns('target'))
            (work / 'mut/assets').mkdir()
            (work / 'mut/assets/fonts').symlink_to(FONTS, target_is_directory=True)
            originals = {n: (copy / 'src' / n).read_text() for n in ('raster.rs', 'limits.rs', 'outline.rs', 'fixed.rs')}
            for label, name, old, new, expected in MUTATIONS:
                for n, text in originals.items():
                    (copy / 'src' / n).write_text(text)
                assert originals[name].count(old) == 1, label
                (copy / 'src' / name).write_text(originals[name].replace(old, new))
                if expected.startswith('py:'):
                    failed = verify(work, copy, env, jf, corpus, ja, jis, 'mut-' + label, x87=False)
                    assert failed is not None and failed[0] == 'python', (label, failed)
                    print(f'RED reference {label} (compiled, Rust tests green): {failed[1]!s:.160}', flush=True)
                    continue
                result = build_run(copy, work, env, 'mut-' + label, expected)
                assert result.returncode == 101 and ('test ' + expected + ' ... FAILED') in result.stdout, (
                    label, result.stdout, result.stderr)
                # An assertion (or a test's own unwrap of the product Result), never an
                # arithmetic/index panic inside the product.
                assert 'panicked at tests/raster.rs' in result.stderr and not any(
                    t in result.stderr for t in ('attempt to', 'index out of bounds')), (label, result.stderr)
                print('RED runtime ' + label + ' (compiled): ' + expected, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
