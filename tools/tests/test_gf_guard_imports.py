"""M5 generated import slots, provider exports and private contract pins."""
CHECK = dict(name='gf-guard-imports-host', tier=1,
    covers=['tools/tests/test_gf_guard_imports.py', 'tools/tests/gf_guard_imports_host.c',
            'tools/prototypes/geforce_imports.def', 'tools/gen_sqlite_imports.py',
            'tools/gen_kdebug_imports.py', 'tools/gen_hwdev_contract.py',
            'hwdev/hwdev_imports.def', 'kernel/module/kdebug_imports.def',
            'docs/design/hwdev/hwdev_imports.md', 'hwdev/generated/*',
            'tools/tests/hwdev_contract_fixture.py', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py', 'drivers/video/geforce/geforce_module.h',
            'mm/paging.h', 'lib/sqlite3/os32_sqlite_vfs.c', 'sdk/kapi.json',
            'tools/hwdev_layout.json', 'tools/mkmod.py', 'tools/module_schema.py',
            'tools/prototypes/sqlite_imports.def', 'build/out/geforce/*', 'build/out/kernel.elf'],
    needs=['make:all'], budget=3.3)
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from kernel_includes import include_flags
from hwdev_contract_fixture import assert_hwdev_contract, assert_hwdev_inventory
import host32
ROOT = Path(__file__).resolve().parents[2]
NAMES = ('geforce_resource_lookup', 'paging_device_map', 'paging_device_unmap')


def main():
    assert_hwdev_contract(ROOT)
    assert_hwdev_inventory(ROOT)
    sys.path.insert(0, str(ROOT / 'tools'))
    from gen_kdebug_imports import load_def
    entries = load_def()
    symbols = [e['symbol'] for e in entries]
    # Later append-only imports (e.g. trident_diagnostic_ops) may follow; the
    # GeForce guard trio must stay consecutive and in this order.
    start = symbols.index(NAMES[0])
    assert symbols[start:start + len(NAMES)] == list(NAMES)
    out = ROOT / 'build/out/geforce'
    meta = json.loads((out / 'geforce_imports.json').read_text())['imports']
    declared = json.loads((ROOT / 'tools/prototypes/geforce_imports.def').read_text())['imports']
    assert [e['name'] for e in meta] == [e['symbol'] for e in declared]
    slots = {e['name']: e['index'] for e in meta}
    # The guard trio stays consecutive; later append-only imports (G3's policy
    # hooks) may follow it.
    names = [e['name'] for e in meta]
    first = names.index(NAMES[0])
    assert names[first:first + len(NAMES)] == list(NAMES)
    resident = subprocess.check_output(['nm', '--defined-only', str(ROOT / 'build/out/kernel.elf')], text=True)
    for name in NAMES:
        assert ' T ' + name + '\n' in resident
    asm = (out / 'geforce_import_stubs.S').read_text()
    with tempfile.TemporaryDirectory(prefix='gf-guard-imports-') as td:
        td = Path(td)
        exe, source = td / 'host', td / 'stubs.S'
        def run(text):
            source.write_text(text)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                '-I' + str(ROOT), '-I' + str(out), *include_flags(ROOT),
                *['-D' + label + '_SLOT=' + str(slots[name]) for label, name in zip(('LOOKUP', 'MAP', 'UNMAP'), NAMES)],
                str(ROOT / 'tools/tests/gf_guard_imports_host.c'), str(out / 'geforce_import_slots.c'),
                str(source), '-o', str(exe)], check=True)
            return host32.run([str(exe)], runner='qemu').returncode
        assert run(asm) == 0
        print('GREEN generated imports: lookup/map/unmap arguments, results, provider exports, private pins', flush=True)
        if '--mutate' in sys.argv:
            old = 'jmp *geforce_import_slots+' + str(4 * slots['paging_device_map']) + '\n'
            new = 'jmp *geforce_import_slots+' + str(4 * slots['paging_device_unmap']) + '\n'
            assert asm.count(old) == 1
            assert run(asm.replace(old, new)) == 1
            print('MUTATE runtime RED map jump routed to unmap', flush=True)
            assert run(asm) == 0
            print('GREEN restored import jump', flush=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])],
        cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
