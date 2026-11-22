"""Check /sys/shell.bin against rshell.c sfs_run_child and the serialfs scope contract."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build_media, mutant_settings
from test_kapi_binding import generate
from ppc_shm import guest_available


def observe(media, cpu, folder, case):
    from link_sh_run import Session
    from usb_run import rsh_command

    s = Session(media, cpu, folder, 240)
    try:
        share = folder / 'share'
        share.mkdir()
        (share / 'probe').write_bytes(b'scope\n')
        s.agent.file_root = share
        s.agent.configured_peer = bytes.fromhex('525400123456')
        s.start()
        s.wait(s.ready, 'scope probe READY', 60)
        assert s.word('g_shell_service', 8) and s.word('exec_nest_level') == 1
        assert s.word('cur_res_owner') == 1 and s.word('g_cur_app') == 0
        command = 'sfs run /bin/sfsprob.bin'
        # A plain external .bin run ends with one empty line (as dispprb's
        # "...\n\n"); under sfs run the begin and exit lines are adjacent.
        begin = rb'(sfs: transport=lan nic=link peer=02:00:5e:00:00:01 sid=[1-9][0-9]*\n)\n'
        assert s.word('sfs_probe_starts') == 1
        if case == 'unmounted':
            rsh_command(s, command, b'sfs: LAN rshell requires --sfs-lan resident /host\n')
            rsh_command(s, 'echo $?', b'2\n')
        elif case == 'absent':
            rsh_command(s, command, b'sfs: session not started (-10)\n')
        else:
            scope = rsh_command(s, '/bin/sfsprob.bin leave')
            match = re.fullmatch(begin, scope)
            assert match, scope
            line = match.group(1)
            busy = b'sfs: session not started (-17: /host in use or session active)\n'
            status = 0 if case == 'fixed-exit' else 7
            expected = line + f'sfs: exit={status}\n'.encode()
            for attempt in range(3):
                if case == 'leak-owner' or (case == 'skip-end' and attempt):
                    rsh_command(s, command, busy)
                    rsh_command(s, 'echo $?', b'1\n')
                else:
                    rsh_command(s, command, line if case == 'skip-end' else expected)
                    rsh_command(s, 'echo $?', f'{status}\n'.encode())
                assert s.word('sfs_probe_starts') == 1
            if case == 'normal':
                fault = rsh_command(s, 'sfs run /bin/fault.bin')
                assert fault.startswith(line) and fault.endswith(b'sfs: exit=139\n'), fault
                rsh_command(s, 'echo $?', b'139\n')
                rsh_command(s, command, expected)
                rsh_command(s, 'echo $?', b'7\n')
                assert s.word('sfs_probe_starts') == 1
                rsh_command(s, 'cd /bin', b'')
                before = rsh_command(s, 'pwd')
                assert before.startswith(b'/bin') and before.endswith(b'\n'), before
                rsh_command(s, 'sfs run cd /host', line + b'sfs: exit=0\n')
                rsh_command(s, 'pwd', before)
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
    seen = set()
    cases = ('normal', 'unmounted', 'absent', 'fixed-exit', 'skip-end', 'leak-owner')
    for case in cases if args.mutate else ('normal',):
        folder = work / case
        user = folder / 'user'
        bins = ' '.join(str(user / (name + '.bin')) for name in ('fault', 'sfsprob'))
        kernel = folder / 'kernel'
        kernel.mkdir(parents=True)
        anchor = 'int sfs_persistent_start(const struct SfsPersistentConfig *cfg)\n{'
        settings = mutant_settings(folder, 'init-meter', 'fs/serialfs/serialfs_persistent.c',
                                   anchor, 'volatile unsigned sfs_probe_starts;\n' +
                                   anchor + '\n    ++sfs_probe_starts;' +
                                   ('\n    return OS32_ERR_NOSYS;' if case == 'unmounted' else ''))
        if case in ('fixed-exit', 'skip-end'):
            source = 'userland/shell/rshell.c'
            anchor = '(void)g_api->sfs_end(status);'
            replacement = 'status = 0; ' + anchor if case == 'fixed-exit' else ''
            extra = mutant_settings(folder, case, source, anchor, replacement)
            settings.extend(item.replace('KB_MUT_', 'PPC_RES_MUT_', 1) for item in extra)
        elif case == 'leak-owner':
            # exec_finish calls the persistent owner-exit hook directly.
            meter = Path(settings[1].split('=', 1)[1])
            anchor = '(void)sfs_persistent_scope_end_owned(owner, 0);'
            source = meter.read_text()
            assert source.count(anchor) == 1
            meter.write_text(source.replace(anchor, '(void)owner;'))
        elif case == 'absent':
            (generate(kernel, link=False, ide=True, fep=True) / 'mutant.stamp').touch()
        media = build_media(kernel, 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                    'KB_ROOT_BINS=' + bins, *settings, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert digest not in seen, (case, digest)
        seen.add(digest)
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    print(f'{len(seen)} distinct SerialFS probe images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
