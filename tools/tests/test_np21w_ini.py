"""Host-only tests: all file inputs are synthetic temporary fixtures."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / 'np21w_ini.py'
if SCRIPT.exists():
    spec = importlib.util.spec_from_file_location('np21w_ini', SCRIPT)
    ini = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ini)
else:
    ini = None

RAW = (b'; opaque \x82\xa0\xff\r\n[NekoProject21]\r\n'
       b' USEGD5430 = false \t; keep\r\nGD5430TYPE=91\nUSEPEGCP=false\nExMemory=16\n'
       b'private=DO_NOT_PRINT\r\n[other]\r\nUSEGD5430=false')
CHANGES = {'USEGD5430': 'true'}
EXPECTED = RAW.replace(b'= false', b'= true', 1)


class ExMemory(unittest.TestCase):
    """ExMemory は MB 単位の拡張メモリ (win9x/ini.cpp:477, PFTYPE_UINT16、
    SUPPORT_LARGE_MEMORY 有効時)。ブートローダが 1MB から 512KB 刻みで実測するので
    (boot/loader_fat.asm:248)、ゲストの総容量はこの値から決まる。
    8MB は CUI の最低動作環境で、memory_boot の legacy フォールバック経路を通す。"""

    def test_switch_between_proven_sizes(self):
        small, diff = ini.transform(RAW, {'EXMEMORY': '7'})
        self.assertEqual(diff, ['EXMEMORY: 16 -> 7'])
        self.assertEqual(small, RAW.replace(b'ExMemory=16', b'ExMemory=7'))
        back, diff = ini.transform(small, {'EXMEMORY': '16'})
        self.assertEqual((back, diff), (RAW, ['EXMEMORY: 7 -> 16']))

    def test_rejects_unproven_sizes(self):
        for value in ('0', '1', '13', '32', '128', '', '16 ', '0x10'):
            with self.assertRaises(ini.IniError):
                ini.transform(RAW, {'EXMEMORY': value})

    def test_nine_mb_is_the_pegc_gui_floor(self):
        """ExMemory=8 -> ゲスト 9MB。640x480 PEGC の 300KB 予約が
        固定アプリ帯 0x500000-0x800000 の外に出る最小構成 (2026-09-09 実測)。"""
        out, diff = ini.transform(RAW, {'EXMEMORY': '8'})
        self.assertEqual(diff, ['EXMEMORY: 16 -> 8'])
        self.assertEqual(out, RAW.replace(b'ExMemory=16', b'ExMemory=8'))

    def test_does_not_disturb_graphics_fields(self):
        out, diff = ini.transform(RAW, {'EXMEMORY': '7'})
        self.assertIn(b' USEGD5430 = false \t; keep', out)
        self.assertIn(b'GD5430TYPE=91', out)
        self.assertIn(b'USEPEGCP=false', out)
        self.assertEqual(diff, ['EXMEMORY: 16 -> 7'])

    def test_missing_or_unknown_fails_closed(self):
        for raw in (RAW.replace(b'ExMemory=16\n', b''),
                    RAW.replace(b'ExMemory=16', b'ExMemory=13'),
                    RAW.replace(b'ExMemory=16', b'ExMemory=16\nexmemory=16')):
            with self.assertRaises(ini.IniError):
                ini.transform(raw, {'USEGD5430': 'true'})


class Pegc(unittest.TestCase):
    """USEPEGCP gates np2cfg.usepegcplane -> pegc.enable (win9x/ini.cpp:687,
    io/pegc.c:375). Same [NekoProject21] table and PFTYPE_BOOL as USEGD5430."""

    def test_pegc_on_off_round_trip(self):
        on, diff = ini.transform(RAW, {'USEPEGCP': 'true'})
        self.assertEqual(diff, ['USEPEGCP: false -> true'])
        self.assertEqual(on, RAW.replace(b'USEPEGCP=false', b'USEPEGCP=true'))
        back, diff = ini.transform(on, {'USEPEGCP': 'false'})
        self.assertEqual((back, diff), (RAW, ['USEPEGCP: true -> false']))

    def test_pegc_is_independent_of_cirrus(self):
        out, diff = ini.transform(RAW, {'USEPEGCP': 'true'})
        self.assertIn(b' USEGD5430 = false \t; keep', out)
        self.assertIn(b'GD5430TYPE=91', out)
        self.assertEqual(diff, ['USEPEGCP: false -> true'])

    def test_rejects_bad_pegc_values(self):
        for value in ('1', 'True', 'yes', '', 'true\nprivate=x'):
            with self.assertRaises(ini.IniError):
                ini.transform(RAW, {'USEPEGCP': value})

    def test_missing_or_duplicate_pegc_field_fails_closed(self):
        for raw in (RAW.replace(b'USEPEGCP=false\n', b''),
                    RAW.replace(b'USEPEGCP=false', b'USEPEGCP=false\nusepegcp=false'),
                    RAW.replace(b'USEPEGCP=false', b'USEPEGCP=unknown')):
            with self.assertRaises(ini.IniError):
                ini.transform(raw, {'USEGD5430': 'true'})


class Availability(unittest.TestCase):
    def test_implementation_exists(self):
        self.assertTrue(SCRIPT.is_file(), 'np21w_ini.py must implement the tested contract')


@unittest.skipIf(ini is None, 'implementation not yet written (assertion RED)')
class Transformation(unittest.TestCase):
    def test_preserves_unrelated_bytes(self):
        result, diff = ini.transform(RAW, CHANGES)
        self.assertEqual(result, EXPECTED)
        self.assertEqual(diff, ['USEGD5430: false -> true'])

    def test_bom_newlines_and_no_final_newline(self):
        for prefix in (b'', b'\xef\xbb\xbf'):
            for newline in (b'\r\n', b'\n', b'\r'):
                raw = prefix + newline.join((b'[NekoProject21]', b'USEGD5430=false',
                                             b'GD5430TYPE=91', b'USEPEGCP=false',
                                             b'ExMemory=16'))
                result, _ = ini.transform(raw, CHANGES)
                self.assertEqual(result, raw.replace(b'USEGD5430=false', b'USEGD5430=true'))

    def test_noop_and_disable(self):
        self.assertEqual(ini.transform(EXPECTED, CHANGES), (EXPECTED, []))
        self.assertEqual(ini.transform(EXPECTED, {'USEGD5430': 'false'})[0], RAW)

    def test_allowlist_rejects_unproven_values(self):
        for changes in ({'WAB_ANSW': '1'}, {'GD5430TYPE': '0x5b'},
                        {'GD5430TYPE': '65535'}, {'USEGD5430': '1'},
                        {'USEGD5430': 'true\nprivate=x'}, {}, {'usegd5430': 'true'}):
            with self.subTest(changes=changes), self.assertRaises(ini.IniError):
                ini.transform(RAW, changes)

    def test_missing_duplicate_and_malformed(self):
        for raw in (b'[other]\nUSEGD5430=false\n',
                    RAW.replace(b'GD5430TYPE=91', b''),
                    RAW.replace(b'GD5430TYPE=91', b'GD5430TYPE=91\ngd5430type=91'),
                    RAW + b'\n[NekoProject21]\n',
                    RAW.replace(b'GD5430TYPE=91', b'GD5430TYPE=91\nusegd5430=false'),
                    RAW.replace(b'GD5430TYPE=91', b'GD5430TYPE=91\n[broken'),
                    RAW.replace(b'GD5430TYPE=91', b'GD5430TYPE=unknown'),
                    b'\xff\xfe' + RAW, RAW + b'\x00'):
            with self.subTest(raw=raw[:24]), self.assertRaises(ini.IniError):
                ini.transform(raw, CHANGES)


@unittest.skipIf(ini is None, 'implementation not yet written (assertion RED)')
class OfflineFiles(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'synthetic.snapshot'
        self.source.write_bytes(RAW)

    def test_unique_backup_atomic_readback_and_restore(self):
        a = ini.prepare(self.source, self.root, CHANGES)
        b = ini.prepare(self.source, self.root, CHANGES)
        self.assertNotEqual(a, b)
        self.assertEqual(self.source.read_bytes(), RAW)
        self.assertEqual((a / 'original.bin').read_bytes(), RAW)
        self.assertEqual((a / 'prepared.bin').read_bytes(), EXPECTED)
        self.assertEqual(ini.restore(a), ['USEGD5430: true -> false'])
        self.assertEqual((a / 'prepared.bin').read_bytes(), EXPECTED)
        ini.restore(a, apply=True)
        self.assertEqual((a / 'prepared.bin').read_bytes(), RAW)
        self.assertEqual((a / 'original.bin').read_bytes(), RAW)
        with self.assertRaises(ini.IniError):
            ini.restore(a, apply=True)

    def test_intervening_modification_refuses_restore(self):
        for name in ('prepared.bin', 'original.bin', 'receipt.json'):
            bundle = ini.prepare(self.source, self.root, CHANGES)
            path = bundle / name
            path.write_bytes(path.read_bytes() + b'changed')
            before = (bundle / 'prepared.bin').read_bytes()
            with self.subTest(name=name), self.assertRaises(ini.IniError):
                ini.restore(bundle, apply=True)
            self.assertEqual((bundle / 'prepared.bin').read_bytes(), before)

    def test_same_bytes_replacement_refuses_restore(self):
        bundle = ini.prepare(self.source, self.root, CHANGES)
        replacement = bundle / 'replacement'
        replacement.write_bytes(EXPECTED)
        replacement.replace(bundle / 'prepared.bin')
        with self.assertRaises(ini.IniError):
            ini.restore(bundle, apply=True)
        self.assertEqual((bundle / 'prepared.bin').read_bytes(), EXPECTED)

    def test_receipt_cannot_authorize_unrelated_rewrite(self):
        bundle = ini.prepare(self.source, self.root, CHANGES)
        path = bundle / 'receipt.json'
        receipt = json.loads(path.read_bytes())
        receipt['changes'] = {'private': 'replacement'}
        path.write_text(json.dumps(receipt))
        with self.assertRaises(ini.IniError):
            ini.restore(bundle, apply=True)
        self.assertEqual((bundle / 'prepared.bin').read_bytes(), EXPECTED)

    def test_restore_cli_defaults_to_dry_run(self):
        bundle = ini.prepare(self.source, self.root, CHANGES)
        code, out, _ = self.run_cli(['restore', str(bundle)])
        self.assertEqual((code, out), (0, 'USEGD5430: true -> false\n'))
        self.assertEqual((bundle / 'prepared.bin').read_bytes(), EXPECTED)
        code, _, _ = self.run_cli(['restore', str(bundle), '--apply'])
        self.assertEqual(code, 0)
        self.assertEqual((bundle / 'prepared.bin').read_bytes(), RAW)

    def test_invalid_preparation_creates_nothing(self):
        with self.assertRaises(ini.IniError):
            ini.prepare(self.source, self.root, {'GD5430TYPE': '0'})
        self.assertEqual(list(self.root.iterdir()), [self.source])

    def test_restore_rechecks_immediately_before_replace(self):
        bundle = ini.prepare(self.source, self.root, CHANGES)
        real = ini._new_file
        def intervene(fd, name, data):
            real(fd, name, data)
            (bundle / 'prepared.bin').write_bytes(b'intervening bytes')
        with mock.patch.object(ini, '_new_file', side_effect=intervene):
            with self.assertRaises(ini.IniError):
                ini.restore(bundle, apply=True)
        self.assertEqual((bundle / 'prepared.bin').read_bytes(), b'intervening bytes')
        self.assertEqual((bundle / 'original.bin').read_bytes(), RAW)

    def test_symlinks_rejected_including_parents(self):
        link = self.root / 'link'
        link.symlink_to(self.source)
        with self.assertRaises(ini.IniError):
            ini.prepare(link, self.root, CHANGES)
        link.unlink()
        link.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ini.IniError):
            ini.prepare(link / self.source.name, self.root, CHANGES)
        with self.assertRaises(ini.IniError):
            ini.prepare(self.source, link, CHANGES)
        bundle = ini.prepare(self.source, self.root, CHANGES)
        (bundle / 'prepared.bin').unlink()
        (bundle / 'prepared.bin').symlink_to(self.source)
        with self.assertRaises(ini.IniError):
            ini.restore(bundle, apply=True)
        self.assertEqual(self.source.read_bytes(), RAW)

    def test_hardlink_and_nonregular_rejected(self):
        os.link(self.source, self.root / 'hard')
        with self.assertRaises(ini.IniError):
            ini.prepare(self.source, self.root, CHANGES)
        with self.assertRaises(ini.IniError):
            ini.prepare(self.root, self.root, CHANGES)

    def test_atomic_failure_preserves_backup_and_source(self):
        with mock.patch.object(ini.os, 'replace', side_effect=OSError('synthetic failure')):
            with self.assertRaises(OSError):
                ini.prepare(self.source, self.root, CHANGES)
        bundle, = self.root.glob('np21w-offline-*')
        self.assertEqual((bundle / 'original.bin').read_bytes(), RAW)
        self.assertFalse((bundle / 'prepared.bin').exists())
        self.assertEqual(self.source.read_bytes(), RAW)

    def test_readback_failure_is_reported(self):
        real = ini.os.replace
        def corrupt(src, dst, **kwargs):
            real(src, dst, **kwargs)
            fd = os.open(dst, os.O_WRONLY, dir_fd=kwargs['dst_dir_fd'])
            try:
                os.write(fd, b'!')
            finally:
                os.close(fd)
        with mock.patch.object(ini.os, 'replace', side_effect=corrupt):
            with self.assertRaises(ini.IniError):
                ini.prepare(self.source, self.root, CHANGES)
        self.assertEqual(self.source.read_bytes(), RAW)

    def run_cli(self, args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = ini.main(args)
        self.assertNotIn('DO_NOT_PRINT', out.getvalue() + err.getvalue())
        return code, out.getvalue(), err.getvalue()

    def test_cli_dry_run_and_explicit_offline_apply(self):
        args = ['prepare', str(self.source), '--set', 'USEGD5430=true']
        code, out, _ = self.run_cli(args)
        self.assertEqual(code, 0)
        self.assertEqual(out, 'USEGD5430: false -> true\n')
        self.assertEqual(list(self.root.iterdir()), [self.source])
        code, _, _ = self.run_cli(args + ['--output', str(self.root), '--apply'])
        self.assertEqual(code, 0)
        self.assertEqual(self.source.read_bytes(), RAW)

    def test_live_apply_fails_before_any_read(self):
        with mock.patch.object(ini, 'read_snapshot', side_effect=AssertionError('must not read')):
            code, _, err = self.run_cli(['prepare', 'not-read.ini', '--set',
                                          'USEGD5430=true', '--apply'])
        self.assertEqual(code, 2)
        self.assertIn('verifier', err)

    def test_repeated_assignment_rejected(self):
        code, _, _ = self.run_cli(['prepare', str(self.source), '--set',
                                   'USEGD5430=true', '--set', 'USEGD5430=false'])
        self.assertEqual(code, 2)




# --- 票 S3I2-T (2a): ALLOWED_PATHS = HDD1FILE / FDD1FILE / FDD2FILE ---------
# 実 ini・実プロセス・実 NP21W_DIR には触れない。wslpath は贋物 (-w / -u の
# 両方)、イメージの存在確認は temp dir の実ファイルで行う。
WIN_DIR = 'C:\\Fixture Dir'
CP932_D88 = b'C:\\np21w\\\x93\xfa\x96{\x8c\xea.d88'
PATHS_RAW = (b'; opaque \x82\xa0\xff\r\n[NekoProject21]\r\n'
             b' USEGD5430 = false \t; keep\r\nGD5430TYPE=91\nUSEPEGCP=false\nExMemory=16\n'
             b'HDD1FILE=C:\\np21w\\work.nhd\r\nFDD1FILE=' + CP932_D88 + b'\r\nFDD2FILE=\n'
             b'private=DO_NOT_PRINT\r\n[other]\r\nHDD1FILE=C:\\elsewhere\\other.nhd')
EJECT = {'FDD1FILE': '', 'FDD2FILE': ''}
FRESH = WIN_DIR + '\\os32_fresh.nhd'


class WslpathFake:
    """`wslpath -w` / `-u` の贋物。往復も贋物の表で答える。"""

    def __init__(self, mapping):
        self.mapping = dict(mapping)
        self.output = None

    def __call__(self, argv, **kwargs):
        assert argv[0] == 'wslpath' and argv[1] in ('-w', '-u'), argv
        if self.output is not None and argv[1] == '-w':
            text = self.output
        elif argv[1] == '-w':
            text = self.mapping[argv[2]]
        else:
            back = {v: k for k, v in self.mapping.items()}
            # 未知の Windows パスも実物同様どこかへ翻訳される (同じ対象とは限らない)。
            text = back.get(argv[2], '/mnt/c/' + argv[2][3:].replace('\\', '/').lower())
        return types.SimpleNamespace(returncode=0, stdout=text.encode('cp932') + b'\r\n')


@unittest.skipIf(ini is None, 'implementation not yet written (assertion RED)')
class PathFields(unittest.TestCase):
    """HDD1FILE / FDD1FILE / FDD2FILE。fail closed / CP932 保持 / 重複拒否は
    固定値キーと同じ作法で、存在要求だけを ALLOWED と別々に適用する。
    transform() は**解決済みの絶対パス**だけを受け取り、環境変数を読まない。"""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = self.temp.name
        Path(self.dir, 'os32_fresh.nhd').write_bytes(b'')
        self.wslpath = WslpathFake({self.dir: WIN_DIR})
        patcher = mock.patch.dict(os.environ, {'NP21W_DIR': self.dir})
        patcher.start()
        self.addCleanup(patcher.stop)
        runner = mock.patch.object(ini.subprocess, 'run', side_effect=self.wslpath)
        runner.start()
        self.addCleanup(runner.stop)

    def test_resolve_image_returns_the_host_and_windows_pair(self):
        host, windows = ini.resolve_image('os32_fresh.nhd', '.nhd')
        self.assertEqual(host, os.path.join(self.dir, 'os32_fresh.nhd'))
        self.assertEqual(windows, FRESH)

    def test_resolve_rejects_names_outside_the_rule_or_absent_on_the_host(self):
        for name in ('missing.nhd', 'os32_fresh.NHD', 'os32_fresh',
                     'sub/os32_fresh.nhd', '..\\os32_fresh.nhd', '../os32_fresh.nhd',
                     '.nhd', '.hidden.nhd', 'a..b.nhd', '\u65e5\u672c.nhd',
                     'os32 fresh.nhd', '', FRESH, None, 1):
            with self.subTest(name=name), self.assertRaises(ini.IniError):
                ini.resolve_image(name, '.nhd')

    def test_a_directory_or_symlink_is_not_a_disk_image(self):
        """`.nhd` という名前のディレクトリ / symlink を受理しない (往復 1 の B5)。"""
        os.mkdir(os.path.join(self.dir, 'as_dir.nhd'))
        os.symlink(os.path.join(self.dir, 'os32_fresh.nhd'),
                   os.path.join(self.dir, 'as_link.nhd'))
        for name in ('as_dir.nhd', 'as_link.nhd'):
            with self.subTest(name=name), self.assertRaises(ini.IniError):
                ini.resolve_image(name, '.nhd')
        self.assertEqual(ini.resolve_image('os32_fresh.nhd', '.nhd')[1], FRESH)

    def test_trailing_space_survives_to_the_windows_check(self):
        """`.strip()` で末尾空白を落とすと存在確認先と ini の書き先がずれる
        (往復 1 の B3)。終端改行だけ除去し、空白は windows_path が拒否する。"""
        for output in ('C:\\Trial ', 'C:\\Trial \\sub'):
            self.wslpath.output = output
            with self.subTest(output=output), self.assertRaises(ini.IniError):
                ini.resolve_image('os32_fresh.nhd', '.nhd')

    def test_conversion_must_round_trip_to_the_same_directory(self):
        self.wslpath.output = 'C:\\Elsewhere'
        with self.assertRaises(ini.IniError):
            ini.resolve_image('os32_fresh.nhd', '.nhd')

    def test_hdd_value_is_the_resolved_absolute_path(self):
        out, diff = ini.transform(PATHS_RAW, {'HDD1FILE': FRESH})
        self.assertEqual(diff, ['HDD1FILE: set -> ' + FRESH])
        self.assertEqual(out, PATHS_RAW.replace(b'HDD1FILE=C:\\np21w\\work.nhd',
                                                b'HDD1FILE=' + FRESH.encode('ascii'), 1))
        self.assertIn(b'[other]\r\nHDD1FILE=C:\\elsewhere\\other.nhd', out)

    def test_transform_never_reads_the_environment(self):
        with mock.patch.object(ini, 'np21w_directory',
                               side_effect=AssertionError('must not resolve')):
            self.assertEqual(ini.transform(PATHS_RAW, {'HDD1FILE': FRESH})[1],
                             ['HDD1FILE: set -> ' + FRESH])
            self.assertEqual(ini.transform(PATHS_RAW, EJECT)[1], ['FDD1FILE: set -> empty'])

    def test_transform_rejects_unresolved_or_malformed_path_values(self):
        for value in ('os32_fresh.nhd', WIN_DIR, WIN_DIR + '\\os32_fresh.d88',
                      WIN_DIR + '\\..\\os32_fresh.nhd', 'C:\\dir\\\u65e5.nhd',
                      'C:\\dir\\' + 'a' * 240 + '.nhd', None, 1):
            with self.subTest(value=value), self.assertRaises(ini.IniError):
                ini.transform(PATHS_RAW, {'HDD1FILE': value})

    def test_semicolon_and_hash_are_refused_on_the_way_out_too(self):
        """自分が書いた ini を同じ変更で読み直せること (往復 1 の B4)。"""
        for value in ('C:\\Trial#1\\os32_fresh.nhd', 'C:\\Trial;1\\os32_fresh.nhd'):
            with self.subTest(value=value), self.assertRaises(ini.IniError):
                ini.transform(PATHS_RAW, {'HDD1FILE': value})
        for windows in ('C:\\Trial#1', 'C:\\Trial;1'):
            self.wslpath.output = windows
            with self.subTest(windows=windows), self.assertRaises(ini.IniError):
                ini.resolve_image('os32_fresh.nhd', '.nhd')
        changes = dict(EJECT, HDD1FILE=FRESH)
        once = ini.transform(PATHS_RAW, changes)[0]
        self.assertEqual(ini.transform(once, changes), (once, []))

    def test_fdd_fields_only_detach_and_keep_every_other_byte(self):
        out, diff = ini.transform(PATHS_RAW, EJECT)
        self.assertEqual(diff, ['FDD1FILE: set -> empty'])
        self.assertEqual(out, PATHS_RAW.replace(b'FDD1FILE=' + CP932_D88, b'FDD1FILE=', 1))
        self.assertNotIn('d88', ''.join(diff))
        for value in (FRESH, 'os32_boot.d88', ' ', None):
            with self.subTest(value=value), self.assertRaises(ini.IniError):
                ini.transform(PATHS_RAW, {'FDD1FILE': value})

    def test_already_detached_is_a_noop_without_a_diff_line(self):
        out, diff = ini.transform(PATHS_RAW, {'FDD2FILE': ''})
        self.assertEqual((out, diff), (PATHS_RAW, []))

    def test_duplicate_or_missing_path_field_fails_closed(self):
        for raw in (PATHS_RAW.replace(b'FDD2FILE=\n', b''),
                    PATHS_RAW.replace(b'HDD1FILE=C:\\np21w\\work.nhd\r\n', b''),
                    PATHS_RAW.replace(b'HDD1FILE=', b'HDD1FILE=\nhdd1file=', 1),
                    PATHS_RAW.replace(b'FDD2FILE=\n', b'FDD2FILE=\nfdd2file=\n'),
                    PATHS_RAW.replace(b'FDD2FILE=\n', b'FDD2FILE= ; detached\n')):
            with self.subTest(raw=raw[:32]), self.assertRaises(ini.IniError):
                ini.transform(raw, EJECT)

    def test_tables_are_required_separately(self):
        """固定値キーだけの live 操作は新しい必須キーに依存しない (逆も同じ)。"""
        without_paths = (PATHS_RAW.replace(b'HDD1FILE=C:\\np21w\\work.nhd\r\n', b'')
                                  .replace(b'FDD1FILE=' + CP932_D88 + b'\r\n', b'')
                                  .replace(b'FDD2FILE=\n', b''))
        out, diff = ini.transform(without_paths, {'USEGD5430': 'true'})
        self.assertEqual(diff, ['USEGD5430: false -> true'])
        odd_fixed = PATHS_RAW.replace(b'ExMemory=16', b'ExMemory=20')
        out, diff = ini.transform(odd_fixed, EJECT)
        self.assertEqual(diff, ['FDD1FILE: set -> empty'])
        self.assertIn(b'ExMemory=20', out)
        with self.assertRaises(ini.IniError):
            ini.transform(odd_fixed, {'USEGD5430': 'true'})
        with self.assertRaises(ini.IniError):
            ini.transform(without_paths, EJECT)

    def test_both_tables_can_change_in_one_pass(self):
        out, diff = ini.transform(PATHS_RAW, dict(EJECT, HDD1FILE=FRESH, USEGD5430='true'))
        self.assertEqual(diff, ['USEGD5430: false -> true', 'HDD1FILE: set -> ' + FRESH,
                                'FDD1FILE: set -> empty'])
        self.assertIn(b' USEGD5430 = true \t; keep', out)

    def test_new_absolute_path_encoding_and_length_are_checked(self):
        for windows in ('C:\\\u65e5\u672c\u8a9e', 'np21w', '\\\\server\\share',
                        'C:\\' + 'a' * 240, 'C:\\', 'C:\\nul', 'C:\\dir\\sub.'):
            self.wslpath.output = windows
            with self.subTest(windows=windows), self.assertRaises(ini.IniError):
                ini.resolve_image('os32_fresh.nhd', '.nhd')

    def test_environment_must_name_np21w_dir(self):
        for value in (None, '', 'relative/dir', 'C:\\np21w'):
            with self.subTest(value=value):
                patch = (mock.patch.dict(os.environ, {}, clear=True) if value is None
                         else mock.patch.dict(os.environ, {'NP21W_DIR': value}))
                with patch, self.assertRaises(ini.IniError):
                    ini.resolve_image('os32_fresh.nhd', '.nhd')

    def test_wslpath_failure_is_content_free(self):
        for failure in (OSError('no wslpath'),
                        ini.subprocess.CalledProcessError(1, 'wslpath'),
                        ini.subprocess.TimeoutExpired('wslpath', 20)):
            with mock.patch.object(ini.subprocess, 'run', side_effect=failure):
                with self.subTest(failure=failure), self.assertRaises(ini.IniError) as caught:
                    ini.resolve_image('os32_fresh.nhd', '.nhd')
                self.assertNotIn(self.dir, str(caught.exception))

    def cli(self, value, *extra):
        source = Path(self.dir) / 'synthetic.snapshot'
        source.write_bytes(PATHS_RAW)
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = ini.main(['prepare', str(source), '--set', 'HDD1FILE=' + value,
                             '--set', 'FDD1FILE=', '--set', 'FDD2FILE='] + list(extra))
        self.assertNotIn('DO_NOT_PRINT', out.getvalue() + err.getvalue())
        self.assertEqual(source.read_bytes(), PATHS_RAW)
        return code, out.getvalue(), err.getvalue()

    def test_cli_resolves_a_name_through_resolve_image(self):
        code, out, _ = self.cli('os32_fresh.nhd')
        self.assertEqual(code, 0)
        self.assertEqual(out, 'HDD1FILE: set -> %s\nFDD1FILE: set -> empty\n' % FRESH)

    def test_cli_refuses_an_absolute_path_and_every_bypass_it_allowed(self):
        """絶対パス入力は resolve_image() を通らず、存在 / 通常ファイル /
        NP21W_DIR 内の検査を丸ごと迂回できた (往復 2 の blocker)。"""
        os.mkdir(os.path.join(self.dir, 'as_dir.nhd'))
        os.symlink(os.path.join(self.dir, 'os32_fresh.nhd'),
                   os.path.join(self.dir, 'as_link.nhd'))
        for value in (FRESH, WIN_DIR + '\\missing.nhd', WIN_DIR + '\\as_dir.nhd',
                      WIN_DIR + '\\as_link.nhd', 'C:\\Somewhere Else\\other.nhd',
                      'C:\\os32_fresh.nhd', 'as_dir.nhd', 'as_link.nhd', 'missing.nhd'):
            with self.subTest(value=value):
                code, out, err = self.cli(value)
                self.assertEqual(code, 2)
                self.assertEqual(out, '')
                self.assertTrue(err.startswith('error: '), err)
                self.assertNotIn(value, out)

    def test_cli_apply_cannot_store_an_unchecked_path(self):
        bundle = Path(self.dir) / 'bundles'
        bundle.mkdir()
        code, _, _ = self.cli(WIN_DIR + '\\missing.nhd', '--output', str(bundle), '--apply')
        self.assertEqual(code, 2)
        self.assertEqual(list(bundle.iterdir()), [])
        code, _, _ = self.cli('os32_fresh.nhd', '--output', str(bundle), '--apply')
        self.assertEqual(code, 0)
        prepared, = bundle.glob('np21w-offline-*/prepared.bin')
        self.assertIn(('HDD1FILE=' + FRESH).encode('ascii'), prepared.read_bytes())


if __name__ == '__main__':
    unittest.main()
