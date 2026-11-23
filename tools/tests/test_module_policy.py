"""T5c-6 table validation and conformance of existing policy consumers."""
CHECK = dict(name='module-policy-host', tier=1,
    covers=['tools/prototypes/usb_imports.def', 'build/out/usb/usb_imports.json', 'tools/tests/host_arch/arch_io.h', 'drivers/video/geforce/geforce_module.c', 'hwdev/hwdev_module.c', 'tools/tests/module_pair_request_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'include/platform_module_pair.h', 'exec/os32x_hdr.c', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'kernel/module/module_schema.h', 'include/config.h', 'build/core.yaml', 'platform/pc98/kernel.c', 'platform/pc98/usb_load.c', 'kernel/sqlite_engine.c', 'userland/system/hsync.c', 'userland/shell/ui.c', 'kdebug/kdebug_module.c', 'kernel/ime/fep_module.c', 'drivers/ata/cd_module.c', 'drivers/video/kcg/kcg_module.c', 'drivers/sound/fm/fm_module.c', 'drivers/sound/cs4231/pcm_module.c', 'platform/pc98/legacy/v86_module.c', 'drivers/net/e82557/lan_module.c', 'drivers/video/cirrus/cirrus_module.c', 'drivers/video/trident/trident_module.c', 'build/kernel.*', 'build/out/*'],
    needs=['make:all'], budget=4.3,
    kind='characterization',
)

import json
import copy
import os
import struct
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
from pathlib import Path
import re
import sys

import yaml

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query

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
    extra.update(module_id=len(rows) + 1, name='EXTRA', path='/boot/extra.mod', dependencies=())
    assert rejected((*rows, extra), validator), 'excess ID accepted'


def check_pair_request(stem, adapter):
    # HWDEV needs a live kdebug dependency and uses its own real runtime fixture.
    if stem == 'hwdev':
        return
    entry = {'usb': 'pc98_usb_load()', 'sqlite': 'os32_sqlite_start()', 'kdebug': 'kdebug_load()',
             'fep': 'fep_module_load((struct module_handle){0})'}.get(stem, stem + '_module_load()')
    with tempfile.TemporaryDirectory(prefix='pair-request-') as tmp:
        exe = Path(tmp) / 'adapter'
        flags = ['gcc', '-m32', '-O2', '-fno-ipa-pure-const', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                 '-ffunction-sections', '-fdata-sections', '-D__KERNEL_BUILD__',
                 '-DPAIR_' + stem.upper(),
                 '-DPAIR_ENTRY=' + entry, '-Ikdebug/generated', '-Ibuild/out/' + stem, *include_flags(ROOT)]

        def exercise(path, expected):
            subprocess.run([*flags, '-DPAIR_ADAPTER="' + str(path) + '"',
                            '-iquote', str(adapter.parent), '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', 'tools/tests/module_pair_request_host.c',
                            '-o', exe], check=True, cwd=ROOT)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                for valid, rc in enumerate(expected):
                    result = host32.run([str(exe)], input=struct.pack('<I', valid), runner=runner)
                    assert result.returncode == rc, (stem, valid, result.returncode, rc)

        exercise(adapter, (0, 0))
        if '--mutate' in sys.argv:
            source = adapter.read_text()
            mutant = Path(tmp) / adapter.name
            old = 'platform_module_pair(&pair);'
            assert source.count(old) == 1
            for change, expected in [('pair_valid = 1;', (1, 0)), ('pair = 0;', (0, 1))]:
                mutant.write_text(source.replace(old, old + '\n    ' + change))
                exercise(mutant, expected)
                print(f'RED {stem} {change}')
            if stem == 'usb':
                anchor = 'kprintf(KLOG_ATTR, "[BOOT] usb module rc='
                assert source.count(anchor) == 1
                mutant.write_text(source.replace(anchor, 'if (0) ' + anchor))
                exercise(mutant, (1, 1))
                print('RED usb boot result omitted')
                anchor = 'else kprintf(KLOG_ATTR, "[BOOT] usb module skipped: DMA unavailable\\n");'
                assert source.count(anchor) == 1
                mutant.write_text(source.replace(anchor, ''))
                exercise(mutant, (3, 3))
                print('RED usb DMA skip omitted')


def conformance():
    assert gen_module_schema.OUTPUT.read_text() == gen_module_schema.render(), 'stale schema'
    config = (ROOT / 'include/config.h').read_text()
    deployed = yaml.safe_load((ROOT / 'build/core.yaml').read_text())['filesystem']['files']
    assert schema.MODULE_FORMATS['x86-pc98'] == (1, 3, 1, 0, 4)
    assert schema.MODULE_FORMATS['ppc-powermac'] == (2, 20, 2, 20, 12)
    # The installed body must publish every typed function in its build contract.
    usb_def = json.loads((ROOT / 'tools/prototypes/usb_imports.def').read_text())
    usb_meta = json.loads((ROOT / 'build/out/usb/usb_imports.json').read_text())
    assert [row['symbol'] for row in usb_def['imports']] == [
        row['name'] for row in usb_meta['imports']]
    positional = {row['name'] for row in schema.COMPONENTS if row['positional_imports']}
    assert positional == {'KDEBUG'}
    distributed = schema.COMPONENTS
    expected = [row['path'].split('/')[-1] for row in distributed]
    build = (ROOT / 'build/kernel.mk').read_text()
    paired = query(ROOT)['KERNEL_PAIRED_MODULES']
    assert [Path(name).name for name in paired] == expected, 'paired build list'
    actual = [row for row in deployed if row['guest'].endswith(('.mod', '/kdebug.bin'))]
    assert sorted((row['host'], row['guest'], row['tags']) for row in actual) == sorted(
        ('build/out/' + name, row['path'], ['programs'] if row['name'] == 'USB' else ['gui'] if row['name'] == 'TRIDENT' else ['core'])
        for name, row in zip(expected, distributed)), 'module distribution list'
    for row in schema.COMPONENTS:
        stem = row['path'].split('/')[-1].split('.')[0]
        adapter = {'sqlite': 'sqlite_engine', 'usb': 'usb_load'}.get(stem, stem + '_module')
        directory = {
            'usb_load': 'platform/pc98', 'kdebug_module': 'kdebug', 'hwdev_module': 'hwdev',
            'fep_module': 'kernel/ime', 'v86_module': 'platform/pc98/legacy',
            'cd_module': 'drivers/ata', 'lan_module': 'drivers/net/e82557',
            'pcm_module': 'drivers/sound/cs4231', 'fm_module': 'drivers/sound/fm',
            'cirrus_module': 'drivers/video/cirrus', 'trident_module': 'drivers/video/trident',
            'geforce_module': 'drivers/video/geforce', 'kcg_module': 'drivers/video/kcg',
        }.get(adapter, 'kernel')
        path = ROOT / directory / (adapter + '.c')
        check_pair_request(stem, path)
        source = path.read_text()
        path_macro = re.search(r'\.path\s*=\s*(SYS_\w+)', source)[1]
        assert re.search(r'^#define ' + path_macro + r'\s+"' + re.escape(row['path']) + '"', config, re.M), row
        assert 'MODULE_ID_' + row['name'] in source, row
        if row['name'] == 'HWDEV':
            assert row['dependencies'] == (schema.CONSTANTS['ID_KDEBUG'],)
            assert '.dependencies = &core_handle, .dependency_count = 1' in source
            assert '(void)hwdev_module_load(handle);' in (ROOT/'kdebug/kdebug_module.c').read_text()
        elif row['dependencies']:
            assert row['dependencies'] == (schema.CONSTANTS['ID_SQLITE'],)
            assert '.dependencies = sqlite, .dependency_count = sqlite ? 1 : 0' in source
            engine = (ROOT / 'kernel/sqlite_engine.c').read_text()
            assert engine.index('rc = os32_sqlite_start();') < engine.index('rc = fep_boot_start(&sqlite_handle, no_dictionary, &reserve);')
        else:
            assert '.dependencies =' not in source, row
    # hsync boot enumerates the whole /boot directory; it has no file allowlist.
    hsync = (ROOT / 'userland/system/hsync.c').read_text()
    assert 'sync_directory(src, dst, 0);' in hsync
    assert 'sync_directory(src_path, dst_path, depth + 1);' in hsync
    assert 'kdebug.bin / sqlite.mod' not in hsync
    # Core splash returns into unconditional boot continuation. SerialFS's FS
    # registration is earlier; actual serial/rshell service starts in shell ui.
    kernel = (ROOT / 'platform/pc98/kernel.c').read_text()
    assert kernel.count('    boot_splash();') == 1
    assert kernel.index('    boot_splash();') < kernel.index('shell_loop_run(&ops);')
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
            assert str(exc) == 'excess ID accepted', exc
            print('MUTATE capacity-check-removed RED (compiled Python, runtime excess ID)')
        else:
            raise AssertionError('capacity mutant survived')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_module_policy.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
