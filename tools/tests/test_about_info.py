"""V11: real kernel formatter -> real CUI and Rust reader, ABI and GUI fit.

The line oracle remains in V8. This checks both consumers against the identical
formatted snapshot, including debug/services. No emulator or device calls.
--mutate runs normal tests first, then copy-only negative controls.
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar
from ver_test_utils import ROOT, execute, function

INFO_REL = 'userland/rust/about/src/info.rs'
CMD_REL = 'userland/shell/cmd_base.c'
LIB_REL = 'userland/rust/about/src/lib.rs'
ENV_OF = {INFO_REL: 'OS32_ABOUT_INFO_RS', CMD_REL: 'OS32_VER_CMD_BASE_C',
          LIB_REL: 'OS32_ABOUT_LIB_RS'}
INFO_RS = pathlib.Path(os.environ.get(ENV_OF[INFO_REL], ROOT / INFO_REL))
CMD_BASE_C = pathlib.Path(os.environ.get(ENV_OF[CMD_REL], ROOT / CMD_REL))
LIB_RS = pathlib.Path(os.environ.get(ENV_OF[LIB_REL], ROOT / LIB_REL))
GUI_WIDGET_RS = ROOT / 'userland/rust/libos32gui/src/widget.rs'
GUI_LAYOUT_RS = ROOT / 'userland/rust/libos32gui/src/layout.rs'
WM_RS = ROOT / 'userland/gshell/src/wm.rs'
VER_H = ROOT / 'sdk/include/os32/os32_ver_info.h'

RUST_MAIN = r'''
#[path = "INFO_PATH"]
#[allow(dead_code)]
mod info;
use info::*;
use std::io::{self, Read};
static mut CALLS: u32 = 0;
static mut RC: i32 = 0;
unsafe extern "C" fn getter(out: *mut u8, size: u32) -> i32 {
    assert_eq!(size as usize, core::mem::size_of::<VerInfo>());
    CALLS += 1;
    (*(out as *mut VerInfo)).count = 7;
    RC
}
mod os32api {
    pub struct Api { pub sys_ver_info: unsafe extern "C" fn(*mut u8, u32) -> i32 }
    pub unsafe fn api() -> Api { Api { sys_ver_info: super::getter } }
}
COLLECT_SOURCE
fn main() {
    assert_eq!(core::mem::size_of::<VerLine>(), 68);
    assert_eq!(core::mem::size_of::<VerInfo>(), 1364);
    assert_eq!(core::mem::offset_of!(VerInfo, lines), 4);
    assert_eq!(core::mem::offset_of!(VerLine, text), 4);
    let mut r = VerInfo::new();
    assert!(r.rows().is_empty());
    assert_eq!(collect(&mut r), 0);
    assert_eq!(r.rows().len(), 7);
    unsafe { assert_eq!({CALLS}, 1); RC = -1; }
    assert_eq!(collect(&mut r), -1);
    unsafe { assert_eq!({CALLS}, 2); }
    r.count = u32::MAX;
    assert_eq!(r.rows().len(), OS32_VER_LINES);
    for kind in [OS32_VER_HEAD, OS32_VER_DEVICE, OS32_VER_IDENTITY, OS32_VER_DEBUG] {
        r.lines[0].kind = kind | OS32_VER_INDENT;
        assert_eq!(r.lines[0].group(), kind);
    }
    r.lines[0].text = [b'x'; OS32_VER_TEXT];
    assert_eq!(r.lines[0].as_bytes().len(), OS32_VER_TEXT);
    r.lines[0].text[3] = 0;
    assert_eq!(r.lines[0].as_bytes(), b"xxx");
    r.lines[0].text[0] = 0;
    assert!(r.lines[0].as_bytes().is_empty());
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    r = VerInfo::new();
    for (i, line) in input.lines().enumerate() {
        let (kind, text) = line.split_once('\t').unwrap();
        r.lines[i].kind = kind.parse().unwrap();
        r.lines[i].text[..text.len()].copy_from_slice(text.as_bytes());
        r.count += 1;
    }
    for line in r.rows() {
        println!("{}\t{}", line.group(), String::from_utf8_lossy(line.as_bytes()));
    }
}
'''
def header_int(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)' % re.escape(name), text, re.M)
    if not m:
        raise AssertionError('%s not found' % name)
    return int(m.group(1), 0)


def rust_int(text, name):
    m = re.search(r'pub const %s: \w+ = (0x[0-9A-Fa-f_]+|\d+);' % re.escape(name), text)
    if not m:
        raise AssertionError('%s not found in info.rs' % name)
    return int(m.group(1).replace('_', ''), 0)


def lib_const(lib, name):
    m = re.search(r'^const %s: \w+ = (\d+);' % re.escape(name), lib, re.M)
    if not m:
        raise AssertionError('%s not found in lib.rs' % name)
    return int(m.group(1))


def wm_const(name):
    m = re.search(r'pub const %s: i32 = (\d+);' % re.escape(name), WM_RS.read_text())
    if not m:
        raise AssertionError('%s not found in wm.rs' % name)
    return int(m.group(1))


def widget_min_h():
    """libos32gui の各コンストラクタが置く min_h (置かなければ 0)。"""
    src = GUI_WIDGET_RS.read_text()
    consts = {k: int(v) for k, v in
              re.findall(r'^(?:pub )?const (\w+): i16 = (\d+);', src, re.M)}
    out = {}
    for name, body in re.findall(r'^pub fn (\w+)\([^)]*\) -> GuiResult<WidgetId> \{\n(.*?)^\}',
                                 src, re.M | re.S):
        m = re.search(r'\.min_h = (\w+)(?: \+ (\d+))?;', body)
        if not m:
            out[name] = 0
            continue
        v = m.group(1)
        base = int(v) if v.isdigit() else consts[v]
        out[name] = base + int(m.group(2) or 0)
    return out


def root_stack(lib, n):
    """lib.rs が root (column) に足す子の主軸の長さの合計と子の数。
    変数ごとの部品の種類は `let <var> = widget::<kind>(` から、大きさの指定は
    `widget::add(root, <var>, SizeSpec::...)` から読む。繰り返しは lib.rs の
    ループの形 (行は n 本、区切りは最大 3 本) に合わせる。"""
    mins = widget_min_h()
    kinds = dict(re.findall(r'let (\w+) = widget::(\w+)\(', lib))
    adds = re.findall(r'widget::add\(root, (\w+), SizeSpec::(Fixed|Flex)\(((?:[^()]|\([^()]*\))*)\)\)', lib)
    if not adds:
        raise AssertionError('root への add が見つからない')
    # group が変わる3か所 (head/device/identity/debug) に区切りを置く。
    if 'if i > 0 && lines[i].group() != lines[i - 1].group() {' not in lib:
        raise AssertionError('区切りの条件が想定と違う (試験の計算を直す)')
    total = 0
    items = 0
    for var, spec, arg in adds:
        mn = mins[kinds[var]]
        if spec == 'Flex':
            sizes = [None]
        else:
            m = re.fullmatch(r'if i == 0 \{ (\w+) \} else \{ (\w+) \}', arg.strip())
            if m:                                # 行: 1 行目と残り n - 1 本
                sizes = [m.group(1)] + [m.group(2)] * (n - 1)
            elif var == 'sep':
                sizes = [arg.strip()] * 3
            else:
                sizes = [arg.strip()]
        for sz in sizes:
            if sz is None:
                total += mn
            else:
                px = int(sz) if sz.isdigit() else lib_const(lib, sz)
                total += px if px > mn else mn
            items += 1
    return total, items



class AboutMatchesVer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='os32-about-info-')
        cls.addClassCleanup(cls.tmp.cleanup)
        d = pathlib.Path(cls.tmp.name)
        main = d / 'main.rs'
        collect = function(LIB_RS.read_text(), 'fn collect(')
        main.write_text(RUST_MAIN.replace('INFO_PATH', str(INFO_RS))
                        .replace('COLLECT_SOURCE', collect))
        cls.rs = d / 'aboutinfo'
        tc = subprocess.run(['rustup', 'toolchain', 'list'], check=True,
                            capture_output=True, text=True)
        name = tc.stdout.splitlines()[0].split()[0]
        r = subprocess.run(['rustc', '+' + name, '--edition', '2021', '-O',
                            '-o', str(cls.rs), str(main)], capture_output=True, text=True)
        if r.returncode:
            raise AssertionError(r.stderr)
        files = {
            'ver_format_source.c': (ROOT / 'kernel/ver_format.c').read_text(),
            'ver_state_source.c': function((ROOT / 'kernel/module/module.c').read_text(),
                                          'const char *kdebug_state_name('),
            'ver_shell_source.c': function(CMD_BASE_C.read_text(), 'static int cmd_ver('),
        }
        r = execute((ROOT / 'tools/tests/ver_about_host.c').read_text(),
                    'qemu', files, assembly='lib/kstring_asm.asm')
        if r.returncode:
            raise AssertionError(r.stderr + r.stdout)
        cls.cases = []
        for case in r.stdout.split('REPORT\n')[1:]:
            report, cui = case.split('CUI\n')
            cls.cases.append((report, cui.removesuffix('END\n').splitlines()))
        assert len(cls.cases) == 26

    def test_both_uis_equal_real_formatter(self):
        for report, cui in self.cases:
            with self.subTest(report=report):
                entries = [s.split('\t', 1) for s in report.splitlines()]
                expected = [text for kind, text in entries]
                expected_cui = [('  ' if int(kind) & 0x100 else '') + text
                                for kind, text in entries]
                self.assertEqual(cui, expected_cui)
                r = subprocess.run([str(self.rs)], input=report, text=True,
                                   capture_output=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                actual = [s.split('\t', 1) for s in r.stdout.splitlines()]
                self.assertEqual([text for group, text in actual], expected)
                self.assertEqual([int(group) for group, text in actual],
                                 [int(kind) & 0xff for kind, text in entries])
                self.assertLessEqual(len(expected), 16)
                self.assertLessEqual(max(map(len, expected)), 52)
                self.assertTrue(expected[-1].startswith('services:'))
        self.assertEqual(max(len(report.splitlines()) for report, _ in self.cases), 16)

    def test_abi_constants_and_single_source(self):
        h, rs, lib = VER_H.read_text(), INFO_RS.read_text(), LIB_RS.read_text()
        for name in ('OS32_VER_LINES', 'OS32_VER_TEXT', 'OS32_VER_COLS',
                     'OS32_VER_KIND_MASK', 'OS32_VER_HEAD', 'OS32_VER_DEVICE',
                     'OS32_VER_IDENTITY', 'OS32_VER_DEBUG', 'OS32_VER_INDENT'):
            self.assertEqual(header_int(h, name), rust_int(rs, name), name)
        self.assertEqual(lib.count('(a.sys_ver_info)'), 1)
        self.assertIn('let rc = collect(&mut report);', lib)
        self.assertIn('if rc != 0 {', lib)
        self.assertIn('return Err(libos32gui::GuiErr(rc));', lib)
        self.assertIn('let lines = report.rows();', lib)
        self.assertIn('widget::label(lines[i].as_bytes())?', lib)
        for old in ('include_bytes!', 'sys_version(', 'VER_HW', 'BootImageInfo',
                    'sys_get_build_info', 'boot_image_info'):
            self.assertNotIn(old, rs + lib)

    def test_width_and_actual_layout(self):
        lib = LIB_RS.read_text()
        cols = (lib_const(lib, 'WIN_W_MAX') - 2 * wm_const('BORDER_W')
                - 2 * lib_const(lib, 'PAD')) // 8
        self.assertLessEqual(rust_int(INFO_RS.read_text(), 'OS32_VER_COLS'), cols)
        self.assertIn('let ww = clamp(sw - 16, 240, WIN_W_MAX);', lib)
        self.assertIn('let wh = clamp(sh - 40, 200, WIN_H_MAX);', lib)
        self.assertIn('let wy = clamp((sh - 24 - wh) / 2, 0, sh);', lib)
        # Count is the formatter's current maximum (16), not the ABI capacity (20).
        stack, items = root_stack(lib, 16)
        need = stack + lib_const(lib, 'GAP') * (items - 1) + 2 * lib_const(lib, 'PAD')
        for sh in (400, 480):
            wh = min(sh - 40, lib_const(lib, 'WIN_H_MAX'))
            client = wh - 2 * wm_const('BORDER_W') - wm_const('TITLEBAR_H')
            self.assertLessEqual(need, client, (sh, need, client))
            wy = max(0, (sh - 24 - wh) // 2)
            self.assertLessEqual(wy + wh, sh)
        mins = widget_min_h()
        self.assertLessEqual(mins['button'], lib_const(lib, 'BAR_H'))
        self.assertIn('if down && (scan == SCAN_ESC || scan == SCAN_RETURN)', lib)
        self.assertIn('if w == self.ok {', lib)
        self.assertIn('widget::set_focus(ok)?;', lib)

    def test_layout_rules_modelled(self):
        lay = re.sub(r'\s+', ' ', GUI_LAYOUT_RS.read_text())
        self.assertIn('SizeSpec::Fixed(px) => { let m = if (px as i32) < min_main '
                      '{ min_main } else { px as i32 }; fixed_total += m;', lay)
        self.assertIn('SizeSpec::Flex(w) => { fixed_total += min_main;', lay)
        mins = widget_min_h()
        self.assertEqual(mins['label'], 16)
        self.assertEqual(mins['row'], 0)
        self.assertEqual(mins['column'], 0)

    def test_deployed(self):
        self.assertIn('host: userland/system/about.bin',
                      (ROOT / 'userland/deploy.yaml').read_text())


MUTATIONS = [
    ('info-count', INFO_REL, 'core::cmp::min(self.count as usize, OS32_VER_LINES)', '1'),
    ('info-nul', INFO_REL, 'c == 0', 'c == 255'),
    ('info-kind-indent', INFO_REL, 'self.kind & OS32_VER_KIND_MASK', 'self.kind'),
    ('info-lines', INFO_REL, 'OS32_VER_LINES: usize = 20;', 'OS32_VER_LINES: usize = 19;'),
    ('info-cols', INFO_REL, 'OS32_VER_COLS: usize = 52;', 'OS32_VER_COLS: usize = 53;'),
    ('lib-sep-label', LIB_REL, 'let sep = widget::row(0, 0)?;', 'let sep = widget::label(b"")?;'),
    ('lib-spacer-label', LIB_REL, 'let spacer = widget::row(0, 0)?;', 'let spacer = widget::label(b"")?;'),
    ('lib-win-h-short', LIB_REL, 'const WIN_H_MAX: i32 = 368;', 'const WIN_H_MAX: i32 = 312;'),
    ('lib-bar-thin', LIB_REL, 'const BAR_H: i16 = 24;', 'const BAR_H: i16 = 20;'),
    ('lib-no-acquire', LIB_REL, 'let rc = collect(&mut report);', 'let rc = 0;'),
    ('ver-indent', CMD_REL, 'if (line->kind & OS32_VER_INDENT)', 'if (0)'),
    ('ver-skip-last', CMD_REL, 'i < report.count', 'i + 1 < report.count'),
]


def one_mutation(item):
    name, rel, old, new = item
    original = (ROOT / rel).read_text()
    if original.count(old) != 1:
        return 'MUTATE %s missing/ambiguous anchor' % name, 1
    with tempfile.TemporaryDirectory(prefix='os32-about-mut-') as td:
        copy = pathlib.Path(td) / pathlib.Path(rel).name
        copy.write_text(original.replace(old, new, 1))
        env = dict(os.environ)
        env[ENV_OF[rel]] = str(copy)
        r = subprocess.run([sys.executable, '-B', str(pathlib.Path(__file__).resolve())],
                           env=env, capture_output=True, text=True, timeout=300)
    return 'MUTATE %s %s' % (name, 'RED' if r.returncode else '**GREEN のまま = 試験が見ていない**'), int(r.returncode == 0)


if __name__ == '__main__':
    mutate = '--mutate' in sys.argv
    prog = unittest.main(argv=[a for a in sys.argv if a != '--mutate'], exit=False)
    failed = not prog.result.wasSuccessful()
    if mutate:
        failed = mutpar.run_with_control(one_mutation, MUTATIONS,
                    ('control', INFO_REL, 'OS32_VER_COLS: usize = 52;',
                     'OS32_VER_COLS: usize = 52;')) != 0 or failed
    sys.exit(1 if failed else 0)
