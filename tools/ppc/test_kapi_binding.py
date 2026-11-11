#!/usr/bin/env python3
"""Check PowerMac's typed bindings for the absent IME provider."""
CHECK = dict(name='ppc-kapi-binding', tier=1,
             covers=['sdk/kapi_target_ppc.py', 'tools/ppc/test_kapi_binding.py'],
             inputs=['sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py',
                     'sdk/kapi.json'], needs=[], budget=5)
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verify(source, entries):
    if '#include "ime.h"' in source:
        return False
    for api in entries:
        name = api['name']
        target = api.get('target', name)
        if re.search(r'\b' + re.escape(target) + r'\s*\(', source):
            return False
        args = ', '.join(api.get('args', [])) or 'void'
        body = ''.join('    (void)' + re.search(r'([a-zA-Z0-9_]+)(?:\[[0-9]*\])?$', arg).group(1) + ';\n'
                       for arg in api.get('args', []))
        if api['ret'] != 'void':
            body += '    return (' + api['ret'] + ')OS32_ERR_NOSYS;\n'
        expected = api['ret'] + ' __cdecl wrap_' + name + '(' + args + ')\n{\n' + body + '}\n'
        if expected not in source:
            return False
    return True


def generate(tmp, mutation=None):
    sdk = tmp / 'sdk'
    sdk.mkdir()
    for name in ('gen_kapi.py', 'kapi_abi_x86.py', 'target_profile.py', 'kapi_target_ppc.py'):
        shutil.copy(ROOT / 'sdk' / name, sdk / name)
    helper = sdk / 'kapi_target_ppc.py'
    generator = sdk / 'gen_kapi.py'
    if mutation == 'empty':
        text = helper.read_text().replace('ABSENT_PROVIDER_SLOTS = IME_SLOTS', 'ABSENT_PROVIDER_SLOTS = frozenset()')
        helper.write_text(text)
    if mutation == 'zero':
        text = generator.read_text().replace('return ({ret})OS32_ERR_NOSYS;', 'return ({ret})0;')
        generator.write_text(text)
    out = tmp / 'gen'
    subprocess.run([sys.executable, '-B', str(generator), '--target', 'ppc-powermac',
                    '--out-dir', str(out), str(ROOT / 'sdk/kapi.json')], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return out


def main():
    entries = [x for x in json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
               if x['name'].startswith('ime_')]
    assert len(entries) == 17
    with tempfile.TemporaryDirectory(prefix='ppc-binding-') as directory:
        tmp = Path(directory)
        (tmp / 'normal').mkdir()
        out = generate(tmp / 'normal')
        source = (out / 'kapi/kapi_generated.c').read_text()
        if not verify(source, entries):
            print('binding verification failed'); return 1
        dirs = ['.', 'include', 'arch/ppc', 'platform/powermac', 'sdk/include',
                'sdk/include/os32', 'kernel', 'init', 'kernel/console', 'kernel/irq',
                'kernel/time', 'mm', 'lib', 'kapi', 'drivers', 'net', 'gfx', 'exec',
                'fs', 'kernel/cli']
        dirs += [str(p.relative_to(ROOT)) for base in ('drivers', 'fs', 'kernel', 'gfx')
                 for p in (ROOT / base).rglob('*') if p.is_dir()]
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        subprocess.run([str(cc), '-std=gnu11', '-ffreestanding', '-D__KERNEL_BUILD__',
                        '-D__cdecl=', '-I' + str(out / 'include')]
                       + ['-I' + str(ROOT / d) for d in dirs]
                       + ['-c', str(out / 'kapi/kapi_generated.c'), '-o', str(tmp / 'kapi.o')], check=True)
        if '--mutate' in sys.argv:
            for mutation in ('empty', 'zero'):
                path = tmp / mutation
                path.mkdir()
                mutated = generate(path, mutation)
                if verify((mutated / 'kapi/kapi_generated.c').read_text(), entries):
                    print(mutation + ': SURVIVED'); return 1
                print(mutation + ': RED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
