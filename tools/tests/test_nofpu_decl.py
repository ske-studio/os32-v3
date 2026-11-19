"""Wave A declaration ABI, executed as an ILP32 ELF without libc."""
CHECK = dict(name='nofpu-decl', tier=1, needs=[], budget=3,
    covers=['arch/x86/arch_exec_storage.h',
 'arch/x86/kernel/x87.h',
 'arch/x86/kernel/x87_emu.h',
 'build/config.mk',
 'docs/decisions/NOFPU_DESIGN.md',
 'include/callconv.h',
 'include/exec_arch.h',
 'include/types.h',
 'kernel/sqlite_engine.h', 'kernel/sqlite_engine_body.h',
 'kernel/ver_info.h',
 'lib/softfloat_bits.h',
 'sdk/include/os32/os32_generations.h',
 'sdk/include/os32/os32_gfx2.h',
 'sdk/include/os32/os32_host_notice.h',
 'sdk/include/os32/os32_kapi_generated.h',
 'sdk/include/os32/os32_kapi_shared.h',
 'sdk/include/os32/os32_ls.h',
 'sdk/include/os32/os32_surface.h',
 'sdk/include/os32/os32_ver_info.h',
 'tools/tests/cc_cache.py',
 'tools/tests/host32.py',
 'tools/tests/kernel_includes.py',
 'tools/tests/nofpu_decl_host.c',
 'tools/tests/test_nofpu_decl.py'],
    kind='contract')

from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'arch/x86/kernel/x87_emu.h'
MUTATIONS = (
    ('operand-buffer-short', '#define X87_EMU_MEMORY_BYTES 108',
     '#define X87_EMU_MEMORY_BYTES 107', 1),
    ('user-esp-index', 'X87_ESP, X87_EBP, X87_ESI, X87_EDI',
     'X87_EBP, X87_ESP, X87_ESI, X87_EDI', 6),
)


def execute(header):
    with tempfile.TemporaryDirectory(prefix='nofpu-decl-') as directory:
        tmp = Path(directory)
        (tmp / 'x87_emu.h').write_text(header)
        exe = tmp / 'fixture'
        args = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Os',
                '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                '-I'+str(tmp), '-I'+str(ROOT / 'arch/x86/kernel'),
                *include_flags(ROOT), str(ROOT / 'tools/tests/nofpu_decl_host.c'),
                '-o', str(exe)]
        subprocess.run(args, check=True)
        return host32.run([str(exe)], timeout=30).returncode


def main():
    header = HEADER.read_text()
    assert execute(header) == 0
    print('NOFPU declarations: GREEN (libc-free ILP32)')
    if '--mutate' in sys.argv:
        for name, before, after, expected in MUTATIONS:
            assert header.count(before) == 1, name
            rc = execute(header.replace(before, after))
            assert rc == expected, (name, rc, expected)
            print(f'{name}: runtime RED rc={rc}')


def run(ctx):
    import os
    args = ['python3', '-B', 'tools/tests/test_nofpu_decl.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
