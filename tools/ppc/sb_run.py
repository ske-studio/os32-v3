#!/usr/bin/env python3
"""Check repeated USER exit and fault recovery through the resident RSH."""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile

from kboot_accept import build, mutant_settings
from kboot_run import require, serial, validate_root
from link_sh_run import Session
from usb_run import rsh_command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--cpu', choices=('both', '7450', '7455'), default='both')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='sb-run-', dir=os.environ['TMPDIR']))
    user = work/'user'
    bins = ' '.join(str(user/(name+'.bin')) for name in ('hello', 'fault'))
    cases = [('normal',)]
    if args.mutate:
        cases.extend([
            ('exit-status', 'arch/ppc/kapi_call.c',
             'return ppc_kapi_invoke(&api, current_slot, values);',
             'struct kapi_values changed = *values;\n'
             '    if (current_slot == (__builtin_offsetof(KernelAPI, sys_exit) - 8) / 4) changed.value[0] = 0;\n'
             '    return ppc_kapi_invoke(&api, current_slot, &changed);',
            ),
            ('fault-exit', 'arch/ppc/kernel_trap.c',
             '        ppc_user_fault_kill();',
             '        extern void kapi_sys_exit(int);\n        kapi_sys_exit(0);',
            )

        ])
    hashes = set()
    for name, *change in cases:
        opts = mutant_settings(work, name, *change) if change else []
        elf = build(work/name, 'IMAGE=kboot-link-sh', 'KB_SH_OUT='+str(user),
                    'KB_ROOT_BINS='+bins, *opts, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        require(digest not in hashes, 'distinct '+name)
        hashes.add(digest)
        print(name, digest, flush=True)
        if args.build_only:
            continue
        for cpu in ('7450', '7455') if args.cpu == 'both' else (args.cpu,):
            s = Session(elf, cpu, work/(name+'-'+cpu), 180)
            try:
                s.start()
                s.wait(lambda: validate_root(serial(s.out/'serial-a.log'), serial(s.out/'serial-b.log')),
                       'rd0 registration and page accounting', 60)
                for child in ('hello', 'hello', 'fault', 'hello'):
                    if child == 'hello':
                        status = 0 if name == 'exit-status' else 7
                        rsh_command(s, '/bin/hello.bin ok', b'hello ok\n' + (b'\n' if status == 0 else b''))
                    else:
                        status = 0 if name == 'fault-exit' else 139
                        rsh_command(s, '/bin/fault.bin')
                    rsh_command(s, 'echo $?', f'{status}\n'.encode())
                rsh_command(s, 'echo alive', b'alive\n')
            finally:
                s.close()
    print(f'{len(hashes)} distinct exit/fault images')


if __name__ == '__main__':
    main()
