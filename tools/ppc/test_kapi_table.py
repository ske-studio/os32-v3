#!/usr/bin/env python3
"""Verify the PowerMac KAPI table and its generated initializer."""
CHECK = dict(name='ppc-kapi-table', tier=1,
             covers=['arch/ppc/exec_kapi.c', 'platform/powermac/kapi_table.h',
                     'tools/ppc/test_kapi_table.py'],
             inputs=['sdk/gen_kapi.py', 'sdk/kapi.json', 'exec/exec.c'], needs=[], budget=10)
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from test_kapi_binding import ROOT, generate


def verify_init(text, data):
    actual = re.findall(r'kapi->(\w+)(?:\[_kr\])?\s*=', text)
    expected = ['magic', 'version'] + [a['name'] for a in data['api']]
    expected += ['kapi_reserved'] + [f['name'] for f in data['data_fields']]
    return (actual == expected and
            'for (_kr = 0; _kr < (u32)KAPI_FUNC_RESERVED; _kr++)' in text and
            'kapi->kapi_reserved[_kr] = kapi_reserved_nosys;' in text and
            all(('kapi->' + a['name'] + ' = ' +
                 (('(void *)' + a.get('target', a['name'])) if a.get('direct')
                  else ('wrap_' + a['name'])) + ';') in text for a in data['api']))


def table_ok(nm, obj):
    line = next(x for x in subprocess.check_output([str(nm), '-S', str(obj)], text=True).splitlines()
                if x.endswith(' ppc_kapi_table'))
    address, size, kind, _ = line.split()
    sections = subprocess.check_output([str(nm.with_name('powerpc-elf-objdump')), '-h', str(obj)], text=True)
    return kind in 'BbDd' and int(size, 16) >= 0x4c0 and int(address, 16) % 4096 == 0 and bool(re.search(r'\.bss\s+\S+.*\n\s*\S+.*', sections)) and '2**12' in sections


def main():
    data = json.loads((ROOT / 'sdk/kapi.json').read_text())
    with tempfile.TemporaryDirectory(prefix='ppc-table-') as directory:
        tmp = Path(directory)
        out = generate(tmp)
        init = (out / 'exec/exec_kapi_init.inc').read_text()
        assert verify_init(init, data)
        dirs = ['.', 'include', 'arch/ppc', 'platform/powermac', 'sdk/include', 'sdk/include/os32', 'kernel', 'init', 'kernel/console', 'kernel/irq', 'kernel/time', 'mm', 'lib', 'kapi', 'drivers', 'net', 'gfx', 'exec', 'fs', 'kernel/cli']
        dirs += [str(p.relative_to(ROOT)) for base in ('drivers', 'fs', 'kernel', 'gfx') for p in (ROOT / base).rglob('*') if p.is_dir()]
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        flags = ['-std=gnu11', '-ffreestanding', '-D__KERNEL_BUILD__', '-D__cdecl=', '-I' + str(out / 'include'), '-I' + str(out / 'sdk/include/os32')] + ['-I' + str(ROOT / d) for d in dirs]
        exec_source = out / 'exec/exec.c'
        exec_source.write_bytes((ROOT / 'exec/exec.c').read_bytes())
        for name, source in [('exec', exec_source), ('kapi', out / 'kapi/kapi_generated.c'), ('table', ROOT / 'arch/ppc/exec_kapi.c')]:
            subprocess.run([str(cc)] + flags + ['-c', str(source), '-o', str(tmp / (name + '.o'))], check=True)
        nm = cc.with_name('powerpc-elf-nm')
        undefined = set(re.findall(r'\b(wrap_\w+)$', subprocess.check_output([str(nm), '-u', str(tmp / 'exec.o')], text=True), re.M))
        defined = set(re.findall(r'\b(wrap_\w+)$', subprocess.check_output([str(nm), '--defined-only', str(tmp / 'kapi.o')], text=True), re.M))
        assert undefined and undefined <= defined
        assert table_ok(nm, tmp / 'table.o')
        print('slots:', len(data['api']), 'wrappers resolved:', len(undefined))
        if '--mutate' in sys.argv:
            assert not verify_init(init.replace('    kapi->' + data['api'][0]['name'] + ' = wrap_' + data['api'][0]['name'] + ';\n', '', 1), data)
            print('missing assignment: RED')
            source = (ROOT / 'arch/ppc/exec_kapi.c').read_text().replace('KernelAPI ppc_kapi_table __attribute__', 'const KernelAPI ppc_kapi_table __attribute__')
            mutant = tmp / 'mutant.c'
            mutant.write_text(source)
            (tmp / 'kapi_table.h').write_text((ROOT / 'platform/powermac/kapi_table.h').read_text().replace('extern KernelAPI', 'extern const KernelAPI'))
            subprocess.run([str(cc)] + flags + ['-c', str(mutant), '-o', str(tmp / 'mutant.o')], check=True)
            assert not table_ok(nm, tmp / 'mutant.o')
            print('const table: RED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
