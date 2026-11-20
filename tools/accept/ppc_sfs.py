"""6b A5/A8/A10 (PPC_KAPI_FILL_DESIGN, serialfs): verify RSH OUT and resident scope lifetime."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build, mutant_settings
from test_kapi_binding import generate
from ppc_shm import guest_available


def observe(elf, cpu, folder, case):
    from link_sh_run import Session
    from usb_run import rsh_command

    s = Session(elf, cpu, folder, 240)
    try:
        share = folder / 'share'
        share.mkdir()
        (share / 'probe').write_bytes(b'scope\n')
        s.agent.file_root = share
        s.agent.configured_peer = bytes.fromhex('525400123456')
        s.start()
        s.wait(s.ready, 'scope probe READY', 60)
        command = 'sfs run /bin/sfsprob.bin'
        # A plain external .bin run ends with one empty line (as dispprb's
        # "...\n\n"); under sfs run the begin and exit lines are adjacent.
        begin = rb'(sfs: transport=lan nic=link peer=02:00:5e:00:00:01 sid=[1-9][0-9]*\n)\n'
        assert s.word('sfs_probe_starts') == 1
        if case == 'absent':
            rsh_command(s, command, b'sfs: session not started (-10)\n')
        else:
            scope = rsh_command(s, '/bin/sfsprob.bin leave')
            match = re.fullmatch(begin, scope)
            assert match, scope
            line = match.group(1)
            expected = line if case == 'drop-exit' else line + b'sfs: exit=7\n'
            for _ in range(3):
                rsh_command(s, command, expected)
                rsh_command(s, 'echo $?', b'7\n')
                assert s.word('sfs_probe_starts') == 1
            if case == 'normal':
                rsh_command(s, 'cd /bin', b'')
                rsh_command(s, 'sfs run cd /host', line + b'sfs: exit=0\n')
                rsh_command(s, 'pwd', b'/bin\n')
        s.q.call('quit')
        s.p.wait(timeout=5)
        assert s.p.returncode == 0
        print(('GREEN' if case == 'normal' else 'RED') + f' {case} {cpu}')
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not all(os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
               for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print('SKIP: PowerPC toolchain unavailable')
        return 3
    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback unavailable')
        return 3
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='ppc-sfs-', dir=os.environ.get('TMPDIR'))).resolve()
    user = work / 'user'
    bins = ' '.join(str(user / (name + '.bin')) for name in ('hello', 'fault', 'sfsprob'))
    seen = set()
    cases = ('normal', 'absent', 'drop-exit')
    for case in cases if args.mutate else ('normal',):
        folder = work / case
        kernel = folder / 'kernel'
        kernel.mkdir(parents=True)
        anchor = 'int sfs_persistent_start(const struct SfsPersistentConfig *cfg)\n{'
        settings = mutant_settings(folder, 'init-meter', 'fs/serialfs/serialfs_persistent.c',
                                   anchor, 'volatile unsigned sfs_probe_starts;\n' +
                                   anchor + '\n    ++sfs_probe_starts;')
        if case == 'drop-exit':
            extra = mutant_settings(folder, case, 'fs/serialfs/serialfs_session.c',
                                    'if (rc == 0) console_printf(KLOG_LEGACY_CYAN, "sfs: exit=%d\\n", exit_code);', '')
            settings.extend(item.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for item in extra)
        elif case == 'absent':
            (generate(kernel, link=False) / 'stamp').touch()
        elf = build(kernel, 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                    'KB_ROOT_BINS=' + bins, *settings, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in seen, (case, digest)
        seen.add(digest)
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    print(f'{len(seen)} distinct SerialFS probe images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
