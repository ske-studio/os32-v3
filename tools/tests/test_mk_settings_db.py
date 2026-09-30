"""S0-T — 初期値 tsv → settings.db の生成とビルド統合のホスト試験。

見るもの (docs/archive/settings/TASK_S0.md §3):
  * 決定性 — 同じ内容 + 同じ epoch なら同じバイト列。mtime を見ない。
  * スキーマ — DESIGN.md §3 の 2 表、schema_version / user_version / page_size
    / journal_mode。
  * tsv の規則 — 違反は非ゼロ終了。末尾の空欄は「空の text」として入る。
  * mkpkg — 登録ファイルの欠損を warning で飛ばさずエラーにする。

実ファイルは temp dir だけ。エミュレータも配備も触らない。
"""
import glob
import hashlib
import os
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MK = ROOT / 'tools' / 'mk_settings_db.py'
MKPKG = ROOT / 'tools' / 'mkpkg.py'
DEFAULTS = ROOT / 'assets' / 'settings' / 'defaults.tsv'

GOOD_TSV = (
    "# コメント\n"
    "\n"
    "gshell\tdesktop/color\tint\t1\n"
    "gshell\tdesktop/wallpaper\ttext\t\n"
    "gshell\ttaskbar/clock_24h\tint\t1\n"
    "app:filer\twindow/main\tblob\t00ff10\n"
    "system\tlocale\ttext\tja_JP\n"
)


def run_mk(tsv_path, out_path, epoch=None, env=None):
    cmd = [sys.executable, '-B', str(MK), '--tsv', str(tsv_path),
           '--out', str(out_path)]
    if epoch is not None:
        cmd += ['--epoch', str(epoch)]
    e = dict(os.environ)
    e.pop('SOURCE_DATE_EPOCH', None)
    if env:
        e.update(env)
    return subprocess.run(cmd, capture_output=True, text=True, env=e)


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


class TempCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='os32-s0t-')
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write_tsv(self, name, text, mtime=None):
        p = self.dir / name
        if isinstance(text, bytes):
            p.write_bytes(text)
        else:
            p.write_text(text, encoding='utf-8', newline='')
        if mtime is not None:
            os.utime(p, (mtime, mtime))
        return p

    def build(self, text, epoch=0, out='out.db', **kw):
        tsv = self.write_tsv('in.tsv', text, **kw)
        db = self.dir / out
        return run_mk(tsv, db, epoch=epoch), db


class Determinism(TempCase):
    def test_same_content_different_mtime_same_bytes(self):
        """mtime が違う同内容の 2 ファイル + 同じ epoch → 同一 hash。"""
        a = self.write_tsv('a.tsv', GOOD_TSV, mtime=100000)
        b = self.write_tsv('b.tsv', GOOD_TSV, mtime=1700000000)
        da, db = self.dir / 'a.db', self.dir / 'b.db'
        self.assertEqual(run_mk(a, da, epoch=0).returncode, 0)
        self.assertEqual(run_mk(b, db, epoch=0).returncode, 0)
        self.assertEqual(sha256(da), sha256(db))

    def test_repeated_build_same_bytes(self):
        """同じ入力を 2 回ビルドしても同じ (T1 の「2 回ビルドして同一 hash」)。"""
        tsv = self.write_tsv('a.tsv', GOOD_TSV)
        d1, d2 = self.dir / '1.db', self.dir / '2.db'
        run_mk(tsv, d1, epoch=0)
        run_mk(tsv, d2, epoch=0)
        self.assertEqual(sha256(d1), sha256(d2))

    def test_epoch_changes_bytes(self):
        tsv = self.write_tsv('a.tsv', GOOD_TSV)
        d1, d2 = self.dir / '1.db', self.dir / '2.db'
        run_mk(tsv, d1, epoch=0)
        run_mk(tsv, d2, epoch=1700000000)
        self.assertNotEqual(sha256(d1), sha256(d2))

    def test_source_date_epoch_used_when_no_flag(self):
        tsv = self.write_tsv('a.tsv', GOOD_TSV)
        d1, d2 = self.dir / '1.db', self.dir / '2.db'
        run_mk(tsv, d1, env={'SOURCE_DATE_EPOCH': '1700000000'})
        run_mk(tsv, d2, epoch=1700000000)
        self.assertEqual(sha256(d1), sha256(d2))

    def test_default_epoch_is_zero(self):
        tsv = self.write_tsv('a.tsv', GOOD_TSV)
        d1, d2 = self.dir / '1.db', self.dir / '2.db'
        run_mk(tsv, d1)
        run_mk(tsv, d2, epoch=0)
        self.assertEqual(sha256(d1), sha256(d2))

    def test_rebuild_over_existing_output(self):
        """既存の出力に追記せず作り直す (FORCE 依存で毎回生成するため)。"""
        tsv = self.write_tsv('a.tsv', GOOD_TSV)
        db = self.dir / 'out.db'
        run_mk(tsv, db, epoch=0)
        first = sha256(db)
        run_mk(tsv, db, epoch=0)
        self.assertEqual(first, sha256(db))
        self.assertFalse((self.dir / 'out.db-journal').exists())


class Schema(TempCase):
    def setUp(self):
        TempCase.setUp(self)
        self.res, self.db = self.build(GOOD_TSV, epoch=0)
        self.assertEqual(self.res.returncode, 0, self.res.stderr)
        self.conn = sqlite3.connect(str(self.db))

    def tearDown(self):
        self.conn.close()
        TempCase.tearDown(self)

    def test_pragmas(self):
        self.assertEqual(self.conn.execute('PRAGMA page_size').fetchone()[0],
                         1024)
        self.assertEqual(self.conn.execute('PRAGMA user_version').fetchone()[0],
                         1)
        self.assertEqual(
            self.conn.execute('PRAGMA journal_mode').fetchone()[0].lower(),
            'delete')

    def test_meta(self):
        row = self.conn.execute(
            'SELECT schema_version, created FROM meta').fetchone()
        self.assertEqual(row[0], 1)
        self.assertEqual(row[1], '1970-01-01T00:00:00Z')

    def test_settings_table_shape(self):
        cols = [(r[1], r[2], r[5]) for r in
                self.conn.execute('PRAGMA table_info(settings)')]
        self.assertEqual([c[0] for c in cols],
                         ['scope', 'key', 'type', 'ival', 'tval', 'bval'])
        self.assertEqual([c[0] for c in cols if c[2]], ['scope', 'key'])
        sql = self.conn.execute(
            "SELECT sql FROM sqlite_master WHERE name='settings'").fetchone()[0]
        self.assertIn('WITHOUT ROWID', sql.upper())

    def test_row_count_and_values(self):
        rows = self.conn.execute(
            'SELECT scope, key, type, ival, tval, bval FROM settings '
            'ORDER BY scope, key').fetchall()
        self.assertEqual(len(rows), 5)
        d = dict(((r[0], r[1]), r) for r in rows)
        self.assertEqual(d[('gshell', 'desktop/color')][2:4], (0, 1))
        self.assertEqual(d[('system', 'locale')][2], 1)
        self.assertEqual(d[('system', 'locale')][4], 'ja_JP')
        self.assertEqual(d[('app:filer', 'window/main')][2], 2)
        self.assertEqual(bytes(d[('app:filer', 'window/main')][5]),
                         b'\x00\xff\x10')

    def test_trailing_empty_field_is_empty_text(self):
        """末尾の空欄は NULL ではなく空の text。"""
        row = self.conn.execute(
            "SELECT type, tval FROM settings "
            "WHERE scope='gshell' AND key='desktop/wallpaper'").fetchone()
        self.assertEqual(row[0], 1)
        self.assertIsNotNone(row[1])
        self.assertEqual(row[1], '')


class Rejects(TempCase):
    def setUp(self):
        TempCase.setUp(self)
        self.seq = 0

    def assertRejected(self, text, needle=None):
        # 1 メソッド内で何度も呼ぶので出力名を毎回変える
        # (直前の成功ビルドの残骸を「失敗なのに作った」と誤判定しない)。
        self.seq += 1
        res, db = self.build(text, out='rej%d.db' % self.seq)
        self.assertNotEqual(res.returncode, 0,
                            '受理されてしまった: %r' % text)
        self.assertTrue(res.stderr.strip(), '理由が出ていない')
        if needle:
            self.assertIn(needle, res.stderr)
        self.assertFalse(db.exists(), '失敗なのに DB を作っている')

    def test_int32_overflow(self):
        self.assertRejected("gshell\ta\tint\t2147483648\n")
        self.assertRejected("gshell\ta\tint\t-2147483649\n")

    def test_int32_bounds_accepted(self):
        res, db = self.build("gshell\ta\tint\t2147483647\n"
                             "gshell\tb\tint\t-2147483648\n")
        self.assertEqual(res.returncode, 0, res.stderr)

    def _ival(self, db, key='a'):
        conn = sqlite3.connect(str(db))
        try:
            return conn.execute(
                'SELECT ival FROM settings WHERE key=?', (key,)).fetchone()[0]
        finally:
            conn.close()

    def test_leading_zeros_accepted(self):
        """先頭ゼロは字句規則 `-?[0-9]+` を満たす = 契約上有効。

        Python の既定の桁数制限 (4300 桁) で `int()` が落ちないよう、
        変換の前に桁を畳むこと。
        """
        z = '0' * 4301
        res, db = self.build("gshell\ta\tint\t%s\n" % z, out='z.db')
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self._ival(db), 0)

        res, db = self.build("gshell\ta\tint\t-%s\n" % z, out='negz.db')
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self._ival(db), 0)

        res, db = self.build("gshell\ta\tint\t%s2147483647\n" % z,
                             out='maxz.db')
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self._ival(db), 2147483647)

        res, db = self.build("gshell\ta\tint\t-%s2147483648\n" % z,
                             out='minz.db')
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self._ival(db), -2147483648)

    def test_leading_zeros_beyond_int32_rejected(self):
        """桁を畳んでも範囲外なら、理由付きで拒否する (Traceback にしない)。"""
        z = '0' * 4301
        self.assertRejected("gshell\ta\tint\t%s2147483648\n" % z, '範囲外')
        self.assertRejected("gshell\ta\tint\t-%s2147483649\n" % z, '範囲外')

    def test_int_lexical(self):
        self.assertRejected("gshell\ta\tint\t0x10\n")
        self.assertRejected("gshell\ta\tint\t 1\n")
        self.assertRejected("gshell\ta\tint\t+1\n")
        self.assertRejected("gshell\ta\tint\t\n")

    def test_invalid_utf8(self):
        self.assertRejected(b"gshell\ta\ttext\t\xff\xfe\n", 'UTF-8')

    def test_nul_in_text(self):
        self.assertRejected(b"gshell\ta\ttext\tx\x00y\n", 'NUL')

    def test_nul_in_scope(self):
        self.assertRejected(b"gsh\x00ell\ta\ttext\tx\n", 'NUL')

    def test_key_rules(self):
        self.assertRejected("gshell\tDesktop\tint\t1\n")
        self.assertRejected("gshell\tdesktop/\tint\t1\n")
        self.assertRejected("gshell\t/desktop\tint\t1\n")
        self.assertRejected("gshell\tdesk top\tint\t1\n")
        self.assertRejected("gshell\t\tint\t1\n")
        self.assertRejected("gshell\t%s\tint\t1\n" % ('a' * 64))

    def test_key_63_bytes_accepted(self):
        res, _ = self.build("gshell\t%s\tint\t1\n" % ('a' * 63))
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_scope_rules(self):
        self.assertRejected("SYSTEM\ta\tint\t1\n")
        self.assertRejected("app:\ta\tint\t1\n")
        self.assertRejected("app:My App\ta\tint\t1\n")
        self.assertRejected("\ta\tint\t1\n")
        self.assertRejected("app:%s\ta\tint\t1\n" % ('n' * 64))

    def test_type_rules(self):
        self.assertRejected("gshell\ta\tINT\t1\n")
        self.assertRejected("gshell\ta\tstring\tx\n")

    def test_duplicate_key(self):
        self.assertRejected("gshell\ta\tint\t1\ngshell\ta\tint\t2\n", '重複')

    def test_column_count(self):
        self.assertRejected("gshell\ta\tint\n")
        self.assertRejected("gshell\ta\tint\t1\textra\n")
        self.assertRejected("gshell a int 1\n")

    def test_cr_rejected(self):
        self.assertRejected(b"gshell\ta\tint\t1\r\n", 'CR')
        # text の値に付いた CR は他の規則では落ちない (CR 規則だけが捕まえる)
        self.assertRejected(b"gshell\ta\ttext\tx\r\n", 'CR')
        self.assertRejected(b"# comment\r\ngshell\ta\ttext\tx\n", 'CR')

    def test_text_255_boundary(self):
        res, _ = self.build("gshell\ta\ttext\t%s\n" % ('x' * 255))
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertRejected("gshell\ta\ttext\t%s\n" % ('x' * 256))
        # 3 バイト文字 86 個 = 258B
        self.assertRejected("gshell\ta\ttext\t%s\n" % ('あ' * 86))

    def test_blob_rules(self):
        self.assertRejected("gshell\ta\tblob\t0f0\n", '奇数')
        self.assertRejected("gshell\ta\tblob\t00 ff\n")
        self.assertRejected("gshell\ta\tblob\t00zz\n")
        self.assertRejected("gshell\ta\tblob\t%s\n" % ('ab' * 4097))

    def test_blob_4096_boundary(self):
        res, db = self.build("gshell\ta\tblob\t%s\n" % ('ab' * 4096))
        self.assertEqual(res.returncode, 0, res.stderr)
        conn = sqlite3.connect(str(db))
        n = conn.execute('SELECT length(bval) FROM settings').fetchone()[0]
        conn.close()
        self.assertEqual(n, 4096)

    def test_comment_and_blank_lines_only(self):
        res, db = self.build("# only comments\n\n")
        self.assertEqual(res.returncode, 0, res.stderr)
        conn = sqlite3.connect(str(db))
        self.assertEqual(
            conn.execute('SELECT COUNT(*) FROM settings').fetchone()[0], 0)
        conn.close()

    def test_error_names_the_line(self):
        res, _ = self.build("gshell\ta\tint\t1\n"
                            "# c\n"
                            "gshell\tb\tint\tzz\n")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(':3:', res.stderr)


class RealDefaults(unittest.TestCase):
    """リポジトリの正典 tsv がそのまま通ること。"""

    def test_defaults_tsv_builds(self):
        self.assertTrue(DEFAULTS.is_file(), '%s が無い' % DEFAULTS)
        with tempfile.TemporaryDirectory(prefix='os32-s0t-def-') as d:
            db = pathlib.Path(d) / 'settings.db'
            res = run_mk(DEFAULTS, db, epoch=0)
            self.assertEqual(res.returncode, 0, res.stderr)
            conn = sqlite3.connect(str(db))
            rows = conn.execute(
                'SELECT scope, key, type, ival, tval FROM settings '
                'ORDER BY scope, key').fetchall()
            ver = conn.execute(
                'SELECT schema_version FROM meta').fetchone()[0]
            conn.close()
        self.assertEqual(ver, 1)
        self.assertEqual(rows, [
            ('gshell', 'desktop/color', 0, 12, None),
            ('gshell', 'desktop/wallpaper', 1, None, ''),
            ('gshell', 'taskbar/clock_24h', 0, 1, None),
        ])

    def test_defaults_tsv_is_lf_and_utf8(self):
        raw = DEFAULTS.read_bytes()
        self.assertNotIn(b'\r', raw)
        raw.decode('utf-8')


class MkpkgMissingFile(TempCase):
    """登録ファイルの欠損は warning ではなくエラー (非ゼロ)。"""

    def _defs(self, host_rel):
        p = self.dir / 'defs.yaml'
        p.write_text(
            "minimal:\n"
            "  type: package\n"
            "  version: 1\n"
            "  lzss: false\n"
            "  files:\n"
            "    - host: %s\n"
            "      guest: /etc/settings.db\n" % host_rel,
            encoding='utf-8')
        return p

    def _run(self, *defs):
        self.out = self.dir / 'packages'
        cmd = [sys.executable, '-B', str(MKPKG)]
        for d in defs:
            cmd += ['--defs', str(d)]
        cmd += ['--output', str(self.out), '--base', str(self.dir)]
        return subprocess.run(cmd, capture_output=True, text=True)

    def pkgs_written(self):
        if not self.out.is_dir():
            return []
        return sorted(p.name for p in self.out.iterdir())

    def _two_packages(self, second_host):
        """1 つ目は揃っている / 2 つ目が欠損、の定義を書く。"""
        (self.dir / 'build' / 'out').mkdir(parents=True, exist_ok=True)
        (self.dir / 'build' / 'out' / 'vmkernel.lz4').write_bytes(b'K' * 32)
        p = self.dir / 'defs.yaml'
        p.write_text(
            "minimal:\n"
            "  type: package\n"
            "  version: 1\n"
            "  lzss: false\n"
            "  files:\n"
            "    - host: build/out/vmkernel.lz4\n"
            "      guest: /boot/vmkernel.lz4\n"
            "append:\n"
            "  type: package\n"
            "  version: 1\n"
            "  lzss: false\n"
            "  files:\n"
            "    - host: \"%s\"\n"
            "      guest: /data/\n" % second_host, encoding='utf-8')
        return p

    def test_missing_file_is_error(self):
        res = self._run(self._defs('build/out/settings.db'))
        self.assertNotEqual(res.returncode, 0,
                            '欠損が素通りしている: %s' % res.stdout)
        self.assertIn('settings.db', res.stdout + res.stderr)

    def test_later_package_missing_writes_nothing(self):
        """後半のパッケージが欠損したら、前半の .PKG も書かない。"""
        res = self._run(self._two_packages('build/out/settings.db'))
        self.assertNotEqual(res.returncode, 0)
        self.assertEqual(self.pkgs_written(), [],
                         '欠損があるのに .PKG を書いている')

    def test_later_package_empty_glob_writes_nothing(self):
        """0 件の glob も欠損。正常側の .PKG を書いて成功にしない。"""
        res = self._run(self._two_packages('build/out/settings*.db'))
        self.assertNotEqual(res.returncode, 0,
                            '0 件の glob が素通りしている: %s' % res.stdout)
        self.assertEqual(self.pkgs_written(), [],
                         '欠損があるのに .PKG を書いている')

    def test_missing_defs_is_error(self):
        """存在しない --defs を黙って無視しない (定義ごと落ちる)。"""
        ok = self._defs('build/out/settings.db')
        (self.dir / 'build' / 'out').mkdir(parents=True, exist_ok=True)
        (self.dir / 'build' / 'out' / 'settings.db').write_bytes(b'DB')
        res = self._run(ok, self.dir / 'nosuch.yaml')
        self.assertNotEqual(res.returncode, 0,
                            '欠けた --defs が素通りしている: %s' % res.stdout)
        self.assertIn('nosuch.yaml', res.stdout + res.stderr)
        self.assertEqual(self.pkgs_written(), [])

    def test_present_file_is_ok(self):
        (self.dir / 'build' / 'out').mkdir(parents=True)
        (self.dir / 'build' / 'out' / 'settings.db').write_bytes(b'DB' * 16)
        res = self._run(self._defs('build/out/settings.db'))
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertTrue((self.dir / 'packages' / 'MINIMAL.PKG').is_file())

    def _glob_defs(self):
        p = self.dir / 'defs.yaml'
        p.write_text(
            "append:\n"
            "  type: package\n"
            "  version: 1\n"
            "  lzss: false\n"
            "  files:\n"
            "    - host: \"assets/images/*.vbz\"\n"
            "      guest: /data/images/\n", encoding='utf-8')
        return p

    def test_glob_without_match_is_error(self):
        """0 件の glob は「登録したのに入らなかった」なのでエラー。"""
        res = self._run(self._glob_defs())
        self.assertNotEqual(res.returncode, 0,
                            '0 件の glob が素通りしている: %s' % res.stdout)
        self.assertIn('*.vbz', res.stdout + res.stderr)
        self.assertEqual(self.pkgs_written(), [])

    def test_glob_with_match_is_ok(self):
        (self.dir / 'assets' / 'images').mkdir(parents=True)
        (self.dir / 'assets' / 'images' / 'a.vbz').write_bytes(b'VBZ')
        res = self._run(self._glob_defs())
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self.pkgs_written(), ['APPEND.PKG'])


def read_mk(name):
    """継続行を畳み、単純な変数代入を展開した Makefile 断片を返す。"""
    text = (ROOT / 'build' / name).read_text(encoding='utf-8')
    text = text.replace('\\\n', ' ')
    for _ in range(4):
        for var, val in (('SETTINGS_DB', '$(BUILD_OUT)/settings.db'),
                         ('SETTINGS_TSV', 'assets/settings/defaults.tsv')):
            text = text.replace('$(%s)' % var, val)
    return text


def mk_prereqs(text, target):
    """target: の前提条件 (継続行を畳んだ 1 行) を返す。"""
    body = text.split('\n' + target + ':', 1)[1]
    return body.split('\n', 1)[0]


class RealPackageDefs(unittest.TestCase):
    """リポジトリ自身のパッケージ構成が厳格化した mkpkg と噛み合うか。

    構成 (build/packages.yaml) は配備マニフェストのタグから中身を作る。
    振り分けの問題 (当たらないタグ、古い除外 …) があると `make iso` が落ちる。
    それをビルドではなくここで先に見つける。ファイルの有無は見ない。
    """

    def test_plan_has_no_problems(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        import mkpkg
        plan = mkpkg.load_plan(str(ROOT / 'build' / 'packages.yaml'))
        _, problems = mkpkg.expand_plan(plan, str(ROOT))
        self.assertEqual(problems, [], 'パッケージ構成に問題がある')


class BuildWiring(unittest.TestCase):
    """Makefile 側の結線 (文面の検査だけ。make は動かさない)。"""

    def test_assets_mk_rule(self):
        text = read_mk('assets.mk')
        self.assertIn('$(BUILD_OUT)/settings.db:', text)
        rule = mk_prereqs(text, '$(BUILD_OUT)/settings.db')
        self.assertIn('assets/settings/defaults.tsv', rule)
        self.assertIn('tools/mk_settings_db.py', rule)
        self.assertIn('FORCE', rule)

    def test_assets_mk_all_and_clean(self):
        text = read_mk('assets.mk')
        self.assertIn('$(BUILD_OUT)/settings.db', mk_prereqs(text, 'all'))
        assets_all = text.split('ASSETS_ALL =', 1)[1].split('\n\n', 1)[0]
        self.assertIn('$(BUILD_OUT)/settings.db', assets_all)
        clean = text.split('clean-assets:', 1)[1].split('\n\n', 1)[0]
        self.assertIn('$(ASSETS_ALL)', clean)

    def test_image_mk_wiring(self):
        text = read_mk('image.mk')
        # FD の中身は packages.yaml の MINIMAL (files: の settings.db を含む) から
        # mkpkg --fd-args が作る (2026-09-24〜)。依存は FDD_IMAGE_DEPS にまとめてある
        self.assertIn('--fd-args', text)
        deps = text.split('FDD_IMAGE_DEPS =', 1)[1].split('\n', 1)[0]
        self.assertIn('$(BUILD_OUT)/settings.db', deps)
        for target in ('images/os32_boot.d88', 'images/os32_boot144.img'):
            self.assertIn('$(FDD_IMAGE_DEPS)', mk_prereqs(text, target))
        pkg = mk_prereqs(text, 'packages')
        for dep in ('$(BUILD_OUT)/settings.db', '$(BUILD_OUT)/vmkernel.lz4',
                    'unicode_bin', 'boot'):
            self.assertIn(dep, pkg, 'packages の依存に %s が無い' % dep)

    def test_packages_plan_has_settings_db(self):
        text = (ROOT / 'build' / 'packages.yaml').read_text(
            encoding='utf-8')
        self.assertIn('guest: /etc/settings.db', text)
        self.assertIn('host: build/out/settings.db', text)


if __name__ == '__main__':
    unittest.main()
