#!/usr/bin/env python3
"""C2: real FEP generator contracts and unsupported display provider."""
CHECK = dict(name='ime-display-boundary-host', tier=1, needs=['make:all'], budget=2,
    covers=['init/kernel.c', 'include/ime_boot.h', 'kernel/ime/ime.h', 'tools/tests/kernel_includes.py', 'tools/tests/test_ime_display_boundary.py', 'kernel/ime/ime_display_private.h',
            'include/ime_control.h', 'platform/*/ime_default_render.h',
            'platform/powermac/ime_display.c', 'kernel/ime/ime_render.h',
            'tools/prototypes/fep_imports.def', 'tools/gen_sqlite_imports.py',
            'tools/gen_kdebug_imports.py', 'tools/tests/host32.py',
            'drivers/char/serial.h',
            'drivers/input/kbd.h',
            'exec/exec.h',
            'fs/fd_redirect.h',
            'fs/vfs.h',
            'include/con_sink.h',
            'include/kprintf.h',
            'include/redir_access.h',
            'include/tvram.h',
            'include/types.h',
            'kernel/ime/fep_module.h',
            'kernel/ime/ime_store.h',
            'kernel/sqlite_engine.h',
            'lib/kstring.h',
            'lib/sqlite3/os32_sqlite_outer.h',
            'lib/utf8.h',
            'mm/kmalloc.h',
            'sdk/include/os32/os32_kapi_shared.h',
            'tools/module_schema.py', 'tools/mkmod.py', 'tools/prototypes/sqlite_imports.def', 'kernel/module/kdebug_imports.def',
            'lib/sqlite3/os32_sqlite_vfs.c', 'sdk/kapi.json'])
import argparse
import json
import os
import subprocess
from kernel_includes import include_flags
from pathlib import Path
import sys
import tempfile
import host32
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gen_sqlite_imports as generator
ROOT = Path(__file__).resolve().parents[2]


def verify(directory):
    header = (directory / 'fep_imports.h').read_text()
    assert '#define FEP_MODULE_ABI_GEN 0x9277c6e2u' in header, 'FEP contract fingerprint'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ime-boundary-') as temporary:
        work = Path(temporary)
        tree = work / 'tree'
        definition = ROOT / 'tools/prototypes/fep_imports.def'
        doc = json.loads(definition.read_text())
        for name in set(doc['contracts']) | {row['source'] for row in doc['imports'] if 'source' in row}:
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / name).read_bytes())
        generator.ROOT = tree
        output = work / 'generated'
        generator.generate(output, definition, module='fep')
        verify(output)
        print('GREEN FEP contract fingerprint', flush=True)
        if args.mutate:
            contract = tree / 'kernel/ime/ime_render.h'
            text = contract.read_text()
            assert text.count('IME_RENDER') > 0
            assert text.count('IME_RENDER.H') == 1
            contract.write_text(text.replace('IME_RENDER.H', 'IME_RENDER.X'))
            generator.generate(output, definition, module='fep')
            try:
                verify(output)
            except AssertionError as error:
                assert str(error) == 'FEP contract fingerprint'
                print('RED runtime ime_render.h one-character comment: real generated fingerprint', flush=True)
            else:
                raise AssertionError('contract mutation survived')
        boot = (ROOT / 'init/kernel.c').read_text()
        anchor = '#include "ime_boot.h"'
        assert boot.count(anchor) == 1
        boot_source = work / 'kernel.c'
        def boot_dependencies(text):
            boot_source.write_text(text)
            cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
            return subprocess.check_output([cc, '-M', '-I' + str(ROOT / 'init'),
                                            *include_flags(ROOT), str(boot_source)], text=True)
        def check_boot(text):
            deps = boot_dependencies(text)
            assert 'kernel/ime/ime.h' not in deps, 'boot depends on ime.h'
        check_boot(boot)
        if args.mutate:
            try:
                check_boot(boot.replace(anchor, '#include "../kernel/ime/ime.h"'))
            except AssertionError as error:
                assert str(error) == 'boot depends on ime.h'
                print('RED boot ime.h dependency', flush=True)
            else:
                raise AssertionError('boot dependency mutation survived')
        source = work / 'provider.c'
        source.write_text('''#include "include/ime_control.h"
#include <ime_default_render.h>
#include "platform/powermac/ime_display.c"
void _start(void) {
    int bad = IME_DEFAULT_RENDER != 0 || ime_display_boot(0) != -1 ||
        ime_display_bytes() != 0 || ime_display_save(0, 3) || ime_display_restore(0, 3);
    ime_display_abort();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(bad));
    for (;;) { }
}
''')
        exe = work / 'provider'
        host32.build(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-Wall', '-Wextra', '-Werror',
            '-I' + str(ROOT), '-I' + str(ROOT / 'include'),
            '-I' + str(ROOT / 'platform/powermac'), str(source), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], capture_output=True)
        assert result.returncode == 0, result
        print('GREEN unsupported provider and narrow facade', flush=True)
    return 0


def run(ctx):
    import os
    import subprocess
    for runner in ctx.runners:
        argv = [sys.executable, '-B', 'tools/tests/test_ime_display_boundary.py']
        if ctx.mutate:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
