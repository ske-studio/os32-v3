#!/usr/bin/env python3
"""LAN_OPERATOR_DESIGN §5, §8 P4c: deferred SFS CALL and shared deadline."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-hostfs-link-io', tier=1, needs=[], budget=15,
    env=['ppc-cross'], basis=['LAN_OPERATOR_DESIGN §5', 'LAN_OPERATOR_DESIGN §8',
        'PACK ppc-sfs-deadline PM conditions B/C'],
    covers=['platform/powermac/hostfs_link_io.c', 'tools/tests/ppc_hostfs_link_io.c',
            'tools/tests/test_ppc_hostfs_link_io.py', 'platform/powermac/hostfs_link_peer.c',
            'platform/powermac/hostfs_boot.c',
            'fs/vfs*.c', 'fs/*.h', 'fs/serialfs/serialfs*.c',
            'fs/vfs_fd_stdin.inc', 'exec/*.h', 'kernel/*.h',
            'kernel/console/*.h', 'drivers/base/dev.h', 'mm/kmalloc.h',
            'net/host_mailbox.c', 'lib/kstring_c.c', 'lib/kstring.h',
            'platform/powermac/hostfs.h', 'platform/powermac/lan_boot.h',
            'drivers/net/gem/*.h',
            'fs/serialfs/*.h', 'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_proto.c',
            'fs/vfs.h', 'net/link*.h', 'net/host_mailbox.h', 'kernel/idle_work.h',
            'kernel/irq/*.h', 'platform/powermac/platform_irq_limits.h',
            'include/*.h', 'sdk/include/os32/*.h', 'lib/le_internal.h',
            'lib/crc32.[ch]', 'lib/crc32_core.inc', 'tools/tests/host32.py'])
MUTATIONS = [
    ('open-hostdown', 'rc == OS32_ERR_HOSTDOWN ||', '0 ||'),
    ('open-full', 'rc == OS32_ERR_FULL) return;', '0) return;'),
    ('deadline', 'if ((i32)(pm_hfs_now(0) - until) >= 0)', 'if (0)'),
    ('retry identity', 'if (same) return result;', 'if (0 && same) return result;'),
    ('IRQ', 'kctx_irq_depth == 0 && _irq_enabled()', '_irq_enabled()'),
    ('EE', 'kctx_irq_depth == 0 && _irq_enabled()', 'kctx_irq_depth == 0'),
    ('CRC', 'if (sfs_crc32(', 'if (0 && sfs_crc32('),
    ('length', 'len != SFS_HDR_LEN + payload + SFS_CRC_LEN', '0'),
    ('oneway', 'if (!length) { finish(0); return; }', 'if (!length) { finish(-1); return; }'),
    # Review F1: the SFS CALL declaration must equal the frame length.
    ('declared length', 'u32 n = tx_len, count = 0, pos = 9;', 'u32 n = tx_len - 1, count = 0, pos = 9;'),
    # The FG1 wait must sleep until an interrupt, not spin (PM/mainline 2026-11-15).
    ('wait spins', '    idle_work_run();\n    _idle();\n}', '    idle_work_run();\n}'),
    ('sleep without guard', '    if (!pm_hfs_can_wait(ctx)) return;\n    idle_work_run();', '    if (!pm_hfs_can_wait(ctx)) { _idle(); return; }\n    idle_work_run();'),
]
def exercise(folder, source, extra=None):
    folder.mkdir()
    (folder / 'io_source.inc').write_text(source)
    (folder / 'io.h').write_text('static inline unsigned int irq_save(void) { return 1; }\nstatic inline void irq_restore(unsigned int f) { (void)f; }\n')
    (folder / 'arch_io.h').write_text('int _irq_enabled(void);\nvoid _idle(void);\n')
    exe = folder / 'probe'
    compiler_headers = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    includes = ['', 'include', 'platform/powermac', 'fs/serialfs', 'fs', 'lib',
                'sdk/include/os32', 'kernel/console', 'drivers/base', 'exec', 'input', 'kernel', 'mm', 'net']
    cmd = ['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
           '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector',
           '-nostdlib', '-nostdinc', '-isystem', compiler_headers, '-static',
           '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
           '-DKBOOT_LINK=1', '-I' + str(folder)]
    cmd += ['-I' + str(ROOT / path) for path in includes]
    cmd += [str(ROOT / path) for path in ['tools/tests/ppc_hostfs_link_io.c',
            'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_proto.c', 'lib/crc32.c',
            'platform/powermac/hostfs_boot.c', 'platform/powermac/hostfs_link_peer.c',
            'fs/serialfs/serialfs.c', 'fs/serialfs/serialfs_persistent.c',
            'fs/vfs.c', 'fs/vfs_fd.c', 'lib/kstring_c.c', 'net/host_mailbox.c']]
    if extra:
        target, old, new = extra
        text = (ROOT / target).read_text()
        assert text.count(old) == 1, target
        replacement = folder / Path(target).name
        replacement.write_text(text.replace(old, new))
        cmd[cmd.index(str(ROOT / target))] = str(replacement)
    subprocess.run(cmd + ['-o', str(exe)], check=True)
    result = host32.run([str(exe)], capture_output=True)
    if result.stdout: print('runtime line:', int.from_bytes(result.stdout, 'little'), flush=True)
    return result.returncode

def main():
    source = (ROOT / 'platform/powermac/hostfs_link_io.c').read_text()
    with tempfile.TemporaryDirectory(prefix='ppc-hostfs-link-') as td:
        work = Path(td)
        assert exercise(work / 'control', source) == 0
        print('GREEN deferred frame / partial IO / fixed deadline / IRQ / EE / no replay / CRC / length / empty 200 / real SfsClient', flush=True)
        if '--mutate' in sys.argv:
            for i, (name, old, new) in enumerate(MUTATIONS):
                assert source.count(old) == 1, name
                assert exercise(work / str(i), source.replace(old, new)) == 1, name
                print('RED runtime:', name, flush=True)
            for i, extra in enumerate([
                ('platform/powermac/hostfs_boot.c', 'if (!rc) rc = sfs_persistent_start(&cfg);', '(void)cfg;'),
                ('platform/powermac/hostfs_link_peer.c', 'return fixed ? allowed : 0;', 'return 0;'),
                ('fs/serialfs/serialfs_persistent.c', 'if (!host_valid) reconnect = 1;', 'reconnect = 1;'),
                ('fs/serialfs/serialfs_persistent.c', 'if (host_valid && host_generation != generation)', 'if (0 && host_valid && host_generation != generation)'),
            ]):
                assert exercise(work / f'integration{i}', source, extra) == 1, extra[0]
                print('RED runtime:', extra[0], flush=True)
if __name__ == '__main__': main()
def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
