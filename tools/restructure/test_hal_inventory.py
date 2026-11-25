"""Fixture tests for P0-6 evidence classification."""
import subprocess
import sys
import tempfile
from pathlib import Path

import hal_inventory as inv


def check():
    source = {'include/fdc_contract.h': 'int fdc_status(void);',
              'drivers/block/unit.c': 'int fdc_status(void) { return 1; }',
              'fs/use.inc': 'int use(void) { /* fdc_status(); */\nreturn fdc_status(); }',
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
    fixture = {
        'drivers/unit.c': 'void set_size(int n) {}',
        'fs/use.c': 'void update(void) { set_size(1); }',
        'fs/medium.c': 'const struct ext2_medium_ops ops = { .read = read };',
        'drivers/ata/test.c': 'int atapi_read_sectors(void) { return atapi_read10(); }',
        'drivers/video/geforce/test.c': 'int gf4_edid_modes(void) { return checksum(); }',
        'drivers/video/geforce/empty.c': 'int gf4_edid_modes(void) { return 0; }',
    }
    hal, calls, gf = inv.inventory(fixture)
    assert [(r[0], r[2], r[4]) for r in calls] == [('fs/use.c', 'set_size', 'drivers/unit.c:1')]
    assert any(r[0:4] == ('ブロック', 'fs/medium.c', 1, 'バックエンド') for r in hal)
    assert any(r[1] == 'drivers/ata/test.c' and 'atapi_read10' in r[5] for r in hal)
    assert [(r[3], r[4]) for r in gf] == [('未確定', '未確定'), ('EDID 表示モードの解析', '実コード')]
    _, nm_calls, _ = inv.inventory({'fs/use.c': 'void use(void) { entry(); }'},
                                   {'entry': ['platform/unit.o']})
    assert nm_calls[0][4] == 'platform/unit.o:nm'
    contract = {
        'include/clock_source.h': 'int platform_clock_read_us(void);',
        'platform/test_a/unit.c': 'int platform_clock_read_us(void) { return 0; }',
        'platform/test_b/unit.c': 'int platform_clock_read_us(void) { return 1; }',
        'include/geforce_contract.h': 'void geforce_foreground_poll(void);',
        'drivers/video/geforce/poll.c': 'void geforce_foreground_poll(void) {}',
        'kernel/poll.c': 'void poll(void) { platform_clock_read_us(); geforce_foreground_poll(); }',
    }
    assert [(r[2], r[5]) for r in inv.inventory(contract)[1]] == [('geforce_foreground_poll', 'P8候補')]
    del contract['include/clock_source.h']
    assert any(r[2] == 'platform_clock_read_us' for r in inv.inventory(contract)[1])
    fixture['include/alternate.h'] = '#define set_size(n) (0)'
    assert inv.inventory(fixture)[1][0][5] == '未確定'



if __name__ == '__main__':
    check()
    if '--mutate' in sys.argv:
        tool = Path(inv.__file__).read_text()
        for old, new in [('return LITERALS.sub', 'return source or LITERALS.sub'),
                         ("and not any(d == domain for d, _ in found)", "and False"),
                         ("or 'gf' in path.lower()", "or False"),
                         ("common_header and len(machines) >= 2", "common_header"),
                         ("common_header and len(machines) >= 2", "len(machines) >= 2"),
                         ("common_header and len(machines) >= 2", "False"),
                         ("if individual:", "if False:"),
                         ("if spec:", "if False:"),
                         ("if name not in device_names", "if name not in decls or name not in device_names"),
                         ("m = re.search(expr, code)", "m = None if path.startswith('fs/') else re.search(expr, code)"),
                         ("if name not in alternate and all(p.split('/')[0] not in COMMON for p, _ in targets)",
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
