"""HWOP_COMMON_DESIGN §2.1. Real packed import order and PPC loader validation."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

CHECK = dict(name='ppc-diag-import-host', tier=2, needs=[], budget=60,
    env=['ppc-cross'], env_limit='Builds the PPC diagnostic ELF with its target compiler.',
    covers=['tools/checks/ppc-diag-import-host.py', 'tools/tests/ppc_diag_import_host.c',
            'tools/tests/host32.py', 'tools/mkmod.py', 'tools/ppc/module_rela.py',
            'tools/module_schema.py', 'Makefile', 'build/**', 'platform/powermac/**',
            'arch/ppc/**', 'kernel/module/**', 'lib/**', 'include/**', 'sdk/**',
            'exec/exec_hwb.h', 'kdebug/**', 'drivers/pci/**'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §2.1'])


def run(ctx):
    import host32
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file():
        print('SKIP: PPC toolchain unavailable')
        return 0
    with tempfile.TemporaryDirectory(prefix='diag-import-') as temp:
        out = Path(temp)
        meta = Path('platform/powermac/kdebug_imports.json')
        for bad in ([False, True] if ctx.mutate else [False]):
            if bad:
                data = json.loads(meta.read_text())
                data['imports'][7]['name'] += '_bad'
                meta = out / 'bad.json'
                meta.write_text(json.dumps(data))
            subprocess.run(['make', 'ARCH=ppc', 'PLATFORM=powermac', 'IMAGE=kboot-link-sh',
                            'KB_OUT='+temp, 'KB_DIAG_METADATA='+str(meta), 'kdebug'], check=True)
            asm = out / 'image.S'
            asm.write_text('.section .rodata\n.global packed,packed_end\npacked:\n.incbin "' +
                           str(out/'kdebug.bin') + '"\npacked_end:\n.section .note.GNU-stack,"",@progbits\n')
            exe = out / 'test'
            subprocess.run(['gcc', '-std=gnu11', '-O2', '-m32', '-nostdinc', '-nostdlib',
                            '-ffreestanding', '-fno-pie', '-no-pie', '-static', '-D__cdecl=',
                            '-D__powerpc__', '-Iplatform/powermac', '-Ikernel/module', '-Iinclude',
                            '-Isdk/include/os32', '-Ilib', '-I.', 'tools/tests/ppc_diag_import_host.c',
                            'kernel/module/module_image.c', str(asm), '-o', str(exe)], check=True)
            rc = host32.run([str(exe)]).returncode
            if rc != (12 if bad else 0):
                raise AssertionError(('slot7-hash' if bad else 'normal', rc))
            print('RED slot7-hash: MODULE_IMPORT_INVALID' if bad else 'GREEN eight typed imports')
    return 0
