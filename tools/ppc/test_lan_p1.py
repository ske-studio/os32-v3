#!/usr/bin/env python3
"""Compile the LAN interface for both targets and check its contract."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'platform/powermac/lan_boot.h'
CHECK = dict(name='ppc-lan-p1', tier=1, budget=60, needs=[],
             covers=['platform/powermac/lan_boot.h'],
             inputs=['tools/ppc/test_lan_p1.py'])

MUTATIONS = [
    ('fault', 'PLN1_STATUS_FAULT = 7,', 'PLN1_STATUS_FAULT = 8,', 'PLN1_STATUS_FAULT'),
    ('header', '#define PLN1_HEADER_BYTES 80', '#define PLN1_HEADER_BYTES 81', 'PLN1_HEADER_BYTES'),
    ('again', 'LAN_AGAIN = 1,', 'LAN_AGAIN = 2,', 'LAN_AGAIN'),
    ('ethertype', '#define PLN1_ETHERTYPE 0x88b6', '#define PLN1_ETHERTYPE 0x88b5', 'PLN1_ETHERTYPE'),
]

# Values are independently copied from the design, not derived from the header.
VALUES = {
    'LAN_OK': 0, 'LAN_AGAIN': 1, 'LAN_EINVAL': -1, 'LAN_ENODEV': -2,
    'LAN_ERANGE': -3, 'LAN_ETIME': -4, 'LAN_EIO': -5,
    'LAN_EUNSUPPORTED': -6, 'LAN_EBUSY': -7,
    **dict(zip(('PLN1_STATUS_OK', 'PLN1_STATUS_UNSUPPORTED', 'PLN1_STATUS_BAD_LENGTH',
                'PLN1_STATUS_RANGE', 'PLN1_STATUS_CONFLICT', 'PLN1_STATUS_STALE',
                'PLN1_STATUS_BUSY', 'PLN1_STATUS_FAULT'), range(8))),
    **dict(zip(('PLN1_MAGIC', 'PLN1_VERSION', 'PLN1_TYPE_BEACON', 'PLN1_TYPE_LOG',
                'PLN1_TYPE_REQ', 'PLN1_TYPE_RSP', 'PLN1_HEADER_BYTES',
                'PLN1_ETHERTYPE', 'PLN1_PAYLOAD_MAX'),
               (0x504c4e31, 1, 1, 2, 3, 4, 80, 0x88b6, 256))),
    **dict(zip(('PLN1_OFF_MAGIC', 'PLN1_OFF_VERSION', 'PLN1_OFF_TYPE',
                'PLN1_OFF_HEADER_BYTES', 'PLN1_OFF_BOOT_ID', 'PLN1_OFF_SEQ',
                'PLN1_OFF_DROP_RECORDS', 'PLN1_OFF_DROP_BYTES', 'PLN1_OFF_REQUEST_ID',
                'PLN1_OFF_OPCODE', 'PLN1_OFF_STATUS', 'PLN1_OFF_PAYLOAD_BYTES',
                'PLN1_OFF_FLAGS', 'PLN1_OFF_RESERVED', 'PLN1_OFF_HOST_NONCE'),
               (0, 4, 5, 6, 8, 24, 32, 40, 48, 52, 54, 56, 58, 60, 64))),
    **dict(zip(('PLN1_OP_STATUS', 'PLN1_OP_CLOCK', 'PLN1_OP_MEMORY',
                'PLN1_OP_READ_DIAG', 'PLN1_OP_PEEK', 'PLN1_REQ_STATUS_BYTES',
                'PLN1_REQ_CLOCK_BYTES', 'PLN1_REQ_MEMORY_BYTES',
                'PLN1_REQ_READ_DIAG_BYTES', 'PLN1_REQ_PEEK_BYTES'),
               (1, 2, 3, 4, 5, 0, 0, 0, 4, 8))),
}
PPC_FLAGS = '''-std=gnu11 -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec
-msdata=none -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon
-fsigned-char -fno-short-enums -fno-builtin -fno-asynchronous-unwind-tables -O2
-Wall -Wextra -Werror -Werror=vla -Werror=implicit-int
-Werror=implicit-function-declaration -ffunction-sections -fdata-sections
-D__cdecl= -D__KERNEL_BUILD__ -DKBOOT_NEGATIVE=0 -MMD -MP'''.split()


def command(args):
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    checks = [f'if ({k} != {v}) return {i};' for i, (k, v) in enumerate(VALUES.items(), 1)]
    checks += ['if (sizeof(struct pm_pci_window) != 16) return 100;',
               'if (sizeof(struct pm_pci) != 20) return 101;',
               'if (__builtin_offsetof(struct pm_gmac_boot, win) != 76) return 102;',
               'if (sizeof(struct pm_gmac_boot) != 220) return 103;',
               'if (__builtin_offsetof(struct lan_message, payload) != 69) return 104;',
               'if (sizeof(struct lan_message) != 328) return 105;']
    source = '#include "lan_boot.h"\nint check(void) {\n' + '\n'.join(checks) + '\nreturn 0; }\n'
    source += 'void _start(void) { int r = check(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r)); __builtin_unreachable(); }\n'
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'test.c').write_text(source)
        (d / 'lan_boot.h').write_text(HEADER.read_text())
        include = ['-I' + str(d), '-Iinclude']
        ppc = '/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc'
        # The runtime entry uses x86 assembly, so compile only check() for PPC.
        (d / 'ppc.c').write_text(source.split('void _start')[0])
        result = command([ppc, *PPC_FLAGS, *include, '-c', str(d / 'ppc.c'), '-o', str(d / 'ppc.o')])
        if result.returncode:
            raise RuntimeError(result.stderr)
        host = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                '-nostdlib', '-static', '-Wall', '-Wextra', '-Werror', *include,
                str(d / 'test.c'), '-o', str(d / 'test')]
        result = command(host)
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = command(['qemu-i386', str(d / 'test')])
        if result.returncode:
            raise RuntimeError(f'host contract failed: {result.returncode}')
        print('PPC and host contract: OK', flush=True)
        if args.mutate:
            original = HEADER.read_text()
            for name, old, new, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor is not unique')
                (d / 'lan_boot.h').write_text(original.replace(old, new))
                print(f'reached={name}', flush=True)
                result = command(host)
                expected = f'static assertion failed: "{diagnostic}"'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError(f'{name}: unexpected compiler result: {result.stderr}')
                print(f'RED {name}: {expected}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
