#!/usr/bin/env python3
"""W8 transport runtime contracts with real SfsClient and explicit RED marks."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
COVERS = ['platform/powermac/hostfs_io.c', 'platform/powermac/hostfs.h',
          'platform/powermac/hostfs_peer.c', 'platform/powermac/hostfs_boot.c',
          'arch/ppc/kernel_trap.c', 'tools/tests/ppc_hostfs_io.c',
          'tools/tests/ppc_hostfs_io.py']
CHECK = dict(name='ppc-hostfs-io', tier=1, budget=30, covers=COVERS,
             inputs=['platform/powermac/lan_boot.h', 'fs/serialfs/sfs_client.c',
                     'fs/serialfs/sfs_proto.c', 'fs/serialfs/*.h', 'fs/vfs*',
                     'fs/serialfs/serialfs.c', 'fs/serialfs/serialfs_persistent.c',
                     'kernel/console/*.h', 'drivers/base/dev.h', 'net/host_mailbox.h',
                     'lib/kstring_c.c',
                     'arch/ppc/pd_walk.c', 'arch/ppc/*.h',
                     'kernel/irq/irq.h', 'kernel/irq/irq_math.h',
                     'include/*.h', 'lib/kstring.h', 'lib/crc32.[ch]',
                     'lib/crc32_core.inc', 'lib/le_internal.h', 'lib/divdi3.c',
                     'platform/powermac/platform_irq_limits.h',
                     'sdk/include/os32/*.h'])
SOURCE = ROOT / COVERS[0]


def exercise(source, directory, expected=0, marker=None, peer=None):
    path = pathlib.Path(directory)
    (path / 'io.c').write_text(source)
    (path / 'peer.c').write_text(peer or (ROOT / 'platform/powermac/hostfs_peer.c').read_text())
    binary = path / 'host'
    flags = ['gcc', '-m32', '-ffreestanding', '-fno-pie', '-no-pie',
             '-nostdlib', '-static', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
             '-DPM_HFS_FAKE_TB', '-DPPC_TRAP_HOST_TEST']
    includes = [ROOT, ROOT / 'include', ROOT / 'platform/powermac',
                ROOT / 'arch/ppc', ROOT / 'fs/serialfs', ROOT / 'lib',
                ROOT / 'sdk/include/os32', ROOT / 'fs', ROOT / 'kernel/console',
                ROOT / 'drivers/base']
    sources = [path / 'io.c', ROOT / 'tools/tests/ppc_hostfs_io.c',
               ROOT / 'fs/serialfs/sfs_client.c', ROOT / 'fs/serialfs/sfs_proto.c',
               ROOT / 'arch/ppc/kernel_trap.c', ROOT / 'arch/ppc/pd_walk.c',
               ROOT / 'lib/crc32.c', ROOT / 'lib/divdi3.c', path / 'peer.c',
               ROOT / 'platform/powermac/hostfs_boot.c', ROOT / 'fs/serialfs/serialfs.c',
               ROOT / 'fs/serialfs/serialfs_persistent.c', ROOT / 'fs/vfs.c',
               ROOT / 'fs/vfs_fd.c', ROOT / 'lib/kstring_c.c']
    subprocess.run(flags + ['-I' + str(p) for p in includes] +
                   [str(p) for p in sources] + ['-o', str(binary)], check=True)
    result = subprocess.run(['qemu-i386', str(binary)], capture_output=True, timeout=10)
    assert result.returncode == expected, (result.returncode, expected, result.stdout, result.stderr)
    if marker is None:
        assert result.stdout.endswith(b'PASS\n'), result.stdout
    else:
        reach, suffix = marker
        assert result.stdout.endswith(reach.to_bytes(4, 'little') + suffix), result.stdout


def main():
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as directory:
        exercise(source, directory)
        print('baseline PASS')
        if '--mutate' in sys.argv:
            mutations = [
                ('stopped tick', 'return (u32)(pm_hfs_tb() / (t->tb_hz / 100u));',
                 'return (u32)(pm_hfs_tb() / (t->tb_hz / 100u)) & 0u;',
                 41, (40, b'STOPPED-CLOCK\n')),
                ('state gate', 'if (!pm_lan_fs_running()) return -1;',
                 'if (0 && !pm_lan_fs_running()) return -1;', 12, (12, b'FAIL\n')),
                ('IRQ wait', 'return kctx_irq_depth == 0;',
                 'return 1;', 11, (11, b'FAIL\n'))]
            for index, (name, old, new, rc, marker) in enumerate(mutations):
                assert source.count(old) == 1, name
                path = pathlib.Path(directory) / str(index)
                path.mkdir()
                exercise(source.replace(old, new), path, rc, marker)
                print(name + ': runtime RED with reach marker')
            peer = (ROOT / 'platform/powermac/hostfs_peer.c').read_text()
            anchor = 'observe(h, src); /* Responses can be the first host observation. */'
            assert peer.count(anchor) == 1
            path = pathlib.Path(directory) / 'peer'
            path.mkdir()
            exercise(source, path, 60, (60, b'FAIL\n'),
                     peer.replace(anchor, '(void)0;'))
            print('FS_RSP observation: runtime RED with reach marker')


if __name__ == '__main__':
    main()
