#!/usr/bin/env python3
"""T2h h2: built stack headers/deploy, live ILP32 frames, isolated rejects.
Run make all first; fixtures are generated only in a temporary directory. Mutation compile errors never count RED.
"""
import argparse
import importlib.util
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk'))
import os32x_hdr as H

TARGET_SRC = [ROOT / 'userland/tests/h2_stack.c', ROOT / 'userland/tests/h2_stack_probe.inc',
              ROOT / 'tools/gen_h2_fixtures.py']
MUTANTS = [
    ('pattern write', 'a[i] = (unsigned char)(H2_PATTERN ^ (i / H2_PAGE_BYTES));', 'a[i] = 0;'),
    ('deep recursion', 'ok = h2_deep(p, depth - 1);', 'ok = 1;'),
    ('argv before wait', 'ok = h2_argv_ok(p);', 'ok = 1;'),
    ('wait callback', 'if (ok && p->wait) ok = p->wait(p, frame, sizeof(frame));', 'if (ok && p->wait) ok = 1;'),
    ('argv after resume', 'if (!h2_argv_ok(p)) ok = 0;', 'if (0) ok = 0;'),
    ('frame after resume', 'return h2_verify(frame, sizeof(frame)) && ok;', 'return ok;'),
    ('large array after resume', 'return h2_verify(array, H2_LARGE_BYTES) && ok;', 'return ok;'),
    ('small array after resume', 'return h2_verify(array, H2_SMALL_BYTES) && ok;', 'return ok;'),
    ('large selection', '? H2_LARGE_BYTES : H2_SMALL_BYTES', '? H2_SMALL_BYTES : H2_SMALL_BYTES'),
    ('large depth', '? 7U : 3U', '? 6U : 3U'),
    ('small depth', '? 7U : 3U', '? 7U : 2U'),
    ('guard page', ' - stack - MEM_GUARD_SIZE;', ' - stack;'),
    ('entry argc', 'if (argc != 3) return 0;', 'if (argc == -1) return 0;'),
]


def run(cmd, **kw):
    return subprocess.run([str(x) for x in cmd], cwd=ROOT, capture_output=True, text=True, **kw)


def require(cond, message):
    if not cond:
        raise AssertionError(message)


def compile_probe(tmp, body):
    (tmp / 'h2_stack_probe.inc').write_text(body)
    (tmp / 'h2_stack_probe.h').write_bytes((ROOT / 'userland/tests/h2_stack_probe.h').read_bytes())
    (tmp / 'probe.c').write_bytes((ROOT / 'tools/tests/h2_stack_host.c').read_bytes())
    exe = tmp / 'probe'
    r = run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
             '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib',
             '-Wl,-e,_start', '-Iinclude', '-Isdk/include/os32', tmp / 'probe.c', '-o', exe])
    require(r.returncode == 0, 'probe compile failed (not RED): ' + r.stderr)
    return exe


def test_built(tmp):
    deploy = (ROOT / 'userland/deploy.yaml').read_text()
    guards, stacks = [], []
    for name, stack in [('h2_stack', 0), ('h2_stack512', 524288)]:
        binary = ROOT / ('userland/tests/' + name + '.bin')
        h = H.parse_header(binary.read_bytes())
        require(h['stack_size'] == stack, name + ': built stack request')
        require(h['version'] == H.OS32X_HDR_VERSION and h['load_addr'] == H.OS32X_APP_LOAD_ADDR,
                name + ': current high image')
        require('    - host: userland/tests/' + name + '.bin\n      guest: /usr/bin/\n      tags: [test]' in deploy,
                name + ': exact deploy registration')
        # mkos32x stores .raw verbatim after the header; make may remove .raw
        # as an intermediate. i386 C6 /0: movb $H2_PATTERN, guard_va.
        raw = binary.read_bytes()[h['header_size']:]
        stores = re.findall(rb'\xc6\x05(.{4})\xa5', raw, re.DOTALL)
        require(len(stores) == 1, name + ': exactly one absolute guard store')
        guards.append(struct.unpack('<I', stores[0])[0])
        stacks.append(h['stack_size'])
    src, exe = tmp / 'guards.c', tmp / 'guards'
    src.write_text('#include "memmap.h"\n'
                   'static const unsigned int guards[]={' + ','.join(map(str, guards)) + '};\n'
                   'static const unsigned int stacks[]={' + ','.join(map(str, stacks)) + '};\n'
                   'void _start(void) { unsigned int i; int rc=0; '
                   'for(i=0;i<2;i++) { unsigned int stack=stacks[i] ? stacks[i] : MEM_EXEC_STACK_SIZE; '
                   'if(guards[i]!=MEM_APP_STACK_TOP-stack-MEM_GUARD_SIZE) rc=1; } '
                   '__asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); '
                   '__builtin_unreachable(); }\n')
    r = run(['gcc', '-m32', '-std=gnu11', '-O2', '-ffreestanding', '-fno-pie', '-no-pie',
             '-fno-stack-protector', '-nostdlib', '-Iinclude', src, '-o', exe])
    require(r.returncode == 0, r.stderr)
    r = host32.run([str(exe)], capture_output=True, text=True)
    require(r.returncode == 0, 'built raw guard/header stack mismatch: ' + str(r.returncode))
    print('PASS built headers/deploy and raw guard/header agreement: 2 variants')


def test_fixtures(tmp):
    spec = importlib.util.spec_from_file_location('h2_generator', ROOT / 'tools/gen_h2_fixtures.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    out = tmp / 'isolated'
    records = generator.generate(out)
    expected = {
        'control-app.fixture': 0,
        'control-shell.fixture': 0,
        'control-shlib.fixture': 0,
        'control-dependent.fixture': 0,
        'old-app.fixture': 2,
        'old-shell.fixture': 2,
        'old-shlib.fixture': 2,
        'unknown-format.fixture': 2,
        'bad-abi-generation.fixture': 3,
        'bad-memory-generation.fixture': 3,
        'old-cpl0-flag.fixture': 2,
        'bad-shlib-generation.fixture': 3,
        'bad-shell-generation.fixture': 3,
    }
    require(records.keys() == expected.keys(), 'exact fixture names')
    reasons = {'valid control': 0, 'unknown format or flags': 2,
               'generation or KAPI layout mismatch': 3}
    manifest = json.loads((out / 'manifest.json').read_text())
    require(manifest.keys() == expected.keys(), 'exact manifest fixture names')
    require({name: reasons[record['expected']] for name, record in manifest.items()} == expected,
            'manifest reasons must agree with the fixed admission rc table')
    # Real ILP32 admission predicate on the serialized headers, no guest entry.
    src = tmp / 'admit.c'
    heads, verdicts = [], []
    for name, record in records.items():
        heads.append('{' + ','.join(str(v) for v in (out / name).read_bytes()[:H.OS32X_HDR_SIZE]) + '}')
        verdicts.append(expected[name])
    src.write_text('#include "exec/os32x_hdr.c"\n'
                   'static const unsigned char heads[][60]={' + ','.join(heads) + '};\n'
                   'static const int expected[]={' + ','.join(map(str, verdicts)) + '};\n'
                   'void _start(void) { OS32Header h; unsigned int i,j; int rc=0; '
                   'for(i=0;i<sizeof(expected)/sizeof(expected[0]);i++) { '
                   'for(j=0;j<sizeof(h);j++) ((unsigned char *)&h)[j]=heads[i][j]; '
                   'if(os32x_layout_check(&h,sizeof(h),KAPI_DATA_FIELDS_OFF)!=expected[i]) rc=1; } '
                   '__asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); '
                   '__builtin_unreachable(); }\n')
    exe = tmp / 'admit'
    r = run(['gcc', '-m32', '-std=gnu11', '-O2', '-ffreestanding', '-fno-pie', '-no-pie',
             '-fno-stack-protector', '-nostdlib', '-I.', '-Isdk/include/os32', src, '-o', exe])
    require(r.returncode == 0, r.stderr)
    r = host32.run([str(exe)], capture_output=True, text=True)
    require(r.returncode == 0, 'serialized fixture admission reasons: ' + str(r.returncode))
    require('h2-isolated' not in (ROOT / 'userland/deploy.yaml').read_text(), 'isolated deploy leak')
    print('PASS emitted fixture admission: 4 controls / 9 rejects; manifest reasons match fixed rc table')


def test_old_object(tmp):
    cross = pathlib.Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'
    cc, ld, objcopy = [cross / ('i386-elf-' + n) for n in ('gcc', 'ld', 'objcopy')]
    # Current start + an unmarked old unit. No invented valid old generations.
    start = tmp / 'start.c'
    start.write_text('#include "os32_kapi_slots.h"\nOS32_KAPI_LAYOUT_STAMP();\n'
                     'extern int old_unit(void);\n'
                     'void _start(void) { volatile int v=old_unit(); (void)v; for(;;) {} }\n')
    old = tmp / 'old.c'
    old.write_text('int old_unit(void) { return 17; }\n')
    common = ['-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-Isdk/include/os32']
    for source, stamp in [(start, True), (old, False)]:
        flags = ['-include', 'sdk/include/os32/os32_unit_stamp.h'] if stamp else []
        r = run([cc, *common, *flags, '-c', source, '-o', source.with_suffix('.o')])
        require(r.returncode == 0, r.stderr)
    elf = tmp / 'mixed.elf'
    args = [ld, '-m', 'elf_i386', '-T', 'sdk/link/app.ld', '-o', elf,
            start.with_suffix('.o'), old.with_suffix('.o')]
    r = run([sys.executable, 'sdk/link_guard.py', *args])
    require(r.returncode != 0 and 'missing per-unit generations' in r.stderr and not elf.exists(),
            'new SDK + old .o must refuse link and remove ELF: ' + r.stderr)
    # Bare ld is confined to this negative test: no wrapper may publish its ELF.
    r = run(args)
    require(r.returncode == 0, r.stderr)
    raw, binary = tmp / 'mixed.raw', tmp / 'mixed.bin'
    r = run([objcopy, '-O', 'binary', elf, raw])
    require(r.returncode == 0, r.stderr)
    r = run([sys.executable, 'sdk/mkos32x.py', raw, binary, '--elf', elf])
    require(r.returncode != 0 and not binary.exists() and 'link' in r.stderr.lower(),
            'packaging must reject bypassed old .o: ' + r.stderr)
    print('PASS new SDK + old .o: link refused, packaging refused, no guest binary')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='h2-') as name:
        tmp = pathlib.Path(name)
        test_built(tmp)
        test_fixtures(tmp)
        test_old_object(tmp)
        body = (ROOT / 'userland/tests/h2_stack_probe.inc').read_text()
        exe = compile_probe(tmp, body)
        r = host32.run([str(exe)], capture_output=True, text=True)
        require(r.returncode == 0, 'live ILP32 probe: ' + str(r.returncode))
        print('PASS live ILP32: 2 plans, 3 entry argc checks, 12 live-frame cases (both sizes)')
        if args.mutate:
            for label, old, new in MUTANTS:
                count = 1
                require(body.count(old) == count, label + ': replacement count drift')
                exe = compile_probe(tmp, body.replace(old, new))
                r = host32.run([str(exe)], capture_output=True, text=True)
                require(r.returncode != 0, 'SURVIVED: ' + label)
                print('RED runtime: ' + label + ' rc=' + str(r.returncode))
            print('PASS mutations: 13/13 runtime RED, compile failures 0')


if __name__ == '__main__':
    main()
