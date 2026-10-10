"""T2h h3b: real pipe/TVRAM with guest verdicts, IPL geometry and deploy scope.

The small verdict harness is LP64; shutdown's real ILP32 gfx harness is run
separately by test_shutdown_probe.py through host32 (qemu in Codex).
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from contextlib import ExitStack
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('fs/pipe_buffer.c', 'kernel/console.c', 'userland/tests/pipe_owner_test.c',
           'userland/tests/audit_test.c', 'userland/tests/kout_test.c',
           'tools/gen_v86_int80.py', 'tools/deploy_manifests.py', 'tools/hostdrv_deploy.py',
           'tools/nhd_deploy.py')
sys.path.insert(0, str(ROOT / 'tools'))
import deploy_manifests


def function(source, name):
    match = re.search(r'^(?:void|int) ' + name + r'\([^\n]*\)\n\{', source, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def run_harness(mutation=None):
    with tempfile.TemporaryDirectory(prefix='os32-h3b-') as directory:
        tmp = Path(directory)
        console = (ROOT / 'kernel/console.c').read_text()
        console = '\n'.join(function(console, name) for name in
                            ('tvram_putchar_at', 'tvram_putkanji_at', 'tvram_readchar_at', 'tvram_reverse_cell'))
        if mutation:
            old, new = mutation
            assert console.count(old) == 1, old
            console = console.replace(old, new)
        (tmp / 'console_source.c').write_text(console)
        (tmp / 'pipe_source.c').write_text((ROOT / 'fs/pipe_buffer.c').read_text())
        (tmp / 'pipe_guest.c').write_text((ROOT / 'userland/tests/pipe_owner_test.c').read_text())
        (tmp / 'audit_guest.c').write_text((ROOT / 'userland/tests/audit_test.c').read_text())
        (tmp / 'kout_guest.c').write_text((ROOT / 'userland/tests/kout_test.c').read_text())
        exe = tmp / 'h3b'
        command = ['gcc', '-std=gnu11', '-D__cdecl=', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                   '-I' + str(tmp)]
        command += ['-I' + str(ROOT / p) for p in ('sdk/include/os32', 'include', 'fs', 'kernel', 'exec')]
        subprocess.run(command + [str(ROOT / 'tools/tests/h3b_fixtures_host.c'), '-o', str(exe)],
                       check=True, cwd=ROOT)
        return subprocess.run([str(exe)], capture_output=True, text=True, check=False)


def check_image_and_deploy():
    spec = importlib.util.spec_from_file_location('gen_v86_int80', ROOT / 'tools/gen_v86_int80.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    data = module.image_bytes()
    assert len(data) == 1261568 and data[:3] == bytes.fromhex('cd 80 f4')
    assert data[3:] == bytes(len(data) - 3)
    geometry = re.search(r'\{\s*1261568,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}',
                         (ROOT / 'drivers/loop_dev.c').read_text())
    assert geometry and tuple(map(int, geometry.groups())) == (77, 2, 8, 1024)
    boot = (ROOT / 'kernel/v86.c').read_text().split('int v86_boot2(')[1]
    assert 'loop_dev_read_chs(V86_DISK_SLOT, 0, 0, 1,' in boot
    assert len(module.IPL) <= module.SECTOR_BYTES
    with tempfile.TemporaryDirectory(prefix='os32-int80-') as directory:
        paths = [Path(directory) / n for n in ('one.img', 'two.img')]
        for path in paths:
            subprocess.run([sys.executable, str(ROOT / 'tools/gen_v86_int80.py'), '--out', str(path)], check=True)
        assert paths[0].read_bytes() == paths[1].read_bytes() == data
    normal = deploy_manifests.load_merged()
    host = deploy_manifests.load_merged(include_host_only=True)
    assert not any(e['host'] == 'build/out/int80.img' for e in normal['filesystem']['files'])
    entries = [e for e in host['filesystem']['files'] if e['host'] == 'build/out/int80.img']
    assert len(entries) == 1 and entries[0]['guest'] == '/test/int80.img' and entries[0]['host_only'] is True
    for name in ('pipe_owner_test', 'shutdown_probe'):
        assert any(e['host'] == f'userland/tests/{name}.bin' for e in normal['filesystem']['files'])
    # The actual HostDrv loader opts in, all existing NHD/package callers use
    # the default exclusion. This test does not deploy to the shared directory.
    import hostdrv_deploy
    cfg = hostdrv_deploy.load_deploy_yaml()
    assert any(e['host'] == 'build/out/int80.img' for e in cfg['filesystem']['files'])
    import nhd_deploy
    assert not any(e['host'] == 'build/out/int80.img'
                   for e in nhd_deploy.load_deploy_yaml()['filesystem']['files'])
    # Exercise HostDrv -> NHD traversal in private directories, without sudo,
    # mounting, or emulator access. Normal files must still be copied.
    with tempfile.TemporaryDirectory(prefix='os32-host-only-') as directory:
        root = Path(directory)
        source, dest = root / 'host', root / 'nhd'
        (source / 'test').mkdir(parents=True)
        (source / 'test/int80.img').write_bytes(data)
        (source / 'test/normal.txt').write_text('control')

        def ensure_dir(guest):
            path = dest / guest.lstrip('/')
            path.mkdir(parents=True, exist_ok=True)
            return str(path), 'ok'

        def copy(command, **kwargs):
            assert command[:3] == ['sudo', 'cp', '--'], command
            shutil.copyfile(command[3], command[4])
            return subprocess.CompletedProcess(command, 0, '', '')

        with ExitStack() as stack:
            stack.enter_context(patch.dict(os.environ, {'HOSTDRV_DIR': str(source)}))
            stack.enter_context(patch.object(nhd_deploy, 'MOUNT_POINT', str(dest)))
            for name in ('legacy_pt_guard', 'ensure_mounted_for_kernel', 'guard_root', 'run_sync'):
                stack.enter_context(patch.object(nhd_deploy, name, return_value=True))
            stack.enter_context(patch.object(nhd_deploy, 'ensure_dir', side_effect=ensure_dir))
            stack.enter_context(patch.object(nhd_deploy, 'guard_dest',
                side_effect=lambda guest, **kw: (str(dest / guest.lstrip('/')), 'ok')))
            stack.enter_context(patch.object(nhd_deploy.subprocess, 'run', side_effect=copy))
            assert nhd_deploy.do_sync_from_hostdrv()
        assert not (dest / 'test/int80.img').exists()
        assert (dest / 'test/normal.txt').read_text() == 'control'
    kout = (ROOT / 'userland/tests/kout_test.c').read_text()
    for case in ('2d', '3c'):
        line = next(line for line in kout.splitlines() if f'"{case} NULL:' in line)
        assert 'N/A' in line and 'SKIP' not in line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check_image_and_deploy()
    result = run_harness()
    print(result.stdout + result.stderr, end='')
    assert result.returncode == 0, result.returncode
    if args.mutate:
        for name, old, new in [
            ('read-sentinel', 'if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) return;\n    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;\n    if (code)',
             'if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) { if (code) *code = 0; return; }\n    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;\n    if (code)'),
            ('reverse-result', 'y >= TVRAM_ROWS) return 0;', 'y >= TVRAM_ROWS) return 1;'),
            ('write-next', '= (u16)(u8)ch;', '= (u16)(u8)(ch + 1);'),
            ('out-of-range-write', 'TVRAM_ROWS) return;\n    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;\n    *(volatile u16 *)',
             'TVRAM_ROWS) { x = y = 0; }\n    u32 offset = (u32)y * TVRAM_BPR + (u32)x * 2;\n    *(volatile u16 *)'),
        ]:
            result = run_harness((old, new))
            assert result.returncode == 1 and 'FAIL audit TVRAM' in result.stdout, (name, result)
            print('RED ' + name)


if __name__ == '__main__':
    main()
