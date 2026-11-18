#!/usr/bin/env python3
"""Exercise PowerMac rtc_read on Cuda and PMU via link-sh RSH."""
import argparse
from datetime import datetime, timedelta
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

BASE = datetime(2024, 2, 29)
CASES = ('normal', 'year', 'swap-opcodes', 'rtc-absent')
MACHINES = {'cuda': 'mac99', 'pmu': 'mac99,via=pmu'}


def observe(elf, machine, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, '7450', evidence, 240)
    session.machine = MACHINES[machine]
    # MacIO advances its counter with QEMU virtual time. Without icount or
    # pauses, bracket the moment of the RTC read in elapsed monotonic time.
    session.qemu_clock = ['-rtc', 'base=' + BASE.isoformat() + ',clock=vm']
    try:
        session.start()
        session.wait(session.ready, 'RTC cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'RTC RSH mode')
        before = time.monotonic() - session.qemu_started
        body = rsh_command(session, '/bin/rtcprob.bin')
        after = time.monotonic() - session.qemu_started
        status = rsh_command(session, 'echo $?')
        match = re.fullmatch(rb'RTC ' + rb' '.join([rb'(\d{1,3})'] * 7) + rb'\n+', body)
        require(match is not None and status == b'0\n', repr((case, machine, body, status)))
        fields = tuple(map(int, match.groups()))

        # Use a Python calendar independent of the machine backend. Iterate
        # over candidate times so leap-day year mutations remain observable.
        first = int(before) - 2
        last = int(after) + 2
        offset = 1 if case == 'year' else 0
        candidates = set()
        for delta in range(first, last + 1):
            date = BASE + timedelta(seconds=delta)
            candidates.add(((date.year + offset) % 100, date.month, date.day,
                            (date.weekday() + 1) % 7, date.hour, date.minute, date.second))
        if case == 'rtc-absent':
            require(fields == (255,) * 7, repr((case, machine, fields)))
        elif case == 'swap-opcodes':
            require(fields == (0,) * 7, repr((case, machine, fields)))
        else:
            require(fields in candidates, repr((case, machine, fields, first, last)))

        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'RTC QEMU exit')
        print(f'{"GREEN" if case == "normal" else "RED"} {case} {machine} {fields}', flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()

    from kboot_accept import build, mutant_settings
    from ppc_shm import guest_available
    from test_kapi_binding import generate

    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file():
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3
    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-rtc-', dir=os.environ.get('TMPDIR'))).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', 'rtcprob')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    digests = set()
    for case in CASES if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        settings = []
        if case == 'year':
            settings = mutant_settings(folder, case, 'platform/powermac/system.c',
                                       'date->year = year;', 'date->year = year + 1;')
        elif case == 'swap-opcodes':
            settings = mutant_settings(folder, case, 'drivers/macintosh/via.c',
                                       'PMU_READ_RTC = 0x38, CUDA_GET_TIME = 0x03',
                                       'PMU_READ_RTC = 0x03, CUDA_GET_TIME = 0x38')
        elif case == 'rtc-absent':
            kernel.mkdir()
            gen = generate(kernel, case)
            (gen / 'stamp').touch()
        elf = build(kernel, *options, *settings, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in digests:
            raise RuntimeError(f'{case} ELF is identical to a previous image: {elf}')
        digests.add(digest)
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for machine in MACHINES:
                observe(elf, machine, folder / machine, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
