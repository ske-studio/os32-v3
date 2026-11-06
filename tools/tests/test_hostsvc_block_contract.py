#!/usr/bin/env python3
"""D1 block wire contract, NET_HOSTSVC_DESIGN §4; libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-block-contract', tier=1, needs=[], budget=3, covers=[
    'fs/serialfs/sfs_proto.h', 'fs/serialfs/sfs_proto.c', 'lib/crc32.c',
    'lib/crc32.h', 'lib/le_internal.h', 'include/types.h',
    'include/time_units.h', 'include/memmap.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk'])

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
SOURCE = r'''
#include "fs/serialfs/sfs_proto.h"
static int test(void) {
    /* Fixed bytes come from §4, independently of the product offsets. */
    const u8 expected[] = {0x78,0x56,0x34,0x12, 0xef,0xcd,0xab,0x89, 2,0};
    u8 req[SFS_BREAD_REQ_LEN] = {0};
    u8 payload[SFS_BREAD_RSP_MAX] = {0};
    u8 wire[SFS_BUFFER_FRAME];
    SfsDec dec;
    SfsFrame frame;
    const u8 ops[] = {SFS_T_HELLO, SFS_T_BYE, SFS_T_STAT, SFS_T_LIST,
        SFS_T_READ, SFS_T_WRITE, SFS_T_MKDIR, SFS_T_RMDIR, SFS_T_UNLINK,
        SFS_T_RENAME, SFS_T_LOG, SFS_T_EXIT, SFS_T_DUMP,
        SFS_T_BOPEN, SFS_T_BREAD, SFS_T_BCLOSE, SFS_T_ERR};
    for (u32 i = 0; i < sizeof(ops); i++)
        for (u32 j = i + 1; j < sizeof(ops); j++)
            if (ops[i] == ops[j]) return 1;
    if (SFS_T_BOPEN != 0x30 || SFS_T_BREAD != 0x31 ||
        SFS_T_BCLOSE != 0x32 || SFS_T_RESP != 0x80) return 2;
    if (SFS_BLOCK_ALIAS_MIN != 1 || SFS_BLOCK_ALIAS_MAX != 63 ||
        SFS_BREAD_COUNT_MIN != 1 || SFS_BREAD_COUNT_MAX != 2) return 3;
    if (SFS_BREAD_REQ_LEN != 10 || SFS_BREAD_RSP_MAX != 1028 ||
        SFS_BREAD_DATA_OFF != 4 || SFS_LOGICAL_BLOCK_SIZE != 512 ||
        SFS_STATUS_LEN != 4) return 4;
    sfs_put32(req + SFS_BREAD_HANDLE_OFF, 0x12345678);
    sfs_put32(req + SFS_BREAD_LBA_OFF, 0x89abcdef);
    sfs_put16(req + SFS_BREAD_COUNT_OFF, SFS_BREAD_COUNT_MAX);
    for (u32 i = 0; i < sizeof(expected); i++)
        if (req[i] != expected[i]) return 5;
    if (SFS_BOPEN_STATUS_OFF != 0 || SFS_BOPEN_HANDLE_OFF != 4 ||
        SFS_BOPEN_SIZE_LO_OFF != 8 || SFS_BOPEN_SIZE_HI_OFF != 12 ||
        SFS_BOPEN_MTIME_LO_OFF != 16 || SFS_BOPEN_MTIME_HI_OFF != 20 ||
        SFS_BOPEN_RSP_LEN != 24 || SFS_BCLOSE_HANDLE_OFF != 0 ||
        SFS_BCLOSE_REQ_LEN != 4 || SFS_BCLOSE_RSP_LEN != 4) return 6;
    for (u32 i = 4; i < sizeof(payload); i++) payload[i] = (u8)i;
    if (SFS_MAX_PAYLOAD != 512 || SFS_LAN_MAX_PAYLOAD != 1028) return 7;
    if (sfs_encode(wire, SFS_T_BREAD | SFS_T_RESP, 7, 9,
                   payload, sizeof(payload)) != 0) return 8;
    u16 len = sfs_encode_limit(wire, SFS_T_BREAD | SFS_T_RESP, 7, 9,
                              payload, sizeof(payload), SFS_LAN_MAX_PAYLOAD);
    if (len != 1044 || wire[3] != 0xb1 || wire[10] != 4 || wire[11] != 4)
        return 9;
    sfs_dec_reset(&dec);
    dec.max_payload = SFS_LAN_MAX_PAYLOAD;
    for (u16 i = 0; i < len; i++) {
        int rc = sfs_dec_feed(&dec, wire[i], &frame);
        if (rc != (i == len - 1)) return 10;
    }
    if (frame.type != 0xb1 || frame.sid != 7 || frame.seq != 9 ||
        frame.len != 1028 || sfs_get32(frame.payload) != 0) return 11;
    for (u32 i = 4; i < frame.len; i++)
        if (frame.payload[i] != (u8)i) return 12;
    return 0;
}
void _start(void) {
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''


def build_run(work):
    src = work / 'block.c'
    src.write_text(SOURCE)
    exe = work / 'block'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    str(src), str(ROOT / 'fs/serialfs/sfs_proto.c'),
                    str(ROOT / 'lib/crc32.c'), '-o', str(exe)], check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='hostsvc-block-contract-') as tmp:
        work = Path(tmp)
        rc = build_run(work)
        if rc:
            raise RuntimeError(f'control failed: rc={rc}')
        print('GREEN D1 wire/layout/op collisions/LAN round trip; runner=' +
              host32.selected_runner(), flush=True)
        if args.mutate:
            original = (ROOT / 'fs/serialfs/sfs_proto.h').read_text()
            anchor = 'SFS_BREAD_COUNT_MAX          2'
            if original.count(anchor) != 1:
                raise RuntimeError('mutation anchor is not unique')
            header = work / 'fs/serialfs/sfs_proto.h'
            header.parent.mkdir(parents=True)
            header.write_text(original.replace(anchor, 'SFS_BREAD_COUNT_MAX          1'))
            rc = build_run(work)
            if rc <= 0:
                raise RuntimeError(f'expected runtime RED: rc={rc}')
            print(f'RED block-count: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_block_contract.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
