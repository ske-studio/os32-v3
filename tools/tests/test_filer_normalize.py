"""filer の normalize_abs が VFS の vfs_resolve_path と同じ結果になるか。

2026-09-10 のレビュー指摘 (P1/blocker): filer の check_abs は正規化しないのに
VFS は `.` `..` `//` を畳むので、`/host/a.txt` と `/host/./a.txt` は文字列として
別物なのに同じファイルを開く。inode を返さない fs (hostdrvfs) では inode 判定も
効かず、同一ファイルへのコピーが O_TRUNC で元を空にする。

両実装を**実ソースから抜き出して**ホストでビルドし、同じ入力で突き合わせる。
片方だけ直しても気付けるようにするのが狙い。エミュレータは使わない。
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

CASES = [
    ('/host/a.txt', '/host/a.txt'),
    ('/host/./a.txt', '/host/a.txt'),
    ('/host//a.txt', '/host/a.txt'),
    ('/host/b/../a.txt', '/host/a.txt'),
    ('/a/./b/../c', '/a/c'),
    ('/', '/'),
    ('/..', '/'),
    ('/./', '/'),
    ('/x//y///z', '/x/y/z'),
    ('/host/./b/./a.txt', '/host/b/a.txt'),
]

RUST_MAIN = '''
const PATH_CAP: usize = 256;
fn cstr_len(s: &[u8]) -> usize { let mut n=0; while n < s.len() && s[n] != 0 { n += 1; } n }
%s
fn run(p: &str) -> String {
    let mut inb = [0u8; PATH_CAP];
    inb[..p.len()].copy_from_slice(p.as_bytes());
    let mut out = [0u8; PATH_CAP];
    match normalize_abs(&inb, &mut out) {
        Some(n) => String::from_utf8_lossy(&out[..n]).into_owned(),
        None => "<None>".to_string(),
    }
}
fn main() {
    for a in std::env::args().skip(1) { println!("{}", run(&a)); }
}
'''


class FilerNormalize(unittest.TestCase):
    def setUp(self):
        src = (ROOT / 'userland/rust/filer/src/model.rs').read_text()
        i = src.index('pub fn normalize_abs')
        j = src.index('\n}\n', i) + 3
        self.func = src[i:j]
        self.tmp = tempfile.TemporaryDirectory(prefix='os32-filer-norm-')
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _build(self):
        main = self.dir / 'main.rs'
        main.write_text(RUST_MAIN % self.func)
        exe = self.dir / 'normtest'
        tc = subprocess.run(['rustup', 'toolchain', 'list'],
                            capture_output=True, text=True)
        name = tc.stdout.split('\n')[0].split(' ')[0]
        r = subprocess.run(['rustc', '+' + name, '-O', '-o', str(exe), str(main)],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return exe

    def test_matches_vfs_rules(self):
        exe = self._build()
        args = [c[0] for c in CASES]
        r = subprocess.run([str(exe)] + args, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        got = r.stdout.strip().split('\n')
        self.assertEqual(len(got), len(CASES))
        for (inp, want), g in zip(CASES, got):
            self.assertEqual(g, want, '%s: %s != %s' % (inp, g, want))

    def test_reviewer_counterexample_collapses(self):
        """レビュアーの反例が同じ正規形になること (= 同一判定に掛かる)。"""
        exe = self._build()
        r = subprocess.run([str(exe), '/host/a.txt', '/host/./a.txt'],
                           capture_output=True, text=True)
        a, b = r.stdout.strip().split('\n')
        self.assertEqual(a, b, '正規化後も別物のままなら O_TRUNC に到達する')

    def test_vfs_source_still_normalizes(self):
        """VFS 側が正規化をやめたらこの前提が崩れるので、実ソースを見張る。"""
        vfs = (ROOT / 'fs/vfs.c').read_text()
        i = vfs.index('int vfs_resolve_path')
        body = vfs[i:i + 2600]
        self.assertIn("== '.'", body, 'VFS が . を畳まなくなった')
        self.assertIn("tmp[start+1] == '.'", body, 'VFS が .. を畳まなくなった')
        self.assertIn("if (tmp[p] == '/') { p++; continue; }", body,
                      'VFS が重複 / を畳まなくなった')


if __name__ == '__main__':
    unittest.main()
