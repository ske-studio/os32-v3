"""T5c-6 table validation and conformance of existing policy consumers."""
import copy
from pathlib import Path
import re
import sys

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import module_schema as schema
import gen_module_schema


def rejected(rows, validator=schema.validate_components):
    try:
        validator(rows)
    except ValueError:
        return True
    return False


def fixtures(validator=schema.validate_components):
    rows = copy.deepcopy(schema.COMPONENTS)
    assert rows[10]["name"] == "HWDEV" and rows[10]["module_id"] == 11
    validator(rows)
    assert rejected(rows[:-1], validator), 'missing component accepted'
    for change in ('duplicate', 'unknown', 'cycle', 'missing-dependency', 'duplicate-dependency'):
        bad = copy.deepcopy(rows)
        if change == 'duplicate':
            bad[-1]['module_id'] = 1
        if change == 'unknown':
            bad[-1]['module_id'] = 99
        if change == 'cycle':
            bad[1]['dependencies'] = (3,)
        if change == 'missing-dependency':
            bad[2]['dependencies'] = (99,)
        if change == 'duplicate-dependency':
            bad[2]['dependencies'] = (2, 2)
        assert rejected(bad, validator), change
    extra = copy.deepcopy(rows[0])
    extra.update(module_id=12, name='EXTRA', path='/boot/extra.mod', dependencies=())
    assert rejected((*rows, extra), validator), 'twelfth ID accepted'


def conformance():
    assert gen_module_schema.OUTPUT.read_text() == gen_module_schema.render(), 'stale schema'
    config = (ROOT / 'include/config.h').read_text()
    deployed = yaml.safe_load((ROOT / 'build/core.yaml').read_text())['filesystem']['files']
    # M2 admits synthetic HWDEV; production image/deployment belongs to M3a/M5.
    distributed = schema.COMPONENTS[:10]
    expected = [row['path'].split('/')[-1] for row in distributed]
    build = (ROOT / 'build/kernel.mk').read_text()
    paired = re.search(r'^KERNEL_PAIRED_MODULES := (.*)$', build, re.M)[1]
    assert paired.split() == ['$(BUILD_OUT)/' + name for name in expected], 'paired build list'
    actual = [row for row in deployed if row['guest'].endswith(('.mod', '/kdebug.bin'))]
    assert [(row['host'], row['guest'], row['tags']) for row in actual] == [
        ('build/out/' + name, row['path'], ['core'])
        for name, row in zip(expected, distributed)], 'core distribution list'
    for row in schema.COMPONENTS:
        stem = row['path'].split('/')[-1].split('.')[0]
        adapter = {'kdebug': 'module', 'sqlite': 'sqlite_engine'}.get(stem, stem + '_module')
        source = (ROOT / 'kernel' / (adapter + '.c')).read_text()
        path_macro = re.search(r'\.path = (SYS_\w+)', source)[1]
        assert re.search(r'^#define ' + path_macro + r'\s+"' + re.escape(row['path']) + '"', config, re.M), row
        assert 'MODULE_ID_' + row['name'] in source, row
        if row['name'] == 'HWDEV':
            assert row['dependencies'] == (schema.CONSTANTS['ID_KDEBUG'],)
            assert '.dependencies = &core_handle, .dependency_count = 1' in source
            assert '(void)hwdev_module_load(handle);' in (ROOT/'kernel/module.c').read_text()
        elif row['dependencies']:
            assert row['dependencies'] == (schema.CONSTANTS['ID_SQLITE'],)
            assert '.dependencies = &sqlite, .dependency_count = 1' in source
            engine = (ROOT / 'kernel/sqlite_engine.c').read_text()
            assert engine.index('rc = os32_sqlite_start();') < engine.index('rc = fep_module_load(sqlite_handle);')
        else:
            assert '.dependencies =' not in source, row
    # hsync boot enumerates the whole /boot directory; it has no file allowlist.
    hsync = (ROOT / 'userland/system/hsync.c').read_text()
    assert 'sync_directory(src, dst, 0);' in hsync
    assert 'sync_directory(src_path, dst_path, depth + 1);' in hsync
    assert 'kdebug.bin / sqlite.mod' not in hsync
    # Core splash returns into unconditional boot continuation. SerialFS's FS
    # registration is earlier; actual serial/rshell service starts in shell ui.
    kernel = (ROOT / 'kernel/kernel.c').read_text()
    assert kernel.count('    boot_splash();') == 1
    assert kernel.index('    boot_splash();') < kernel.index('rc = exec_run(kcg_unavailable')
    ui = (ROOT / 'userland/shell/ui.c').read_text()
    assert ui.index('g_api->serial_init(SYS_SERIAL_BAUD);') < ui.index('execute_command("rshell")') < ui.index('        show_prompt();')
    print('PASS module policy consumers and splash -> shell serial/rshell -> prompt')


def main():
    fixtures()
    conformance()
    if '--mutate' in sys.argv:
        source = (ROOT / 'tools/module_schema.py').read_text()
        old = "    require(len(rows) <= MODULE_RECORD_CAP, 'module record capacity exceeded')"
        assert source.count(old) == 1
        namespace = {}
        exec(compile(source.replace(old, ''), 'module_schema_mutant.py', 'exec'), namespace)
        try:
            fixtures(namespace['validate_components'])
        except AssertionError as exc:
            assert str(exc) == 'twelfth ID accepted', exc
            print('MUTATE capacity-check-removed RED (compiled Python, runtime twelfth ID)')
        else:
            raise AssertionError('capacity mutant survived')


if __name__ == '__main__':
    main()
