"""Fixture tests for P0-6 evidence classification."""
import subprocess
import sys
import tempfile
from pathlib import Path

import hal_inventory as inv


def check():
    source = {'include/fdc_contract.h': 'int fdc_status(void);',
              'drivers/block/unit.c': 'int fdc_status(void) { return 1; }',
              'fs/use.inc': '/* fdc_status(); */\nreturn fdc_status();',
              'gfx/opaque.c': 'const char *s = "struct GfxBackend {";',
              'include/input_service.h': 'struct InputOps { int (*get)(void); };',
              'platform/test.c': 'const InputOps io = { .get = fetch };',
              'hwdev/gf/unit.c': 'void unit(void) {}'}
    hal, calls, gf = inv.inventory({p: inv.mask(s) for p, s in source.items()})
    assert [(c[0], c[1], c[2], c[4]) for c in calls] == [
        ('fs/use.inc', 2, 'fdc_status', 'drivers/block/unit.c:1')]
    assert any(r[1] == 'gfx/opaque.c' and r[3] == r[6] == '未確定' for r in hal)
    assert any(r[3] == 'バックエンド' and r[6] == '移動のみ候補' for r in hal)
    assert any(r[3] == '契約' and r[2] == 1 for r in hal)
    mixed = dict(source, **{'kernel/fallback.c': 'int fdc_status(void) { return 0; }'})
    _, ambiguous, _ = inv.inventory({p: inv.mask(s) for p, s in mixed.items()})
    assert ambiguous[0][5] == '未確定'
    assert gf[0][:3] == ('hwdev/gf/unit.c', 'GPU エンジン・表示制御・サービス', '推測')


if __name__ == '__main__':
    check()
    if '--mutate' in sys.argv:
        tool = Path(inv.__file__).read_text()
        for old, new in [('return LITERALS.sub', 'return source or LITERALS.sub'),
                         ("and not any(d == domain for d, _ in found)", "and False"),
                         ("or 'gf' in path.lower()", "or False"),
                         ("if targets and all(p.split('/')[0] not in COMMON for p, _ in targets)",
                          "if targets")]:
            assert tool.count(old) == 1
            with tempfile.TemporaryDirectory() as tmp:
                dest = Path(tmp)
                (dest / 'hal_inventory.py').write_text(tool.replace(old, new))
                runner = dest / 'test_hal_inventory.py'
                runner.write_text(Path(__file__).read_text())
                proc = subprocess.run([sys.executable, '-B', runner], capture_output=True)
                assert proc.returncode and (b'AssertionError' in proc.stderr or
                                            b'IndexError' in proc.stderr), proc.stderr
                print('RED', old)
    print('PASS restructure-p0-6')
