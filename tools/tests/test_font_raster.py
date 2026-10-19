"""T7b-a5: 16ppem 1bpp raster, 32px = 2x nearest, references and x87.

References are independent of the Rust code: Shinonome's own 16ppem EBDT
strike (the original dots; also the BDF original when present locally),
fontTools exact-rational metrics for every corpus glyph, FreeType 2 (only
when freetype-py is importable, e.g. via OS32_FREETYPE_PYTHONPATH; otherwise
a SKIP line). The x87 run compiles the product raster for i686-os32-none and
executes it on the host inside the product a2 guard (kernel/font_guard.c +
font_x87.asm, CW 037F): host f32 (SSE) and x87 are compared by coverage
tolerance, not bit equality (acceptance 5). The final linked font_test.elf
is checked by tools/check_isa386.py (acceptance 4).
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
THRESHOLD = 128          # coverage byte >= 128 is black (limits.rs COVERAGE_THRESHOLD)
X87_TOLERANCE = 1        # quantised coverage units (of 255) allowed between SSE and x87
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
    ('nonzero-abs', 'raster.rs', 'let coverage = abs(sum).min(1.0);', 'let coverage = sum.min(1.0);',
     'pixel_square_is_exact_and_doubles'),
    ('threshold', 'limits.rs', 'COVERAGE_THRESHOLD: f32 = 128.0 / 255.0;',
     'COVERAGE_THRESHOLD: f32 = 127.0 / 255.0;', 'threshold_is_half_coverage_boundary'),
    ('nearest-2x', 'raster.rs', 'let bit = 0x80u8 >> ((column / factor) % 8);',
     'let bit = 0x80u8 >> (column % 8);', 'pixel_square_is_exact_and_doubles'),
    ('round-26.6', 'raster.rs', '(value.abs() * scale + 0x8000) >> 16', '(value.abs() * scale) >> 16',
     'units_to_26_6_rounding'),
    ('short-loca-even', 'outline.rs', 'let padded = end.next_multiple_of(2);',
     'let padded = end;', 'odd_length_components_keep_even_short_loca'),
    ('i16-overflow', 'raster.rs', 'left: base.left.checked_mul(factor as i16).ok_or(Error::Bounds)?,',
     'left: base.left.wrapping_mul(factor as i16),', 'coordinate_overflow'),
    ('flatten-coarse', 'raster.rs', '< QUAD_FLATNESS_SQ * d2', '< 0.25 * d2', 'flattened_edge_limit'),
    ('cbox-on-curve-only', 'raster.rs', '        if let Some(c) = e.control {\n            visit(c)?;\n        }\n', '',
     'control_box_includes_off_curve_points'),
    ('span-split', 'raster.rs', 'if floor(xa) == floor(xb) || ceil(xa) == ceil(xb) {', 'if true {',
     'diagonal_coverage_is_exact_area'),
    ('flip-y', 'raster.rs', 'y: (top * 64 - coord_26_6(p.y, scale)?) as f32 / 64.0,',
     'y: (coord_26_6(p.y, scale)? - bottom * 64) as f32 / 64.0,', 'control_box_includes_off_curve_points'),
    # Only real fonts see this one (synthetic hmtx is uniform): every Rust test
    # stays green and the corpus metric reference must catch it.
    ('advance-gid', 'raster.rs', 'i32::from(face.advance(gid)?)', 'i32::from(face.advance(gid.saturating_sub(1))?)',
     'py:metrics'),
]


def corpora(work):
    jis = []
    for a in range(0x21, 0x7f):
        for b in range(0x21, 0x7f):
            try:
                jis.append(ord((b'\x1b$B' + bytes([a, b]) + b'\x1b(B').decode('iso2022_jp')))
            except UnicodeDecodeError:
                pass
    assert len(jis) == 6879
    latin = list(range(0x20, 0x7f)) + list(range(0xa0, 0x100))
    ja = sorted(set(jis) | set(latin))
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
    """The product raster for i686-os32-none (x87 f32) linked with the a2 guard."""
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
    """x87 (guest code generation, guard CW 037F) vs host SSE f32 by coverage."""
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
        check_metrics(faces, glyphs, corpus, jf)
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
        jis, latin, ja, paths = corpora(work)
        corpus = dict(shinonome=ja, inconsolata=latin, ipaexg=jis, ipaexm=jis)
        env = {**os.environ, 'OS32_FONT_SHINONOME': str(jf),
               **{'OS32_FONT_' + k: str(v) for k, v in paths.items()}}
        normal = verify(work, CRATE, env, jf, corpus, ja, jis, 'normal')
        assert normal is None, normal
        isa386()
        if args.mutate:
            copy = work / 'mut/userland/lib/font'
            shutil.copytree(CRATE, copy, ignore=shutil.ignore_patterns('target'))
            (work / 'mut/assets').mkdir()
            (work / 'mut/assets/fonts').symlink_to(FONTS, target_is_directory=True)
            originals = {n: (copy / 'src' / n).read_text() for n in ('raster.rs', 'limits.rs', 'outline.rs')}
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
