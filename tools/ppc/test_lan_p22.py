#!/usr/bin/env python3
"""Exercise the monitor and its fault wrapper with a freestanding host fixture."""
import pathlib
import subprocess
import sys
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32
CHECK = {'id': 'ppc-lan-p22', 'covers': ['platform/powermac/lan_monitor.c'],
         'inputs': ['tools/ppc/lan_p22_fixture.c', 'platform/powermac/lan_wire.c',
                    'platform/powermac/lan_boot.h', 'arch/ppc/kernel_probe.h',
                    'arch/ppc/ppc_trap.h', 'arch/ppc/ppc_oea.h']}
SOURCE = ROOT / 'platform/powermac/lan_monitor.c'
MUTATIONS = [
 ('H:A10 read-range', 'if (!range) goto bad_range;', 'if (!range) range = &d->peek_ok[0];', 12),
 ('physical-range', '!contains(range, pa, 1)', '0', 17),
 ('fault', 'r->status = PLN1_STATUS_FAULT; return;', 'r->payload_bytes = (u16)i; r->status = PLN1_STATUS_FAULT; return;', 18),
 ('conflict', 'if (same_request(q, &cache[i].req))', 'if (1 || same_request(q, &cache[i].req))', 27),
 ('nonce', '!equal(q->host_nonce, session_nonce, 16)', '0', 29),
 ('length', 'if (q->payload_bytes != n)', 'if (0 && q->payload_bytes != n)', 22),
 ('clear', 'ppc_trap_onfault_clear();', '(void)0;', 31),
]

def exercise(source, temp):
    path = pathlib.Path(temp); (path / 'monitor.c').write_text(source)
    binary = path / 'fixture'
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-m32','-ffreestanding','-fno-pie','-no-pie','-nostdlib','-static','-fno-builtin','-std=gnu11','-I',str(ROOT),'-I',str(ROOT/'include'),'-I',str(ROOT/'platform/powermac'),str(path/'monitor.c'),str(ROOT/'platform/powermac/lan_wire.c'),str(ROOT/'tools/ppc/lan_p22_fixture.c'),'-o',str(binary)],check=True,timeout=60)
    return subprocess.run(host32.command([str(binary)]),capture_output=True,text=True,timeout=60)
def main():
    source=SOURCE.read_text()
    with tempfile.TemporaryDirectory() as temp:
        result=exercise(source,temp)
        if result.returncode != 0 or result.stdout != 'REACHED\nreached=A10\nPASS\n': raise RuntimeError(f'baseline: {result.returncode} {result.stdout} {result.stderr}')
        print('baseline PASS')
        if '--mutate' in sys.argv:
            for name,old,new,expected in MUTATIONS:
                if source.count(old)!=1: raise RuntimeError(f'{name}: anchor count {source.count(old)}')
                result=exercise(source.replace(old,new),temp)
                marker = "reached=A10\n" if expected not in (22, 34) else ""
                diagnostic = "H:A10 read-range\n" if expected == 12 else ""
                if result.returncode != 1 or result.stdout != f'REACHED\n{marker}{diagnostic}FAIL {expected:02d}\n': raise RuntimeError(f'{name}: unexpected {result.returncode} {result.stdout} {result.stderr}')
                print(f'{name} RED (rc=1, REACHED, FAIL {expected:02d})')
if __name__ == '__main__': main()
