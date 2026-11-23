"""PowerMac HW_OP §5 rows 5c/6a. Compare real PPC instruction labels and
freestanding host window transactions against the independent width/VA contract."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def host(work, source, expected):
    path = work / 'window.c'
    path.write_text(source)
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-nostdlib', '-nostdinc', '-ffreestanding',
                    '-static', '-fno-pie', '-no-pie', '-O2', '-Wall', '-Werror',
                    '-I.', '-Iinclude', '-Iarch/ppc', '-Iplatform/powermac',
                    path, 'tools/ppc/hwb_window_host.c', '-o', exe], cwd=ROOT, check=True)
    rc = subprocess.run(['qemu-i386', exe]).returncode
    assert rc == expected, (rc, expected)


def labels(work):
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin'
    obj = work / 'transfer.o'
    subprocess.run([cross / 'powerpc-elf-gcc', '-c', 'arch/ppc/hwb_transfer.S', '-o', obj],
                   cwd=ROOT, check=True)
    dis = subprocess.check_output([cross / 'powerpc-elf-objdump', '-d', obj], text=True)
    for name, ops in [('read', ('lbz','lhz','lwz','lhbrx','lwbrx')),
                      ('write', ('stb','sth','stw','sthbrx','stwbrx'))]:
        for suffix, op in zip(('8','16','32','16le','32le'), ops):
            base = 'ppc_hwb_' + name + suffix
            assert re.search(fr'<{base}_insn>:\n[^\n]*\s{op}\s', dis), base
            assert re.search(fr'<{base}_resume>:\n[^\n]*eieio\n[^\n]*sync', dis), base


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/hwb_window.c').read_text()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        exe = work / 'paging'
        subprocess.run(['gcc', '-m32', '-nostdinc', '-nostdlib', '-ffreestanding',
                        '-static', '-fno-pie', '-no-pie', '-O2', '-Wall', '-Werror',
                        '-I.', '-Iinclude', '-Iarch/ppc', '-Iplatform/powermac',
                        'arch/ppc/pd_walk.c', 'tools/ppc/hwb_paging_host.c', '-o', exe],
                       cwd=ROOT, check=True)
        subprocess.run(['qemu-i386', exe], check=True)
        labels(work)
        host(work, source, 0)
        if args.mutate:
            anchor = 'if (!install(va, raw)) goto cleanup;'
            assert source.count(anchor) == 1
            early = 'if (pages == 2 && used == 1 && store) {\n' \
                    'const struct transfer *t = &transfers[1][width >> 1][!!little];\n' \
                    'u32 first = va + offset;\n' \
                    'if (ppc_trap_onfault_transfer((u32)t->pc,first,first,width,1,(u32)t->resume)) {\n' \
                    't->run(first,operand,1,0); ppc_trap_onfault_clear(); }}'
            lines = (ROOT / 'tools/ppc/hwb_window_host.c').read_text().splitlines()
            expected, = (i for i,line in enumerate(lines,1) if 'REQUIRE(!calls && !stores' in line)
            host(work, source.replace(anchor, anchor + '\n' + early), expected)
            print('RED store before second map')
    print('PPC transfer labels and host window transactions PASS')


if __name__ == '__main__':
    main()
