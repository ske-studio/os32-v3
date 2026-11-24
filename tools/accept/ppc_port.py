#!/usr/bin/env python3
"""Exercise the PowerMac serial KAPI over RSH and mac99 ESCC channel A."""
import argparse
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

PAYLOAD = b'\xa5\x5a'
EXPECTED = {
    'normal': (b'PORT echo OK\n\n', b'0\n'),
    'drop-char': (b'PORT echo OK\n\n', b'0\n'),
    'drop-rx': (b'PORT receive failed\n\n', b'7\n'),
    'absent': (b'PORT initialized failed\n\n', b'1\n'),
}


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = "    'system_info': (frozenset({"
    if source.count(anchor) != 1:
        raise RuntimeError('absent port mutation anchor must be unique')
    group = """    'port': (frozenset({
        'serial_puts', 'serial_diag', 'serial_init', 'serial_init_vfast',
        'serial_putchar', 'serial_getchar', 'serial_trygetchar',
        'serial_is_initialized', 'serial_get_status',
    }), frozenset(['port_service.h'])),
""" + anchor + "\n        'sys_set_mtime',"
    zero_anchor = 'ZERO_SLOTS = frozenset({\n'
    if source.count(zero_anchor) != 1:
        raise RuntimeError('absent port zero mutation anchor must be unique')
    (folder / 'kapi_target_ppc.py').write_text(
        source.replace(anchor, group).replace(
            zero_anchor, zero_anchor + "    'serial_is_initialized',\n"))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--feature-link', '1', '--feature-ide', '1',
                    '--feature-fep', '1', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'mutant.stamp').touch()


def observe(media, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)

        class PortSession(Session):
            def serial_a_backend(self):
                return f'tcp:127.0.0.1:{listener.getsockname()[1]}'

        session = PortSession(media, cpu, evidence, 240)
        serial = None
        try:
            session.start()
            serial, _ = listener.accept()
            serial.setblocking(False)
            stream = bytearray()
            with (evidence / 'serial-a.log').open('wb') as log:
                def receive():
                    while True:
                        try:
                            data = serial.recv(4096)
                        except BlockingIOError:
                            break
                        if not data:
                            raise RuntimeError('QEMU serial socket closed')
                        log.write(data)
                        stream.extend(data)

                session.wait(session.ready, 'port cui READY', 60)
                require(session.agent.operator.ready['mode'] == 'cui', 'port RSH mode')
                sys_body = rsh_command(session, '/bin/sysiprob.bin')
                sys_status = rsh_command(session, 'echo $?')
                expected_sys = (b'MTIME invalid failed', b'1\n') if case == 'absent' else (b'MTIME OK', b'0\n')
                require((sys_body.rstrip(b'\n'), sys_status) == expected_sys, repr((sys_body, sys_status)))
                echo = case != 'absent'
                if echo:
                    rsh_command(session, '/bin/portprob.bin', b'PORT settings OK\n\n')
                    rsh_command(session, 'echo $?', b'0\n')
                receive()
                start = len(stream)

                def ready():
                    receive()
                    return b'?' in stream[start:]

                def send():
                    session.wait(ready, 'port probe ready byte', 10)
                    serial.sendall(PAYLOAD)

                body = rsh_command(session, '/bin/portprob.bin' + (' echo' if echo else ''),
                                   during=send if echo else None)
                status = rsh_command(session, 'echo $?')
                # The shell adds a blank line only when the probe's last write ended the line;
                # compare the probe's own text and the exit status.
                want_body, want_status = EXPECTED[case]
                require((body.rstrip(b'\n'), status) == (want_body.rstrip(b'\n'), want_status),
                        repr((case, body, status)))
                if case in ('normal', 'drop-char'):
                    def echoed():
                        receive()
                        return b'?' + PAYLOAD in stream[start:]

                    session.wait(echoed, 'port payload echoed in order', 5)
                    marker = b'ESCC string output'
                    if case == 'drop-char':
                        marker = marker[1:]
                    require(bytes(stream[start:]).endswith(marker + b'?' + PAYLOAD) and
                            (case != 'drop-char' or b'ESCC string output?' not in stream[start:]),
                            repr(stream[start:]))
                receive()
            session.q.call('quit')
            session.p.wait(timeout=5)
            require(session.p.returncode == 0, 'port QEMU exit')
            print(('GREEN' if case == 'normal' else 'RED ' + case) + ' ' + cpu, flush=True)
        finally:
            if serial is not None:
                serial.close()
            session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--case', choices=EXPECTED)
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if any(not os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
           for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3

    from kboot_accept import build_media, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-port-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('portprob', 'sysiprob')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    cases = EXPECTED if args.mutate else ('normal',)
    for case in ((args.case,) if args.case else cases):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'drop-rx':
            mutation = mutant_settings(
                folder, case, 'platform/powermac/escc.c',
                '    int byte;\n    do {\n        byte = pm_escc_trygetchar();',
                '    int byte;\n    while (pm_escc_trygetchar() < 0) {}\n'
                '    do {\n        byte = pm_escc_trygetchar();')
        elif case == 'drop-char':
            mutation = mutant_settings(folder, case, 'kernel/port_service.c',
                                       '    while (*str) {', '    if (*str) ++str;\n    while (*str) {')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        media = build_media(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {media}')
        images[digest] = case
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
