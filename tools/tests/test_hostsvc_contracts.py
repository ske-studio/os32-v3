#!/usr/bin/env python3
"""Stage 0 + declaration stage 2 contracts, NET_HOSTSVC_DESIGN §§2–5 (no hardware or libc).

Expected wire numbers below are transcribed from the design, independently of
product arithmetic. Compile headers separately with host/cross compilers, then
execute ILP32 layout checks. Mutations must compile and fail at runtime.
"""
INITIALIZER_UNITS = ('fs/ext2/ext2_vfs.c', 'fs/fat/fatfs_vfs.c',
                     'fs/hostdrv/hostdrvfs.c', 'fs/isofs/iso9660.c',
                     'fs/serialfs/serialfs.c', 'drivers/base/dev.c',
                     'drivers/ata/cd_body.c', 'drivers/net/e82557/lan_body.c')

CHECK = dict(name='hostsvc-contracts', tier=1, needs=[],
    covers=['net/nic.h', 'net/link.h', 'fs/serialfs/sfs_proto.h',
            'fs/serialfs/serialfs.h', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py', 'build/config.mk',
            'net/nic_rx.h',
            'net/link_notify.h',
            'net/host_mailbox.h',
            'sdk/include/os32/os32_host_notice.h',
            'fs/serialfs/sfs_client.h',
            'fs/serialfs/sfs_block.h',
            'drivers/base/dev.h',
            'drivers/block/netcd.h',
            'fs/vfs.h',
            'fs/isofs/iso9660_media.h',
            'drivers/net/e82557/lan_module.h',
            'kapi/kapi_host_notice.h',
            'tools/tests/hostsvc_fixture.h',
            'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_fixture.py',
            'tools/hostsvc_contracts.pyi',
            'fs/ext2/ext2_vfs.c', 'fs/fat/fatfs_vfs.c',
            'fs/hostdrv/hostdrvfs.c', 'fs/isofs/iso9660.c',
            'fs/serialfs/serialfs.c', 'drivers/base/dev.c',
            'drivers/ata/cd_body.c', 'drivers/net/e82557/lan_body.c'], budget=3)

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
HEADERS = (
    'net/nic.h',
    'net/link.h',
    'fs/serialfs/sfs_proto.h',
    'fs/serialfs/serialfs.h',
    'net/nic_rx.h',
    'net/link_notify.h',
    'net/host_mailbox.h',
    'sdk/include/os32/os32_host_notice.h',
    'fs/serialfs/sfs_client.h',
    'fs/serialfs/sfs_block.h',
    'drivers/base/dev.h',
    'drivers/block/netcd.h',
    'fs/vfs.h',
    'fs/isofs/iso9660_media.h',
    'drivers/net/e82557/lan_module.h',
    'kapi/kapi_host_notice.h',
    'tools/tests/hostsvc_fixture.h',
)
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
MUTATIONS += (
    ('notice-size', 'sdk/include/os32/os32_host_notice.h', 'data[236]', 'data[237]'),
    ('notice-order', 'sdk/include/os32/os32_host_notice.h',
     'serial, dropped, host_generation', 'dropped, serial, host_generation'),
    ('reply-order', 'fs/serialfs/sfs_client.h', 'capacity, length', 'length, capacity'),
    ('lan-slots', 'drivers/net/e82557/lan_module.h', 'API_SLOTS 22u', 'API_SLOTS 23u'),
    ('lan-order', 'drivers/net/e82557/lan_module.h',
     '    const struct NetAdapter *adapter;\n    int (*netcd_register)(const char *);',
     '    int (*netcd_register)(const char *);\n    const struct NetAdapter *adapter;'),
    ('device-order', 'drivers/base/dev.h',
     '    int (*blk_read_u32)(Device *, u32 lba, int count, void *buf);\n    int (*media_check)(Device *, u32 *generation);',
     '    int (*media_check)(Device *, u32 *generation);\n    int (*blk_read_u32)(Device *, u32 lba, int count, void *buf);'),
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
    checks += [('NET_NIC_AUTO', '0'), ('NET_NIC_E82557', '1'),
               ('NET_NIC_LGY98', '2'), ('NET_RX_SFS', '1'), ('NET_RX_LINK', '2'),
               ('NET_RX_SCAN_MAX', '8'), ('LAN_MODULE_OPS_VERSION', '2'),
               ('LAN_MODULE_API_SLOTS', '22'), ('sizeof(LanModuleApi)', '88'),
               ('sizeof(Device)', '72'), ('sizeof(VfsOps)', '104')]
    layouts = {
        'struct NetAdapter': (8, [('acquire', 0), ('release', 4)]),
        'HostNotice': (256, [('serial', 0), ('dropped', 4), ('host_generation', 8),
                            ('service', 12), ('kind', 14), ('length', 16),
                            ('reserved', 18), ('data', 20)]),
        'struct HostAlive': (12, [('host_generation', 0), ('observation', 4), ('valid', 8)]),
        'struct SfsPersistentConfig': (12, [('io', 0), ('io_ctx', 4), ('lan_enabled', 8)]),
        'struct SfsReply': (8, [('data', 0), ('capacity', 4), ('length', 6)]),
        'struct SfsBlock': (24, [('sid', 0), ('handle', 4), ('size', 8), ('mtime', 16)]),
        'struct NetCd': (32, [('handle', 0), ('media_gen', 4), ('last_use', 8),
                              ('alive_seen', 12), ('size', 16), ('mtime', 24)]),
    }
    for typ, (size, fields) in layouts.items():
        checks.append((f'sizeof({typ})', str(size)))
        checks.extend((f'OFF({typ}, {field})', str(offset)) for field, offset in fields)
    checks += [('OFF(LanModuleApi, io)', '60'), ('OFF(LanModuleApi, adapter)', '80'),
               ('OFF(LanModuleApi, netcd_register)', '84'),
               ('OFF(Device, blk_read_u32)', '64'), ('OFF(Device, media_check)', '68'),
               ('OFF(VfsOps, resource_check)', '96'), ('OFF(VfsOps, resource_refresh)', '100')]
    # Compile-time checks concern types only; all layout checks execute below.
    signatures = {
        'net_bind': 'int (*)(const struct NetNicOps *)',
        'net_nic_poll_once': 'int (*)(void)',
        'net_rx_take': 'int (*)(u16, void *, u32, u32 *)',
        'sfs_ensure_session': 'int (*)(u32)',
        'sfs_request_deadline': 'int (*)(u8, const void *, u16, struct SfsReply *, u32)',
        'sfs_alive_observe': 'void (*)(u32)',
    }
    signatures.update({'net_adapter_register': 'int (*)(enum NetNicId, const struct NetAdapter *)',
     'net_start': 'int (*)(enum NetNicId, const u8 *)',
     'net_unbind': 'void (*)(void)',
     'net_tx': 'int (*)(const void *, u32)',
     'net_mac': 'int (*)(u8 *)',
     'net_rx_credit_bytes': 'u32 (*)(void)',
     'net_rx_reset': 'void (*)(u16)',
     'net_tick': 'void (*)(void)',
     'net_rx_demux': 'void (*)(const struct NetNicOps *, const u8 *, const u8 *)',
     'link_notify_session': 'void (*)(u16, u16, int, u32, u16)',
     'link_notify_rx': 'void (*)(u32, const u8 *, u16)',
     'link_notify_tick': 'void (*)(u32)',
     'link_notify_reset': 'void (*)(void)',
     'link_notify_control': 'int (*)(u8, u32, u16)',
     'host_mailbox_init': 'int (*)(void)',
     'host_mailbox_put': 'int (*)(u32, u16, u16, const void *, u16, int)',
     'host_mailbox_stage': 'int (*)(u32, u32, HostNotice *)',
     'host_mailbox_commit': 'void (*)(u32, u32)',
     'host_mailbox_latest_serial': 'u32 (*)(void)',
     'host_alive_snapshot': 'void (*)(struct HostAlive *)',
     'kapi_host_notice_read': 'int (*)(u32, u32, HostNotice *)',
     'kapi_host_notice_serial': 'u32 (*)(void)',
     'sfs_persistent_start': 'int (*)(const struct SfsPersistentConfig *)',
     'sfs_persistent_client': 'const SfsClient *(*)(void)',
     'sfs_persistent_generation': 'u32 (*)(void)',
     'sfs_persistent_stale': 'void (*)(void)',
     'sfs_persistent_enabled': 'int (*)(void)',
     'sfs_persistent_scope_begin': 'int (*)(void)',
     'sfs_persistent_scope_end': 'int (*)(int)',
     'sfs_client_call_deadline': 'int (*)(SfsClient *, u8, const void *, u16, struct '
                                 'SfsReply *, u32)',
     'sfs_client_hello_deadline': 'int (*)(SfsClient *, u32, u32)',
     'sfs_bopen': 'int (*)(const char *, u16, struct SfsBlock *, u32)',
     'sfs_bread': 'int (*)(const struct SfsBlock *, u32, u16, void *, u32, u32)',
     'sfs_bclose': 'int (*)(const struct SfsBlock *, u32)',
     'netcd_register': 'int (*)(const char *)',
     'netcd_read': 'int (*)(Device *, u32, int, void *)',
     'netcd_media_check': 'int (*)(Device *, u32 *)',
     'dev_media_check': 'int (*)(Device *, u32 *)',
     'dev_register_cd': 'int (*)(Device *, int)',
     'vfs_fd_stale_mount': 'void (*)(void *)',
     'iso9660_resource_check': 'int (*)(void *, u32 *)',
     'iso9660_resource_refresh': 'int (*)(void *)',
     'iso9660_media_drop': 'void (*)(void *)',
     'hs_reset': 'void (*)(u32)',
     'hs_advance': 'void (*)(u32)',
     'hs_set_if': 'void (*)(int)',
     'hs_rx': 'int (*)(enum NetNicId, const void *, u32)',
     'hs_tx_take': 'int (*)(void *, u32, u32 *)',
     'hs_fail': 'void (*)(const char *, int)',
     'hs_copyout_hook': 'void (*)(void (*)(void))'})
    text = ''.join(f'#include "{header}"\n' for header in HEADERS)
    text += '#define OFF(t, m) __builtin_offsetof(t, m)\n'
    for name, signature in signatures.items():
        text += (f'_Static_assert(__builtin_types_compatible_p(__typeof__(&{name}), '
                 f'{signature}), "{name}");\n')
    text += '''static int hook_called;
static void hook(void) { hook_called++; }
static int equal(u32 a, u32 b) {
    __asm__ volatile ("" : "+r"(a), "+r"(b));
    return a == b;
}
static int test(void) {
'''
    for i, (actual, expected) in enumerate(checks, 1):
        text += f'    if (!equal({actual}, {expected})) return {i};\n'
    text += '''    int rc = 0;
    hs_reset(0xFFFFFF00u); hs_advance(0x101u); if (!equal(hs_now(), 1)) return 240;
    if (!hs_if()) return 241;
    hs_set_if(0); if (hs_if()) return 242;
    hs_fail("heap", -10);
    if (hs_failure_take("copyout", &rc)) return 243;
    if (!hs_failure_take("heap", &rc) || rc != -10) return 244;
    if (hs_failure_take("heap", &rc)) return 245;
    hs_copyout_hook(hook); hs_copyout_run_hook(); hs_copyout_run_hook();
    if (!equal(hook_called, 1)) return 246;
    hs_fail("nic.rx", -14); hs_copyout_hook(hook); hs_reset(0);
    if (hs_failure_take("nic.rx", &rc) || hs_now() || !hs_if()) return 247;
    hs_copyout_run_hook(); if (!equal(hook_called, 1)) return 248;
    return 0;
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
                    str(src), str(ROOT / 'tools/tests/hostsvc_fixture.c'), '-o', str(exe)], check=True, cwd=ROOT)
    result = host32.run([str(exe)], cwd=ROOT)
    if result.returncode > 0:
        index = result.returncode - 1
        print('layout mismatch:', checks[index] if index < len(checks) else index)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    from hostsvc_fixture import Fixture, UNITS, ethernet, notice
    import ast
    protocols = ast.parse((ROOT / 'tools/hostsvc_contracts.pyi').read_text())
    methods = {node.name: {m.name for m in node.body if isinstance(m, ast.FunctionDef)}
               for node in protocols.body if isinstance(node, ast.ClassDef)}
    assert methods == {
        'ServiceUnavailable': set(),
        'ImageStore': {'__init__', 'open_session', 'close_session', 'dispatch'},
        'Registry': {'__init__', 'register', 'resolve', 'list_bytes', 'dispatch'},
        'Notify': {'__init__', 'session', 'reset', 'hello_extension', 'notify', 'ack', 'tick'},
        'Proc': {'poll', 'output', 'stderr', 'kill', 'close'},
    }
    assert {n.name for n in protocols.body if isinstance(n, ast.FunctionDef)} == {
        'clip_start', 'print_start'}
    for unit in UNITS:
        fixture = Fixture(unit)
        assert not fixture.bound and not fixture.hello and not fixture.alive_valid
        assert fixture.if_enabled and fixture.tick == 0 and not fixture.rx
        fixture.reset(0xffffff00)
        fixture.advance(2570)
        assert fixture.tick == 1
        frame = ethernet(notice(1, 2, 3, b'x'), bytes(6), b'123456')
        fixture.inject(frame)
        assert fixture.rx.popleft() == frame and fixture.take() == []
        fixture.fail('drop', True)
        assert fixture.failure_take('drop') is True
        assert fixture.failure_take('drop') is None
        fixture.close()
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
            src.write_text('#include "sdk/include/os32/os32_host_notice.h"\n')
            subprocess.run([compiler, *FLAGS, '-std=c89', '-pedantic-errors',
                            '-I' + str(ROOT), '-c', str(src),
                            '-o', str(work / 'public.o')], check=True, cwd=ROOT)
            subprocess.run([compiler, *FLAGS, '-I' + str(ROOT),
                            *include_flags(ROOT), '-c',
                            str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                            '-o', str(work / 'fixture.o')], check=True, cwd=ROOT)
            # Cross also compiles the complete fixture and signature assertions.
            src = work / 'types.c'
            src.write_text(source()[0])
            subprocess.run([compiler, *FLAGS, '-I' + str(ROOT),
                            *include_flags(ROOT), '-c', str(src),
                            '-o', str(work / 'types.o')], check=True, cwd=ROOT)
            # Compile existing consumers too: appended fields must pass -Wextra.
            subprocess.run([compiler, *FLAGS, '-Wno-address-of-packed-member',
                            *include_flags(ROOT), '-fsyntax-only', *INITIALIZER_UNITS],
                           check=True, cwd=ROOT)
            print(f'PASS standalone headers, signatures and existing initializers ({compiler})', flush=True)
        if compile_and_run(work, ROOT) != 0:
            raise RuntimeError('normal control failed')
        print('PASS wire values and layouts (ILP32, runner=' + host32.selected_runner() + ')',
              flush=True)
        if args.mutate:
            for name, rel, old, new in MUTATIONS:
                tree = work / name
                # Overlay all tracked headers plus new declarations: relative includes
                # must stay within the same mutated tree.
                tracked = subprocess.check_output(['git', 'ls-files', '*.h'], cwd=ROOT,
                                                  text=True).splitlines()
                for header in set(tracked) | set(HEADERS):
                    target = tree / header
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(ROOT / header, target)
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
