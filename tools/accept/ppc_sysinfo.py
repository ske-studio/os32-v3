#!/usr/bin/env python3
"""Exercise the build information and clock KAPI slots over link-sh RSH on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'normal': (b'INFO OK', b'0\n'),
    'empty-build': (b'INFO build failed', b'2\n'),
    'swap-time': (b'INFO clock words failed', b'4\n'),
    'absent': (b'INFO clock unavailable', b'3\n'),
}


def absent_bindings(folder, out, slot='sys_time_now'):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    # Anchor on the table head: provider groups shrink as slots are published.
    anchor = "PROVIDER_GROUPS = {\n"
    if source.count(anchor) != 1:
        raise RuntimeError('system_info mutation anchor must be unique')
    group = f"    'accept_absent': (frozenset({{{slot!r}}}), frozenset()),\n"
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, anchor + group))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def observe(elf, cpu, evidence, case, app='infoprob', expected=EXPECTED):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'sysinfo cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'sysinfo RSH mode')
        body = rsh_command(session, f'/bin/{app}.bin')
        status = rsh_command(session, 'echo $?')
        require((body.rstrip(b'\n'), status) == expected[case], repr((case, body, status)))
        if app == 'memstat' and case == 'normal':
            used = rsh_command(session, '/bin/memstat.bin kheap-used').strip()
            status = rsh_command(session, 'echo $?')
            require(status == b'0\n' and used.isdigit() and 0 < int(used) <= 0xffffffff
                    and int(used) != 0xa5a5a5a5, repr(('system kheap_used', used, status)))
            print('system kheap_used=' + used.decode(), flush=True)
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'sysinfo QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED ' + case) + ' ' + cpu, flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--memstat', action='store_true')
    args = parser.parse_args()
    app = 'memstat' if args.memstat else 'infoprob'
    expected = ({'normal': (b'MEMSTAT OK', b'0\n'),
                 **{case: (b'MEMSTAT FAIL', b'1\n') for case in ('diag', 'boundary', 'size133', 'copy133', 'peak', 'absent')}}
                if args.memstat else EXPECTED)
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if any(not os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
           for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3

    from kboot_accept import build, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-sysinfo-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', app)]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in expected if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'swap-time':
            mutation = mutant_settings(folder, case, 'kapi/kapi_sysinfo.c',
                                       'sys_time_now(&snap_lo, &snap_hi)',
                                       'sys_time_now(&snap_hi, &snap_lo)')
        elif case == 'empty-build':
            mutation = mutant_settings(folder, case, 'kapi/kapi_sysinfo.c',
                                       'kapi_sys_build_time_copy(buf, (u32)size);',
                                       "buf[0] = 0;")
        elif case == 'boundary':
            mutation = mutant_settings(folder, case, 'kapi/kapi_sysinfo.c',
                                       'size < MEMSTAT_MIN', 'size <= MEMSTAT_MIN')
        elif case == 'size133':
            mutation = mutant_settings(folder, case, 'kapi/kapi_sysinfo.c',
                                       'snap.size = size <', 'snap.size = size == 133 ? 132 : size <')
        elif case == 'copy133':
            mutation = mutant_settings(folder, case, 'kapi/kapi_sysinfo.c',
                                       'kmemcpy(out, &snap, snap.size);',
                                       'kmemcpy(out, &snap, size == 133 ? 132 : snap.size);')
        elif case == 'peak':
            mutation = mutant_settings(folder, case, 'kapi/kapi_sysinfo.c',
                                       'snap.kheap_peak = kmalloc_peak_bytes;', 'snap.kheap_peak = 0;')
        elif case == 'diag':
            header = 'platform/powermac/platform_mem_stat.h'
            before = 'platform_mem_stat_diag(MemStat *snap)\n{\n    (void)snap;'
            settings = mutant_settings(folder, case, header, before,
                                       before.replace('(void)snap;', 'snap->kdebug_state = 1;'))
            mutated_header = settings[1].split('=', 1)[1]
            mutation = mutant_settings(folder, 'include-diag', 'kapi/kapi_sysinfo.c',
                                       '"platform_mem_stat.h"', f'"{mutated_header}"')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen', 'mem_stat' if args.memstat else 'sys_time_now')
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {elf}')
        images[digest] = case
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case, app, expected)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
