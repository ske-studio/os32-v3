"""Exercise the scope ledger contract using an independent miniature Git tree."""
import argparse
import contextlib
import csv
import io
from pathlib import Path
import subprocess
import tempfile
from types import ModuleType

import p1_verify as verify


# The miniature Git tree supplies the same historical blob as the real ledger.
CLASSIFIER_PATH = verify.LAYER_BLOB.split(':', 1)[1]


def fixture(root):
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    for path in ('tools/gfinit_rom_check.py', 'tools/fixture_source.py', 'code.c', 'tools/checks/fixture_runner.py', 'tools/restructure/inventory.py',
                 'tools/checks/restructure_p0_987.py', 'include/pc98.h', 'platform/pc98/platform_console.h', 'build/arch/example.mk'):
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.touch()
    classifier = root / CLASSIFIER_PATH
    classifier.write_text("NAMED = {'pc98.h': ('platform', 'pc98')}\nDEVICE_ROOTS = ('drivers',)\n")
    (root / 'tools/fixture_source.py').write_text("print('GREEN')\n")
    (root / 'tools/checks/fixture_runner.py').write_text("# covers tools/fixture_source.py\n")
    (root / 'build/arch/example.mk').write_text('C_ARCH += code.c\n')
    subprocess.run(['git', 'add', '.'], cwd=root, check=True)
    return subprocess.check_output(['git', 'write-tree'], cwd=root, text=True).strip()


def exercise(module, root, tree):
    module.ROOT = root
    table = root / 'scope.tsv'
    product = ['code.c', 'P1-1', '製品', '仮 D-3。所属判断=共通実装：像の選択で共通処理を使う', '', '共通実装']
    runner = ['tools/checks/fixture_runner.py', 'P1-4', '実行の基盤', '登録を実行', '', '開発・検査用実装']
    rom = ['tools/gfinit_rom_check.py', 'P1-4', '検査の本体', '期待 CRC と照合', '', '開発・検査用実装']
    source = ['tools/fixture_source.py', 'P1-1', '運用の道具', '候補印=合否語、登録・check 目標・workflow の参照。入力を生成する', '', '開発・検査用実装']
    classifier = [CLASSIFIER_PATH, 'P1-4', '検査の本体', '層の分類', '', '開発・検査用実装']
    header = ['include/pc98.h', 'P1-1', '製品', '名前つきヘッダ', '', '機種固有実装']
    fixed = ['platform/pc98/platform_console.h', 'P1-1', '製品', '固定名の契約', '', '機種固有実装']
    manifest = ['build/arch/example.mk', 'P1-1', 'ビルドの定義', '像の定義', '', 'ビルドの定義']
    footer = [verify.EXCLUDED, ['tools/checks/restructure_p0_987.py', '開発・検査用実装'],
              ['tools/restructure/inventory.py', '開発・検査用実装']]

    for snippet, marker in [("print('FAIL')", '合否語'), ("print('RED')", '合否語'),
                            ('return 3', '異常終了'), ('value != expected', '期待値・試験入力')]:
        assert module.candidate_marks(snippet, 'probe.py', set()) == marker
    assert not module.candidate_marks('print(value)', 'probe.py', {'other_probe.py'})

    def check(rows, expected_code, messages, rom_row=rom):
        with table.open('w', newline='', encoding='utf-8') as stream:
            csv.writer(stream, delimiter='\t').writerows([verify.HEADER, classifier, header, fixed, manifest, rom_row, source, *rows])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = module.verify_scope(table, tree)
        assert code == expected_code, output.getvalue()
        for message in messages:
            assert message in output.getvalue(), message

    check([product, runner, *footer], 0, ['A1\tcode.c', '差 0'])
    header[-1] = '共通実装'
    check([product, runner, *footer], 1, ['include/pc98.h: 所属照合 機種固有実装 → 共通実装 の理由がない'])
    header[-1] = '機種固有実装'
    for row in (header, fixed):
        original = row.copy()
        row[3] += '。所属判断=共通実装：共通の処理'
        row[-1] = '共通実装'
        check([product, runner, *footer], 1, [row[0] + ': ヘッダの所属は 機種固有実装 に固定'])
        row[:] = original
    check([[*product[:3], '像の入力', *product[4:]], runner, *footer], 1, ['code.c: 所属照合'])
    for invalid in ('', '範囲外'):
        check([[*product[:-1], invalid], runner, *footer], 1, ['code.c: 所属種類が空または不正'])
    check([product, runner, *footer[:-1], [footer[-1][0], '']], 1, ['母数外の所属種類が不正'])
    reviewed = source[3]
    source[3] = '用途未記録'
    check([product, runner, *footer], 1, ['fixture_source.py: 候補印=合否語、登録・check 目標・workflow の参照'])
    source[3] = reviewed
    check([product, runner, *footer], 1, ['gfinit_rom_check.py: 検査の本体として分類する'],
          [rom[0], 'P1-1', '運用の道具', *rom[3:]])
    check([['other.c', *product[1:]], runner, *footer], 1, ['不足: code.c', '余分: other.c'])
    check([product, product, runner, *footer], 1, ['重複: code.c'])
    check([product, [runner[0], '', *runner[2:]], *footer], 1, ['必須欄が空'])
    check([product, [runner[0], '文書', '文書', *runner[3:]], *footer], 1, ['tools の種類が不正'])
    check([product, runner, *footer[:-1]], 1, ['母数外 不足: tools/restructure/inventory.py'])


def hal_fixture(root):
    # Use a separate index so the scope fixture still describes its own tree.
    root.mkdir()
    subprocess.run(['git', 'init', '-q'], cwd=root, check=True)
    files = {'gfx/opaque.c': 'int marker;',
             'hwdev/gf/unit.c': 'int gf_unit(void) { return 1; }',
             'fs/use.inc': 'int use(void) { return gf_unit(); }'}
    for path, source in files.items():
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    subprocess.run(['git', 'add', '.'], cwd=root, check=True)
    for name, path in zip(('hal', 'geforce', 'p8'), files):
        (root / f'{name}.tsv').write_text('file\t根拠\n' + path + '\tfixture\n')
    return subprocess.check_output(['git', 'write-tree'], cwd=root, text=True).strip()


def exercise_hal(module, root, tree):
    module.ROOT = root
    expected = {'gfx/opaque.c', 'hwdev/gf/unit.c', 'fs/use.inc'}
    assert module.hal_targets(tree) == expected
    assert module.hal_table_targets(root) == expected
    table = root / 'hal_map.tsv'
    header = ['パス', '役割', '移設', '行き先の候補', '根拠']
    row = ['gfx.c', '振り分け', '移すだけ', 'sys/hal/gfx/', 'gfx.c の flip が委譲する']
    pending = ['gpu.c', 'バックエンド', '未確定', 'sys/dev/', 'gpu.c の probe を A1 で確認']

    sources = {'gfx.c': '/* cpuid */ void flip(void) {}'}
    def check(rows, status, *messages):
        with table.open('w', newline='', encoding='utf-8') as stream:
            csv.writer(stream, delimiter='\t').writerows([header, *rows])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = module.verify_hal(table, {'gfx.c', 'gpu.c'}, sources)
        assert result == status, output.getvalue()
        for message in messages:
            assert message in output.getvalue(), message

    check([row, pending], 0, 'A1 HAL\tgpu.c', '未確定 1')
    check([row, row], 1, '不足: gpu.c', '重複: gfx.c')
    check([row, ['other.c', *pending[1:]]], 1, '不足: gpu.c', '余分: other.c')
    check([row, [pending[0], pending[1], 'P8', 'sys/hal/', '根拠']], 1, '装置固有')
    check([row, ['gpu.c', '契約', '移すだけ', row[3], '入口を宣言']], 1, '衝突')
    check([row[:-1] + [' '], pending], 1, 'HAL 必須欄が空')
    check([row[:2] + ['候補'] + row[3:], pending], 1, '移設判断が不正')

    for code in ('int flags = PTE_PCD;', 'kbd_trygetkey();', 'id == NET_NIC_E82557',
                 'extern struct ops ext2_medium_loop_ops;', '__asm__("nop");'):
        sources['gfx.c'] = code
        check([row, pending], 1, '移すだけの境界根拠がない')
        check([row[:2] + ['P8'] + row[3:], pending], 0, 'P8 1')
    sources['gfx.c'] = 'pte_write();'
    check([row[:-1] + ['境界照合 pte_write。GPU の GART 表を更新する。'], pending], 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = Path(verify.__file__).read_text()
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        tree = fixture(root)
        hal_root = root / 'hal-fixture'
        hal_tree = hal_fixture(hal_root)
        exercise(verify, root, tree)
        exercise_hal(verify, hal_root, hal_tree)
        if args.mutate:
            for anchor, replacement in (
                ('{r[1] for r in hal}', 'set()'),
                ('{r[0] for r in calls}', 'set()'),
                ('{r[0] for r in geforce}', 'set()'),
                ("('hal.tsv', 'geforce.tsv', 'p8.tsv')", "('hal.tsv', 'geforce.tsv')"),
                ("if symbol and f'境界照合 {symbol}。' not in reason:", 'if False:'),
                ('if dest in destinations:', 'if False:'),
                ("compare('HAL', paths, expected, issues)", 'pass'),
                ('if any(not cell.strip() for cell in row):', 'if False:'),
                ('for path, reason in sorted(pending):', 'for path, reason in []:'),
                ("elif 'sys/hal/' in dest and role not in HAL_ROLES:", 'elif False:'),
                ('if move not in (', 'if False and move not in ('),
                ('if header_kind and ownership != header_kind:', 'if False:'),
                ('if evidence - {ownership}:', 'if False:'),
                ('evidence.update(members.get(path, ()))', 'pass'),
                ('if ownership not in OWNERSHIP_KINDS:', 'if False:'),
                ("if row[1] != '開発・検査用実装':", 'if False:'),
                ('expected - counts.keys()', 'set()'),
                ('counts.keys() - expected', 'set()'),
                ("if path.removeprefix('tools/') in CHECK_BODIES and path.startswith('tools/') and kind != '検査の本体':", 'if False:'),
                ('if Path(path).name in callers:', 'if False:'),
                ('if re.search(pattern, source)', 'if False'),
                ("if marks and REVIEW_MARK + marks + '。' not in reason:", 'if False:'),
                ('if count > 1:', 'if False:'),
                ('if any(not value.strip() for value in row[:4]):', 'if False:'),
                ("compare('母数外', exclusions, omitted, issues)", 'pass'),
                ('for path, reason in pending:', 'for path, reason in []:'),
            ):
                assert source.count(anchor) == 1, anchor
                mutant = ModuleType('mutant')
                mutant.__file__ = verify.__file__
                exec(compile(source.replace(anchor, replacement), verify.__file__, 'exec'),
                     mutant.__dict__)
                try:
                    exercise(mutant, root, tree)
                    exercise_hal(mutant, hal_root, hal_tree)
                except AssertionError:
                    print('RED', anchor)
                else:
                    raise AssertionError('surviving mutation: ' + anchor)
    print('OK: P1 scope and HAL ledger contracts')


if __name__ == '__main__':
    main()
