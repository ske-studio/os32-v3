"""ESC0..7 libc-free x87 oracle and transactional memory regressions."""
CHECK = dict(name='nofpu-emu', tier=1, needs=[], budget=40,
    covers=['arch/x86/arch_exec_storage.h',
 'arch/x86/arch_io.h',
 'arch/x86/arch_jmp_storage.h',
 'arch/x86/kernel/gdt.h',
 'arch/x86/kernel/x87.h',
 'arch/x86/kernel/x87_emu.h',
 'arch/x86/kernel/x87_emu_decode.c',
 'arch/x86/kernel/x87_emu_dispatch.c',
 'arch/x86/kernel/x87_emu_esc03.c',
 'arch/x86/kernel/x87_emu_esc47.c',
 'arch/x86/kernel/x87_emu_exception.c',
 'arch/x86/kernel/x87_emu_internal.h',
 'arch/x86/kernel/x87_emu_memory.c',
 'arch/x86/kernel/x87_emu_ops.c',
 'arch/x86/kernel/x87_emu_state.c',
 'arch/x86/x86_flags.h',
 'build/config.mk',
 'fs/vfs_stdin.h',
 'exec/appslot.h',
 'fs/fd_redirect.h',
 'include/app_ids.h',
 'include/appmem_types.h',
 'include/callconv.h',
 'include/exec_arch.h',
 'include/io.h',
 'include/ksetjmp.h',
 'include/memmap.h',
 'include/pc98.h',
 'include/redir_access.h',
 'include/types.h',
 'lib/divdi3.c',
 'lib/softfloat/**',
 'mm/paging.h',
 'mm/physmem.h',
 'platform/pc98/platform_io.h',
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
 'tools/tests/test_nofpu_emu.py',
 'tools/tests/x87_emu_core_host.c'], kind='contract')
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['decode', 'state', 'ops', 'esc03', 'esc47', 'exception', 'memory', 'dispatch']
MUTATIONS = [
    ('arithmetic-sub-for-add', 'ops', 'case 0: return extF80_add(a, b);',
     'case 0: return extF80_sub(a, b);'),
    ('rounding-map-reversed', 'exception', 'softfloat_round_max, softfloat_round_minMag',
     'softfloat_round_minMag, softfloat_round_max'),
    ('pop-reversed', 'state', 'x87_top(&e->state) + 1', 'x87_top(&e->state) - 1'),
    ('second-page-unchecked', 'memory', 'if (!as_access_page(a->as, va, write, &pa[count]))',
     'if (count == 1) return X87_EMU_OK;\n        if (!as_access_page(a->as, va, write, &pa[count]))'),
    ('fnstenv-mask-removed', 'esc03', 'e->state.cw |= 0x3f;', 'e->state.cw |= 0;'),
    ('sti-reverse-order', 'esc47', 'idx >= 4 ? (idx ^ 1) : idx',
     'idx'),
    ('save-clears-register-payload', 'esc47', 'x87_emu_state_init(&e->state, false);',
     'x87_emu_state_init(&e->state, true);'),
    ('save-environment-only', 'esc47',
     'x87_emu_state_encode(&e->state, e->output, c->d->operand16, false);',
     'x87_emu_state_encode(&e->state, e->output, c->d->operand16, true);'),
    ('fnstsw-clobbers-eax', 'esc47', '(e->gpr[X87_EAX] & 0xffff0000u)', '0'),

]

def main():
    with tempfile.TemporaryDirectory(prefix='nofpu-emu-') as directory:
        tmp = Path(directory)
        oracle = tmp/'oracle'
        subprocess.run(['gcc','-m64','-mno-sse','-mno-sse2','-DX87_ORACLE_BUILD','-Os','-ffreestanding',
                        '-fno-stack-protector','-nostdlib','-static','-no-pie',
                        str(ROOT/'tools/tests/x87_emu_core_host.c'),'-o',str(oracle)],check=True)
        # ELF64 with direct syscall, no i386 int80 runner or binfmt fallback.
        capture=subprocess.run([str(oracle)],check=True,capture_output=True).stdout
        sections=[('arith',768*108),('convert',20*6),('unary',32*108),('extended',14*108),('store',6*112),('env',4*108),('rear_reg',27*8*4*3*112),('rear_edge',18*27*112),('rear_mem',16*108),('rear_store',4*4*12*7*116),('rear_load',24*108),('rear_image',16*324)]
        assert len(capture)==sum(size for _,size in sections),len(capture)
        offset=0; header=[]
        for name,size in sections:
            data=capture[offset:offset+size];offset+=size
            header.append('static const unsigned char reference_'+name+'[]={' +
                          ','.join(str(b) for b in data)+'};\n')
        (tmp/'x87_emu_reference.h').write_text(''.join(header))
        print('Oracle: native ELF64 x87, 5322 cases, libc=none',flush=True)
        flags = ['-I'+str(tmp), '-m32', '-march=i386', '-std=gnu11', '-Os', '-ffreestanding',
                 '-fno-pie', '-fno-stack-protector', '-msoft-float', '-mno-fp-ret-in-387',
                 '-DX87_EMU_HOST_TEST', '-I'+str(ROOT/'arch/x86/kernel'), *include_flags(ROOT)]
        sf = ROOT/'lib/softfloat'
        sources = [sf/name for name in json.loads((sf/'UPSTREAM.json').read_text())['selected_sources']]
        sources += [ROOT/'lib/divdi3.c', ROOT/'tools/tests/x87_emu_core_host.c']
        objects = []
        for i, source in enumerate(sources):
            obj=tmp/f'base{i}.o'
            subprocess.run(['gcc', *flags, '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        # Compile the actual privileged entry too; the executable only hooks
        # IRQ/CR0/owner eligibility, which cannot execute in a user process.
        cross=Path(os.environ.get('CROSS_DIR','/home/hight/opt/cross'))/'bin/i386-elf-gcc'
        production_flags=[f for f in flags if f != '-DX87_EMU_HOST_TEST']
        for name in SOURCES:
            obj=tmp/f'production_{name}.o'
            subprocess.run([str(cross),*production_flags,'-Wall','-Wextra','-Werror',
                '-c',str(ROOT/f'arch/x86/kernel/x87_emu_{name}.c'),'-o',str(obj)],check=True)
        base = {name: (ROOT/f'arch/x86/kernel/x87_emu_{name}.c').read_text() for name in SOURCES}
        def execute(change=None):
            built=[]
            for name, text in base.items():
                if change and name == change[1]:
                    assert text.count(change[2]) == 1, change[0]
                    text=text.replace(change[2],change[3])
                path=tmp/f'x87_emu_{name}.c'; path.write_text(text)
                obj=path.with_suffix('.o')
                subprocess.run(['gcc', *flags, '-Wall', '-Wextra', '-Werror', '-c',str(path),'-o',str(obj)],check=True)
                built.append(str(obj))
            exe=tmp/'fixture'
            subprocess.run(['gcc','-m32','-nostdlib','-static','-no-pie',*built,*objects,'-o',str(exe)],check=True)
            undefined=subprocess.run(['nm','-u',str(exe)],check=True,capture_output=True,text=True).stdout
            assert not undefined.strip(), undefined
            return host32.run([str(exe)],timeout=30).returncode
        rc=execute()
        assert rc == 0, ('baseline',rc)
        print(f'ESC0..7 core/oracle: GREEN runner={host32.selected_runner()} libc=none',flush=True)
        if '--mutate' in sys.argv:
            for change in MUTATIONS:
                rc=execute(change)
                assert 0 < rc < 128, (change[0],rc)
                print(f'{change[0]}: runtime RED rc={rc}',flush=True)

def run(ctx):
    args=['python3','-B','tools/tests/test_nofpu_emu.py']
    if ctx.mutate: args.append('--mutate')
    return subprocess.call(args,cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__ == '__main__': main()
