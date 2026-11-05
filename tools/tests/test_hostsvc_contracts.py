#!/usr/bin/env python3
"""Stage 0 contracts, NET_HOSTSVC_DESIGN §§2–5 (no hardware or libc).

Expected wire numbers below are transcribed from the design, independently of
product arithmetic. Compile headers separately with host/cross compilers, then
execute ILP32 layout checks. Mutations must compile and fail at runtime.
"""
CHECK = dict(name='hostsvc-contracts', tier=1, needs=[],
    covers=['net/nic.h', 'net/link.h', 'fs/serialfs/sfs_proto.h',
            'fs/serialfs/serialfs.h', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py', 'build/config.mk'], budget=3)

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HEADERS = ('net/nic.h', 'net/link.h', 'fs/serialfs/sfs_proto.h',
           'fs/serialfs/serialfs.h')
# §5: original 20-byte header, appended HELLO fields, independent notification.
WIRE = {
    'LINK_OP_NOTIFY': 11, 'LINK_HDR_LEN': 20, 'LINK_OFF_OP': 0,
    'LINK_OFF_FLAGS': 1, 'LINK_OFF_EPOCH': 2, 'LINK_OFF_SEQ': 4,
    'LINK_OFF_ACK': 8, 'LINK_OFF_LENGTH': 12, 'LINK_OFF_RID': 14,
    'LINK_OFF_SESS': 18, 'LINK_HELLO_SA_LEN': 6, 'LINK_HELLO_ES_LEN': 2,
    'LINK_HELLO_REQ_SESS_OFF': 2, 'LINK_HELLO_REQ_EPOCH_OFF': 4,
    'LINK_HELLO_SA_EXT_LEN': 12, 'LINK_HELLO_ES_EXT_LEN': 8,
    'LINK_HELLO_SA_HOST_GEN_OFF': 6, 'LINK_HELLO_SA_ALIVE_OFF': 10,
    'LINK_HELLO_ES_HOST_GEN_OFF': 2, 'LINK_HELLO_ES_ALIVE_OFF': 6,
    'LINK_NOTIFY_RID': 0, 'LINK_NOTIFY_HOST_GEN_OFF': 0,
    'LINK_NOTIFY_SERVICE_OFF': 4, 'LINK_NOTIFY_KIND_OFF': 6,
    'LINK_NOTIFY_LENGTH_OFF': 8, 'LINK_NOTIFY_RESERVED_OFF': 10,
    'LINK_NOTIFY_DATA_OFF': 12, 'LINK_NOTIFY_DATA_MAX': 236,
    'LINK_NOTIFY_MAX_LEN': 248, 'LINK_NOTIFY_ALIVE_KIND': 1,
    'LINK_NOTIFY_ALIVE_LEN': 4,
    # §4: existing serial limit is preserved; only LAN can carry 2 sectors.
    'SFS_MAX_PAYLOAD': 512, 'SFS_LAN_MAX_PAYLOAD': 1028,
    'SFS_T_BOPEN': 0x30, 'SFS_T_BREAD': 0x31, 'SFS_T_BCLOSE': 0x32,
    'SFS_T_RESP': 0x80, 'SFS_STATUS_LEN': 4, 'SFS_LOGICAL_BLOCK_SIZE': 512,
    'SFS_BLOCK_ALIAS_MIN': 1, 'SFS_BLOCK_ALIAS_MAX': 63,
    'SFS_BOPEN_STATUS_OFF': 0, 'SFS_BOPEN_HANDLE_OFF': 4,
    'SFS_BOPEN_SIZE_LO_OFF': 8, 'SFS_BOPEN_SIZE_HI_OFF': 12,
    'SFS_BOPEN_MTIME_LO_OFF': 16, 'SFS_BOPEN_MTIME_HI_OFF': 20,
    'SFS_BOPEN_RSP_LEN': 24, 'SFS_BREAD_HANDLE_OFF': 0,
    'SFS_BREAD_LBA_OFF': 4, 'SFS_BREAD_COUNT_OFF': 8,
    'SFS_BREAD_REQ_LEN': 10, 'SFS_BREAD_COUNT_MIN': 1,
    'SFS_BREAD_COUNT_MAX': 2, 'SFS_BREAD_DATA_OFF': 4,
    'SFS_BREAD_RSP_MAX': 1028, 'SFS_BCLOSE_HANDLE_OFF': 0,
    'SFS_BCLOSE_REQ_LEN': 4, 'SFS_BCLOSE_RSP_LEN': 4,
}
MUTATIONS = (
    ('notify-size', 'net/link.h', 'LINK_NOTIFY_DATA_MAX        236',
     'LINK_NOTIFY_DATA_MAX        237'),
    ('notify-order', 'net/link.h', 'LINK_NOTIFY_SERVICE_OFF     4',
     'LINK_NOTIFY_SERVICE_OFF     5'),
    ('hello-size', 'net/link.h', 'LINK_HELLO_SA_EXT_LEN 12',
     'LINK_HELLO_SA_EXT_LEN 13'),
    ('block-order', 'fs/serialfs/sfs_proto.h', 'SFS_BREAD_LBA_OFF            4',
     'SFS_BREAD_LBA_OFF            5'),
    ('block-size', 'fs/serialfs/sfs_proto.h', 'SFS_BOPEN_RSP_LEN            24',
     'SFS_BOPEN_RSP_LEN            25'),
    ('nic-order', 'net/nic.h',
     '    void (*mac)(u8 *);\n    void (*poll)(void);',
     '    void (*poll)(void);\n    void (*mac)(u8 *);'),
    ('persistent-order', 'fs/serialfs/serialfs.h',
     'u32 generation, last_use, alive_seen;',
     'u32 last_use, generation, alive_seen;'),
)
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def source():
    checks = [(key, str(value)) for key, value in WIRE.items()]
    checks += [('sizeof(struct NetNicOps)', '20'),
               ('sizeof(struct SfsPersistent)', 'sizeof(SfsClient) + 12'),
               ('OFF(struct SfsPersistent, client)', '0')]
    checks += [(f'OFF(struct NetNicOps, {field})', str(i * 4))
               for i, field in enumerate(('tx', 'rx', 'mac', 'poll',
                                         'rx_credit_bytes'))]
    checks += [(f'OFF(struct SfsPersistent, {field})',
                f'sizeof(SfsClient) + {i * 4}')
               for i, field in enumerate(('generation', 'last_use', 'alive_seen'))]
    # Compile-time checks concern types only; all layout checks execute below.
    signatures = {
        'net_bind': 'int (*)(const struct NetNicOps *)',
        'net_rx_take': 'int (*)(u16, void *, u32, u32 *)',
        'sfs_ensure_session': 'int (*)(u32)',
        'sfs_request_deadline': 'int (*)(u8, const void *, u16, void *, u32)',
        'sfs_alive_observe': 'void (*)(u32)',
    }
    text = ''.join(f'#include "{header}"\n' for header in HEADERS)
    text += '#define OFF(t, m) __builtin_offsetof(t, m)\n'
    for name, signature in signatures.items():
        text += (f'_Static_assert(__builtin_types_compatible_p(__typeof__(&{name}), '
                 f'{signature}), "{name}");\n')
    text += '''static int equal(u32 a, u32 b) {
    __asm__ volatile ("" : "+r"(a), "+r"(b));
    return a == b;
}
static int test(void) {
'''
    for i, (actual, expected) in enumerate(checks, 1):
        text += f'    if (!equal({actual}, {expected})) return {i};\n'
    text += '''    return 0;
}
void _start(void) {
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''
    return text, checks


def compile_and_run(work, tree):
    src = work / 'contracts.c'
    body, checks = source()
    src.write_text(body)
    inc = ['-I' + str(tree), *include_flags(ROOT)]
    exe = work / 'contracts'
    subprocess.run(['gcc', *FLAGS, *inc, '-nostdlib', '-static', '-no-pie',
                    str(src), '-o', str(exe)], check=True, cwd=ROOT)
    result = host32.run([str(exe)], cwd=ROOT)
    if result.returncode > 0:
        index = result.returncode - 1
        print('layout mismatch:', checks[index] if index < len(checks) else index)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
    with tempfile.TemporaryDirectory(prefix='hostsvc-contracts-') as tmp:
        work = Path(tmp)
        for compiler in ('gcc', str(cross / 'bin/i386-elf-gcc')):
            for header in HEADERS:
                src = work / 'standalone.c'
                src.write_text(f'#include "{header}"\n')
                subprocess.run([compiler, *FLAGS, '-I' + str(ROOT),
                                *include_flags(ROOT), '-c', str(src),
                                '-o', str(work / 'standalone.o')], check=True, cwd=ROOT)
            # Cross also compiles the complete fixture and signature assertions.
            src = work / 'types.c'
            src.write_text(source()[0])
            subprocess.run([compiler, *FLAGS, '-I' + str(ROOT),
                            *include_flags(ROOT), '-c', str(src),
                            '-o', str(work / 'types.o')], check=True, cwd=ROOT)
            print(f'PASS standalone headers and signatures ({compiler})', flush=True)
        if compile_and_run(work, ROOT) != 0:
            raise RuntimeError('normal control failed')
        print('PASS wire values and layouts (ILP32, runner=' + host32.selected_runner() + ')',
              flush=True)
        if args.mutate:
            for name, rel, old, new in MUTATIONS:
                tree = work / name
                # Copy headers so relative #includes remain inside the overlay.
                shutil.copytree(ROOT / 'fs/serialfs', tree / 'fs/serialfs',
                                ignore=shutil.ignore_patterns('*.c'))
                shutil.copy2(ROOT / 'fs/vfs.h', tree / 'fs/vfs.h')
                (tree / 'net').mkdir()
                for header in ('net/nic.h', 'net/link.h'):
                    shutil.copy2(ROOT / header, tree / header)
                path = tree / rel
                original = path.read_text()
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: mutation anchor is not unique')
                path.write_text(original.replace(old, new))
                rc = compile_and_run(work, tree)
                if rc <= 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx) -> int:
    for index, runner in enumerate(ctx.runners):
        env = dict(os.environ, HOST32_RUNNERS=runner)
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_contracts.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=env)
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
