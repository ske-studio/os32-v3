#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §8 N1・N2.

N2 CRT calls are deferred to N3 by the PM. The temporary closure fixture links
syscalls/help/newlib; the freestanding PPC runner observes loader BSS bounds.
"""
import copy
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'sdk/allocator')]
import check_nano_inputs_target as inputs
import check_nano_inputs as x86
import build_nano_target as builder
import build_nano as xbuilder
import check_link_target as gate

CHECK = dict(name='nano-targets', env=['ppc-cross'], tier=2, budget=60, needs=['make:all'],
    covers=['tools/tests/test_nano_targets.py', 'tools/check_nano_inputs*.py',
            'tools/ci/build_cross_ppc.sh', 'tools/ci/build_cross.sh', 'sdk/allocator/**',
            'sdk/crt/**', 'sdk/link/ppc/app.ld', 'sdk/include/**',
            'userland/lib/rt/ls.c'],
    basis=['PPC_FP_NEWLIB_DESIGN §8 N1・N2'])


def rejected(fn):
    try:
        fn()
    except (inputs.Mismatch, ValueError, KeyError):
        return
    raise AssertionError('accepted invalid target/receipt/provider')


def run(args):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


def compile_c(prefix, source, output):
    inputs.command(prefix, 'gcc', *inputs.ABI, '-std=gnu11', '-O2',
        '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
        '-Wno-attributes', '-ffunction-sections', '-fdata-sections',
        '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/include/os32'),
        '-include', ROOT / 'sdk/include/os32/os32_unit_stamp.h',
        '-c', source, '-o', output)


def closure(prefix, work, ledger, receipt):
    objects = []
    for name in ('syscalls', 'help'):
        obj = work / (name + '.o')
        compile_c(prefix, ROOT / ('sdk/crt/' + name + '.c'), obj)
        objects.append(obj)
    archive = builder.build(prefix, work / 'nano', crt=objects[0], receipt=receipt)
    source = work / 'app.c'
    source.write_text('''
#include "os32api.h"
#include "os32/help.h"
#include <stdio.h>
#include <stdlib.h>
KernelAPI *kapi;
extern void _init(void);
void _start(void) {
    _init(); os32_help_show("nano");
    void *p = malloc(17); printf("%.2f %p", 1.25, p); free(p);
}
''')
    app = work / 'app.o'
    compile_c(prefix, source, app)
    elf, mapfile = work / 'app.elf', work / 'app.map'
    libc, libgcc = inputs.libraries(prefix)
    inputs.command(prefix, 'ld', '-m', 'elf32ppc', '--gc-sections',
        '--emit-relocs', '-T', ROOT / 'sdk/link/ppc/app.ld',
        '-u', '_printf_float', '-Map=' + str(mapfile), '-o', elf, *objects, app,
        '--start-group', archive, libc, libgcc, '--end-group')
    assert not inputs.command(prefix, 'nm', '-u', elf).strip(), 'undefined symbols'
    sections = inputs.command(prefix, 'readelf', '-SW', elf).decode()
    for line in sections.splitlines():
        match = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+(\S+)\s+\S+\s+(\S+)', line)
        if match and 'A' in match[3] and int(match[2], 16):
            assert match[1] in ('.text', '.data', '.bss'), 'orphan alloc: ' + line
    reloc = inputs.command(prefix, 'readelf', '-rW', elf).decode()
    assert not any(x in reloc for x in ('SDAREL', 'SDA2REL', 'EMB_SDA')), reloc
    gate.validate_output(prefix, mapfile, elf, archive.with_suffix('.json'))
    bad = json.loads(archive.with_suffix('.json').read_text())
    bad['target'] = 'i386-elf'
    wrong = work / 'wrong-link.json'
    wrong.write_text(json.dumps(bad))
    rejected(lambda: gate.validate_output(prefix, mapfile, elf, wrong))
    assert not elf.exists(), 'rejected output retained'
    duplicate = work / 'duplicate.c'
    duplicate.write_text('void *malloc(unsigned n) { return (void *)n; }')
    compile_c(prefix, duplicate, work / 'duplicate.o')
    with mapfile.open('a') as stream:
        stream.write('\nLOAD ' + str(work / 'duplicate.o') + '\n')
    rejected(lambda: gate.check(prefix, mapfile, archive.with_suffix('.json')))
    return archive


def sentinel(prefix, work, script):
    source = work / 'sentinel.c'
    source.write_text('''
#include "nano_adapter.h"
_Static_assert(sizeof(void *) == 4 && sizeof(size_t) == 4, "ILP32");
_Static_assert(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__, "BE");
_Static_assert(sizeof(struct mallinfo) == 40, "nano layout");
volatile unsigned small __attribute__((section(".sdata"))) = 0x12345678;
const unsigned ro __attribute__((section(".sdata2"))) = 0x87654321;
volatile unsigned zero __attribute__((section(".sbss")));
volatile unsigned zero2 __attribute__((section(".sbss2")));
extern unsigned char __bss_start[], __bss_end[];
void _start(void) {
    zero = zero2 = 99;
    for (volatile unsigned char *p = __bss_start; p < __bss_end; ++p) *p = 0;
    register unsigned r0 __asm__("r0") = 1;
    register unsigned r3 __asm__("r3") = zero || zero2 || small != 0x12345678 || ro != 0x87654321;
    __asm__ volatile("sc" : : "r"(r0), "r"(r3) : "memory");
    for (;;) {}
}
''')
    obj, elf = work / 'sentinel.o', work / 'sentinel.elf'
    inputs.command(prefix, 'gcc', *inputs.ABI, '-O2', '-ffreestanding',
        '-fno-pie', '-fno-stack-protector', '-I' + str(ROOT / 'sdk/allocator'),
        '-c', source, '-o', obj)
    inputs.command(prefix, 'ld', '-m', 'elf32ppc', '-T', script, '-o', elf, obj)
    return subprocess.run(['qemu-ppc', str(elf)], capture_output=True).returncode


def cases(prefix, work, mutate):
    ledger = inputs.select(json.loads(inputs.LEDGER.read_text()))
    inputs.check(prefix, ledger)
    inputs.sdk_inputs(ledger)
    bad = copy.deepcopy(ledger)
    bad['toolchain']['local']['members']['libc_a-mallocr.o'] = '0' * 64
    rejected(lambda: inputs.check(prefix, bad))
    receipt = work / 'receipt.json'
    trusted = dict(inputs.inventory(prefix, ledger), target=inputs.TARGET, abi=inputs.ABI,
        upstream=ledger['toolchain']['upstream'], configure=ledger['toolchain']['configure'],
        builder_sha256=inputs.digest(inputs.BUILDER.read_bytes()))
    receipt.write_text(json.dumps(trusted))
    inputs.check(prefix, ledger, receipt)
    archive = closure(prefix, work, ledger, receipt)
    for key, value in (('target', 'i386-elf'), ('abi', ['-msoft-float']),
                       ('builder_sha256', 'wrong'), ('configure', [])):
        bad = dict(trusted, **{key: value})
        receipt.write_text(json.dumps(bad))
        rejected(lambda: builder.build(prefix, work / 'bad', receipt=receipt))
    receipt.write_text(json.dumps(trusted))
    builder.build(prefix, work / 'reuse', reuse=archive, receipt=receipt)
    assert sentinel(prefix, work, ROOT / 'sdk/link/ppc/app.ld') == 0
    # Execute the unchanged x86 builder twice at exactly the same absolute path.
    xprefix = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
    old = json.loads(subprocess.check_output(['git', 'show', 'a0429c94:sdk/allocator/nano_inputs.json'], cwd=ROOT))
    current = json.loads(inputs.LEDGER.read_text())
    assert {k: current[k] for k in ('toolchain', 'sdk_build')} == {k: old[k] for k in ('toolchain', 'sdk_build')}
    assert x86.cache_key(current) == x86.cache_key(old)
    xcrt = work / 'x86-syscalls.o'
    x86.command(xprefix, 'gcc', '-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin',
        '-fno-pie', '-I' + str(ROOT / 'sdk/include/os32'), '-include',
        ROOT / 'sdk/include/os32/os32_unit_stamp.h', '-c', ROOT / 'sdk/crt/syscalls.c', '-o', xcrt)
    # ar defaults to U on this host. Freeze time/stat metadata, preserving the
    # real prefix, builder arguments, member order and archive bytes verbatim.
    freeze = work / 'freeze.c'
    freeze.write_text(r'''#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>
time_t time(time_t *p) { if (p) *p=946684800; return 946684800; }
#define WRAP(name, argtype) \
int name(argtype arg, struct stat *s) { \
    int (*real)(argtype,struct stat *) = dlsym(RTLD_NEXT, #name); \
    int rc=real(arg,s); if (!rc) s->st_mtime=946684800; return rc; }
WRAP(stat, const char *)
WRAP(fstat, int)
''')
    frozen = work / 'freeze.so'
    run(['gcc', '-shared', '-fPIC', freeze, '-ldl', '-o', frozen])
    env = dict(os.environ, LD_PRELOAD=str(frozen))
    args = [sys.executable, '-B', ROOT / 'sdk/allocator/build_nano.py',
            '--cross-dir', xprefix, work / 'x86', '--crt', xcrt]
    subprocess.run(list(map(str, args)), env=env, check=True, capture_output=True)
    xarchive = work / 'x86/libos32nano.a'
    before = xarchive.read_bytes()
    subprocess.run(list(map(str, args)), env=env, check=True, capture_output=True)
    assert xarchive.read_bytes() == before
    rejected(lambda: builder.build(prefix, work / 'wrong-reuse', reuse=xarchive, receipt=receipt))
    print('x86 bytes/JSON/cache-key MATCH: ' + inputs.digest(before), flush=True)
    if mutate:
        # Mutate the receipt guard in the real builder; the construction executes.
        source = (ROOT / 'tools/check_nano_inputs_target.py').read_text()
        anchor = 'require(expected["target"] == TARGET and expected["abi"] == inputs["abi"],'
        assert source.count(anchor) == 1
        mutant = work / 'check_nano_inputs_mutant.py'
        mutant.write_text(source.replace(anchor, 'require(True,'))
        spec = importlib.util.spec_from_file_location('nano_mutant', mutant)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT, module.BUILDER, module.LEDGER = ROOT, inputs.BUILDER, inputs.LEDGER
        original = builder.inputs
        builder.inputs = module
        receipt.write_text(json.dumps(dict(trusted, target='i386-elf')))
        try:
            try:
                rejected(lambda: builder.build(prefix, work / 'mutant', receipt=receipt))
            except AssertionError:
                print('runtime RED: PPC receipt replaced with i386', flush=True)
            else:
                raise AssertionError('receipt mutation did not reach construction')
        finally:
            builder.inputs = original
        text = (ROOT / 'sdk/link/ppc/app.ld').read_text()
        anchor = '        *(.sbss .sbss.* .sbss2 .sbss2.*)'
        assert text.count(anchor) == 1
        script = work / 'mutant.ld'
        script.write_text(text.replace(anchor, ''))
        assert sentinel(prefix, work, script) == 1, 'sbss mutation survived'
        print('runtime RED: sbss excluded from loader BSS range', flush=True)
        assert sentinel(prefix, work, ROOT / 'sdk/link/ppc/app.ld') == 0
        receipt.write_text(json.dumps(trusted))
        builder.build(prefix, work / 'restored', receipt=receipt)
    print('nano targets GREEN', flush=True)


def main():
    prefix = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (prefix / 'bin/powerpc-elf-gcc').is_file() or not shutil.which('qemu-ppc'):
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ': PPC toolchain/QEMU unavailable')
        return 1 if required else 0
    with tempfile.TemporaryDirectory(prefix='nano_targets_') as work:
        cases(prefix, Path(work), '--mutate' in sys.argv)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(exc.stderr.decode() if isinstance(exc.stderr, bytes) else exc.stderr, file=sys.stderr)
        raise


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_nano_targets.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
