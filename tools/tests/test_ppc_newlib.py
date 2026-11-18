#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §4 N3・§8 / astra レビュー 2026-11-18.

heap の回収は DEFERRED PPC-NEWLIB-HEAP-RECLAIM.

Real PPC CRT execution and real nano/float execution on qemu-user. System
heap/file/exit acceptance is tools/accept/ppc_newlib.py (PM socket runner).
"""
import os
import importlib.util
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/tests'), str(ROOT/'sdk/allocator'), str(ROOT/'tools')]
import test_nano_targets as nano
import build_nano_target as builder
import check_nano_inputs_target as inputs

# Fixtures build their own objects; only the generated SDK headers are needed (as nano-targets).
CHECK = dict(name='ppc-newlib', tier=2, env=['ppc-cross'], budget=60,
    needs=['make:sdk/include/os32/os32_unit_stamp.h'],
    covers=['tools/tests/test_ppc_newlib.py', 'tools/tests/ppc_newlib_host.c',
            'tools/accept/ppc_newlib.py', 'build/ppc-user.mk', 'sdk/crt/ppc/start.c',
            'sdk/link_guard.py', 'userland/ppc/nlprobe.c',
            'userland/ppc/sh/newlib_runtime.c','tools/tests/test_nano_targets.py', 'sdk/allocator/**',
            'tools/check_nano_inputs*.py', 'tools/ci/build_cross*.sh',
            'sdk/crt/**', 'sdk/include/**', 'sdk/link/ppc/app.ld', 'build/ppc-ram-root.json',
            'userland/lib/rt/ls.c'], basis=['PPC_FP_NEWLIB_DESIGN §8 N2・N3'])

FLOAT = b'N3-float=1.25,-2.50,3.75\n'
ADAPTER = ('void *malloc(size_t size) { return _malloc_r(_impure_ptr, size); }',
           'void *malloc(size_t size) { return size == 65535 ? NULL : _malloc_r(_impure_ptr, size); }')


def execute(prefix, work, adapter=None, make_source=None):
    make_source = make_source or (ROOT/'build/ppc-user.mk').read_text()
    recipe=work/'flags.mk'
    recipe.write_text('SDK_TARGET_CALL_ABI := powerpc-sysv\nCC := '+str(prefix/'bin/powerpc-elf-gcc')+'\n'+make_source+
                      '\nprint-link:\n\t@echo $(PPC_USER_LINK)\n')
    flags=shlex.split(subprocess.check_output(['make','-s','-f',str(recipe),'print-link'],cwd=ROOT,text=True))
    floating=any(a == '-u' and b == '_printf_float' for a,b in zip(flags,flags[1:]))
    archive = builder.build(prefix, work/'nano', adapter=adapter)
    obj = work/'host.o'
    # The fixture uses the allocator header, unlike ordinary SDK USER units.
    inputs.command(prefix, 'gcc', *inputs.ABI, '-O2', '-ffreestanding', '-fno-builtin',
        '-fno-pie', '-fno-stack-protector', '-Wno-attributes',
        '-I'+str(ROOT/'sdk/include/os32'), '-I'+str(ROOT/'sdk/allocator'),
        '-c', ROOT/'tools/tests/ppc_newlib_host.c', '-o', obj)
    libc, libgcc = inputs.libraries(prefix)
    elf = work/'host.elf'
    inputs.command(prefix, 'ld', '-m', 'elf32ppc', '-e', '_start', '-o', elf, obj,
        *(['-u', '_printf_float'] if floating else []),
        '--start-group', archive, libc, libgcc, '--end-group')
    result = subprocess.run(['qemu-ppc', str(elf)], capture_output=True)
    return result.returncode, result.stdout


def crt(prefix, work, source):
    start = work/'start.c'; start.write_text(source)
    fixture = work/'crt.c'
    fixture.write_text(r'''
#include "os32api.h"
extern KernelAPI *kapi;
extern void *__os32_aux;
extern void os32_start_c(int, char **, KernelAPI *, void *);
static KernelAPI api;
static int initialized, mode, help_calls;
static void stop(int code) {
    register int r0 __asm__("r0")=1;
    register int r3 __asm__("r3")=code;
    __asm__ volatile("sc" : : "r"(r0), "r"(r3) : "memory");
    for (;;) {}
}
void _init(void) { initialized = kapi == &api && __os32_aux == &mode; }
int os32_help_show(const char *s) { (void)s; ++help_calls; return mode == 1 ? 0 : -1; }
int main(int argc, char **argv, KernelAPI *p) {
    (void)argv;
    if (!initialized || p != &api || argc != 2 || mode == 1) return 11;
    return 7;
}
static void finish(int code) {
    if (!initialized) stop(11);
    if (help_calls != (mode ? 1 : 0)) stop(13);
    help_calls = 0;
    if (code != (mode == 1 ? 0 : 7)) stop(12);
    if (++mode == 4) stop(0);
    char *flags[] = {"probe", mode == 1 ? "--help" : mode == 2 ? "-h" : "/?"};
    os32_start_c(2, flags, &api, &mode);
}
void _start(void) {
    api.sys_exit = finish;
    char *args[] = {"probe", "normal"};
    os32_start_c(2, args, &api, &mode);
}
''')
    for name, src in (('crt',fixture), ('start',start)):
        nano.compile_c(prefix, src, work/(name+'.o'))
    elf=work/'crt.elf'
    inputs.command(prefix, 'ld', '-m', 'elf32ppc', '-T', ROOT/'sdk/link/ppc/app.ld', '-o', elf,
                   work/'crt.o', work/'start.o')
    return subprocess.run(['qemu-ppc', str(elf)], capture_output=True).returncode


def resource_modes(work):
    source = (ROOT/'userland/ppc/nlprobe.c').read_text()
    source = source[source.index('static int fds[256];'):source.index('\nint main(')]
    source = source.replace('__asm__ volatile("trap" : : : "memory")', '(void)0')
    fixture = work/'resources.c'
    fixture.write_text(r"""
#include <stdio.h>
#include <stdlib.h>
static unsigned allocations, releases, opens, closes;
static unsigned char block[65536];
static void *test_malloc(size_t n) { ++allocations; return n == sizeof block ? block : NULL; }
static void test_free(void *p) { if (p == block) ++releases; }
#define malloc test_malloc
#define free test_free
typedef struct { int (*sys_open)(const char *, int); int (*sys_close)(int); } KernelAPI;
static int open_file(const char *p, int flags) { (void)p; (void)flags; return opens++ < 3 ? (int)opens : -1; }
static int close_file(int fd) { (void)fd; ++closes; return 0; }
""" + source + r"""
int main(void) {
    KernelAPI api = { open_file, close_file };
    for (int mode = 0; mode < 3; ++mode) {
        allocations = releases = opens = closes = 0;
        if (resources(&api, mode != 0, mode == 2) || allocations != 1 ||
            opens != 4 || closes != (mode == 0 ? 3u : 0u) ||
            releases != (mode == 0 ? 1u : 0u)) return 1;
    }
    return 0;
}
""")
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', str(fixture), '-o', str(work/'resources')], check=True)
    subprocess.run([str(work/'resources')], check=True)
    print('resource modes GREEN: one 64 KiB allocation each')


def main():
    prefix = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (prefix/'bin/powerpc-elf-gcc').is_file() or not shutil.which('qemu-ppc'):
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP')+': PPC toolchain/QEMU unavailable')
        return int(required)
    spec=importlib.util.spec_from_file_location('newlib_accept',ROOT/'tools/accept/ppc_newlib.py')
    meter=importlib.util.module_from_spec(spec); spec.loader.exec_module(meter)
    for case,code in (('normal',0),('init',11),('sbss',12),('float',0),('adapter',21)):
        body=(b'N3-float=,,\n' if case == 'float' else FLOAT)+b'N3-exit='+str(code).encode()+b'\n'+(b'\n' if code == 0 else b'')
        status=str(code).encode()+b'\n'
        assert meter.verdict(case,body,status,'') != 'wrong-reason'
        for bad,rc,diag in ((body+b'extra',status,''),(body,b'99\n',''),(body,status,'FATAL trap')):
            assert meter.verdict(case,bad,rc,diag) == 'wrong-reason'
    with tempfile.TemporaryDirectory(prefix='ppc_newlib_') as folder:
        work=Path(folder)
        resource_modes(work)
        source=(ROOT/'sdk/crt/ppc/start.c').read_text()
        assert crt(prefix, work, source) == 0
        assert execute(prefix, work) == (0, FLOAT)
        assert nano.sentinel(prefix, work, ROOT/'sdk/link/ppc/app.ld') == 0
        if '--mutate' in sys.argv:
            anchor='    _init();'
            assert source.count(anchor) == 1
            assert crt(prefix, work, source.replace(anchor,'')) == 11
            print('runtime RED: N2 _init removed')
            for flag in ("s[0]=='-' && s[1]=='h' && s[2]=='\\0'", "s[0]=='/' && s[1]=='?' && s[2]=='\\0'"):
                assert source.count(flag) == 1
                assert crt(prefix, work, source.replace(flag, '0')) == 13
                print('runtime RED: help flag removed:', flag)

            assert crt(prefix, work, source) == 0
            script=(ROOT/'sdk/link/ppc/app.ld').read_text()
            anchor='        *(.sbss .sbss.* .sbss2 .sbss2.*)'
            assert script.count(anchor) == 1
            mutant=work/'app.ld'; mutant.write_text(script.replace(anchor,''))
            assert nano.sentinel(prefix, work, mutant) == 1
            print('runtime RED: N2 sbss removed')
            assert nano.sentinel(prefix, work, ROOT/'sdk/link/ppc/app.ld') == 0
            make_source=(ROOT/'build/ppc-user.mk').read_text()
            anchor='PPC_USER_FLOAT ?= -u _printf_float'
            assert make_source.count(anchor) == 1
            assert execute(prefix, work, make_source=make_source.replace(anchor,'PPC_USER_FLOAT ?=')) == (0, b'N3-float=,,\n')
            print('runtime RED: N3 _printf_float removed')
            assert execute(prefix, work) == (0, FLOAT)
            adapter=(ROOT/'sdk/allocator/nano_adapter.c').read_text()
            assert adapter.count(ADAPTER[0]) == 1
            mutant=work/'adapter.c'; mutant.write_text(adapter.replace(*ADAPTER))
            assert execute(prefix, work, adapter=mutant)[0] == 21
            print('runtime RED: N3 adapter route broken')
            assert execute(prefix, work) == (0, FLOAT)
        print('PPC newlib GREEN')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(exc.stderr.decode() if isinstance(exc.stderr, bytes) else exc.stderr, file=sys.stderr)
        raise


def run(ctx):
    return subprocess.call(['python3','-B','tools/tests/test_ppc_newlib.py']+
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
