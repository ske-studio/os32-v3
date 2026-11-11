#!/usr/bin/env python3
"""KAPI target selection and isolated output roots."""
CHECK = dict(name='kapi-target-host', tier=1, needs=[], budget=5,
             covers=['sdk/gen_kapi.py', 'sdk/target_profile.py', 'sdk/kapi.json',
                     'kapi/kapi_generated.c', 'sdk/link/generations.ld',
                     'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs',
                     'tools/tests/test_kapi_target.py'])
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ('include/kapi_call_generated.h', 'sdk/include/os32/os32_kapi_generated.h',
           'sdk/include/os32/os32_kapi_slots.h', 'kapi/kapi_generated.c',
           'exec/exec_kapi_init.inc', 'sdk/include/os32/os32_generations.h',
           'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs',
           'sdk/include/os32/os32_unit_stamp.h', 'sdk/crt/generations.inc',
           'sdk/link/generations.ld')


def generate(script, target, output):
    return subprocess.run([sys.executable, '-B', str(script), '--target', target,
                           '--out-dir', str(output), str(ROOT / 'sdk/kapi.json')],
                          cwd=ROOT, capture_output=True, text=True)


def exercise_adapter(root, output):
    api = json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
    slots = {entry['name']: index for index, entry in enumerate(api)}
    source = root / 'adapter_test.c'
    source.write_text(r'''#include "os32_kapi_shared.h"
struct kapi_values { u32 value[8]; };
#include "ppc_kapi_adapter_generated.h"
static KernelAPI table;
static int seen;
static void put(char c, u8 attr) { seen = c + attr; }
static int open_file(const char *path, int flags) {
    return path == (const char *)0x1234 && flags == -7 ? -23 : 99;
}
void _start(void) {
    struct kapi_values values = {{65, 3}};
    int status = 0;
    table.shell_putchar = put;
    table.sys_open = open_file;
    if (ppc_kapi_invoke(&table, PUT_SLOT, &values) != 0 || seen != 68) status = 1;
    values.value[0] = 0x1234; values.value[1] = (u32)-7;
    if (ppc_kapi_invoke(&table, OPEN_SLOT, &values) != (u32)-23 && status == 0) status = 2;
    if (ppc_kapi_invoke(&table, VAR_SLOT, &values) != (u32)OS32_ERR_NOSYS) status = 3;
    if (ppc_kapi_invoke(&table, 9999, &values) != (u32)OS32_ERR_NOSYS) status = 4;
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(status));
    __builtin_unreachable();
}
'''.replace('PUT_SLOT', str(slots['shell_putchar']))
       .replace('OPEN_SLOT', str(slots['sys_open']))
       .replace('VAR_SLOT', str(slots['kprintf'])))
    binary = root / 'adapter_test'
    compile_result = subprocess.run(['gcc', '-m32', '-Wall', '-Wextra', '-Werror',
                                     '-ffreestanding', '-fno-pie', '-no-pie',
                                     '-nostdlib', '-static', '-I', str(ROOT / 'sdk/include/os32'),
                                     '-I', str(output / 'include'), str(source), '-o', str(binary)],
                                    capture_output=True, text=True)
    assert compile_result.returncode == 0, compile_result.stderr
    return subprocess.run(['qemu-i386', str(binary)], capture_output=True).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        script = ROOT / 'sdk/gen_kapi.py'
        x86, ppc = root / 'x86', root / 'ppc'
        for target, output in [('x86-pc98', x86), ('ppc-powermac', ppc)]:
            result = generate(script, target, output)
            assert result.returncode == 0, result.stderr
            assert all((output / name).is_file() for name in OUTPUTS)
        for name in OUTPUTS:
            assert (x86 / name).read_bytes() == (ROOT / name).read_bytes(), name
        assert not (x86 / 'include/ppc_kapi_adapter_generated.h').exists()
        assert exercise_adapter(root, ppc) == 0
        denied = subprocess.run([sys.executable, '-B', str(script), '--target',
                                 'ppc-powermac', '--check'], cwd=ROOT,
                                capture_output=True, text=True)
        assert denied.returncode != 0 and 'requires --out-dir' in denied.stderr
        if args.mutate:
            source = script.read_text()
            anchor = 'arguments.append(f"({kind})values->value[{index}]")'
            assert source.count(anchor) == 1
            mutant = root / 'adapter_mutant.py'
            mutant.write_text(source.replace(anchor, anchor.replace('{index}', '0')))
            import os
            env = dict(os.environ, PYTHONPATH=str(ROOT / 'sdk'))
            mutant_output = root / 'adapter_mutant'
            result = subprocess.run([sys.executable, '-B', str(mutant), '--target',
                                     'ppc-powermac', '--out-dir', str(mutant_output),
                                     str(ROOT / 'sdk/kapi.json')], cwd=ROOT, env=env,
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            assert exercise_adapter(root, mutant_output) == 1
            print('RED PPC adapter: argument position')
            source = script.read_text()
            anchor = "require(args.target, 'sdk_user')"
            assert source.count(anchor) == 1
            mutant = root / 'gen_kapi.py'
            mutant.write_text(source.replace(anchor, "require(args.target, 'layout')"))
            # The copied generator imports its real helper modules from sdk/.
            import os
            env = dict(os.environ, PYTHONPATH=str(ROOT / 'sdk'))
            result = subprocess.run([sys.executable, '-B', str(mutant), '--target',
                                     'ppc-powermac', '--out-dir', str(root / 'mutant'),
                                     str(ROOT / 'sdk/kapi.json')], cwd=ROOT, env=env,
                                    capture_output=True, text=True)
            assert result.returncode != 0 and 'unsupported kernel layout target' in result.stderr
            assert not (root / 'mutant').exists()
            print('RED PPC acceptance: layout capability rejects target')
    print('PASS target output isolation and x86 byte equality')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
