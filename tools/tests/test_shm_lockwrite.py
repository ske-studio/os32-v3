"""E11-13 guest probe wiring: both terminal pages, CPL3 and armed fault marker."""
import argparse
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

def check(s):
    assert 'strcmp(argv[1], "lockwrite") == 0' in s, 'lockwrite mode'
    assert 'strcmp(argv[2], "first")' in s and 'strcmp(argv[2], "last")' in s, 'both pages'
    assert 'BLOCK_BYTES - PAGE_BYTES' in s, 'last page'
    assert '(cs & 3) != 3' in s, 'CPL3 required'
    start = s.index('    if (lockwrite) {', s.index('if ((cs & 3)'))
    body = s[start:s.index('    report = (volatile u32 *)strtoul', start)]
    assert body.index('sys_shm_lock') < body.index('r3_arm(') < body.index('*target = WRITE_PATTERN;'), 'lock before CPL3 write'
    assert 'report[1] = R3_SURV;' in body and 'return 1;' in body, 'survival fails'

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', default='qemu')
p.add_argument('--mutate', action='store_true')
a = p.parse_args()
s = (ROOT/'userland/tests/shm_reuse_child.c').read_text()
check(s)
print('PASS lockwrite guest wiring')
if a.mutate:
    for old, new in [('strcmp(argv[1], "lockwrite") == 0', '0'),
                     ('BLOCK_BYTES - PAGE_BYTES', '0'),
                     ('*target = WRITE_PATTERN;', '(void)target;'),
                     ('sys_shm_lock', 'sys_shm_free'),
                     ('report[1] = R3_SURV;', 'report[1] = 0;')]:
        try: check(s.replace(old, new))
        except (AssertionError, ValueError): print('RED', old)
        else: raise AssertionError('surviving mutant: '+old)
