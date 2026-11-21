#!/usr/bin/env python3
"""S-C: USER nesting and direct shell acceptance on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import socket
import tempfile

from kboot_accept import build, mutant_settings
from kboot_run import require

CHECK = dict(name='ppc-sc', tier=2, budget=900, needs=[],
             covers=['platform/powermac/input_usb.[ch]', 'platform/powermac/input_keymap.c', 'tools/ppc/sc_run.py', 'userland/ppc/nest.c'],
             inputs=['build/ppc-user.mk', 'build/targets/ppc-powermac.mk',
                     'tools/ppc/sb_run.py', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/ppc_lan_host.py', 'tools/mkroot_image.py',
                     'platform/powermac/cli_boot.c', 'arch/ppc/exec_context.c',
                     'arch/ppc/kapi_call.c', 'platform/powermac/shell_boot.c',
                     'platform/powermac/input_boot.c', 'platform/powermac/input_decode.c',
                     'platform/powermac/console_stream.c', 'platform/powermac/kboot_console.c',
                     'userland/ppc/hello.c', 'tools/ppc/link_sh_run.py',
                     'tools/ppc/link_run.py', 'tools/ppc/usb_run.py',
                     'tools/host_agent.py', 'tools/host_operator_socket.py',
                     'tools/host_rshell.py', 'tools/ppc_link_transport.py',
                     'kernel/console/con_sink.c', 'mm/kmalloc.c',
                     'userland/shell/rshell_lan.c', 'userland/ppc/sh/link_profile.h',
                     'fs/serialfs/serialfs_persistent.c', 'kernel/console/console.c',
                     'platform/powermac/usb_boot.c', 'platform/powermac/usb_platform.c',
                     'tools/host_linux.py', 'tools/host_notify.py', 'tools/host_services.py',
                     'tools/host_sfs_link.py', 'tools/ppc/sd_run.py'])

LINK_NEST_EXPECTED = b'hello ok\nnest kind=1 code=7 parent=12345\n' * 2
EXPECTED = ('/bin/nest.bin\n' +
            'hello ok\nnest kind=1 code=7 parent=12345\n' * 2 +
            'exit=5\nos32> ')


def check_shell(text, fd1=True, restart=True):
    """Check results between prompts, never accepting command echo as output."""
    text = text.replace('\r', '')
    require('OS32 External Shell Started' in text and 'sh> ' in text, 'sh startup')
    # Split at prompts, then discard the echoed input line in each interval.
    results = {}
    for interval in text.split('sh> ')[1:]:
        echo, newline, output = interval.partition('\n')
        if newline:
            results.setdefault(echo, []).append(output)

    def outputs(cmd, expected):
        require(results.get(cmd) == expected,
                'shell output '+repr(cmd)+': '+repr(results.get(cmd)))

    help_output = results.get('help', [''])[0]
    names = {line.strip() for line in help_output.splitlines()}
    registered = set('help ? clear cls tick uptime time exit cp mv rm cat cat2 '
                     'echo ls cd pwd mkdir rmdir env set export unset source . '
                     'ask if goto return'.split())
    require(registered <= names, 'help registered commands')
    require(not names.intersection('ver uname date beep np2 mnt sys pci hdprep filer rshell'.split()),
            'help excluded commands')
    outputs('echo $PATH', ['/bin\n'])
    outputs('echo $HOME', ['/tmp\n'])
    outputs('echo $SHELL', ['/bin/sh.bin\n'])
    outputs('pwd', ['/\n', '/tmp/\n'])
    listing = results.get('ls /bin', [''])[0].upper().split()
    require('SH.BIN' in listing and 'HELLO.BIN' in listing, 'bin listing')
    outputs('echo sc-ok > /tmp/sc', [''])
    outputs('cat /tmp/sc', ['sc-ok\n'] if fd1 else [''])
    diagnostic = results.get('cat /nofile > /tmp/e', [''])[0]
    require('cat: /nofile: No such file or directory\n' == diagnostic,
            'diagnostic missing from terminal')
    outputs('cat /tmp/e', [''])
    outputs('/bin/hello.bin ok', ['hello ok\n', 'hello ok\n'])
    outputs('echo $?', ['7\n', '7\n'])
    outputs('echo $SC_PARENT', ['kept\n'])
    outputs('echo again', ['again\n', 'again\n'])
    outputs('source /tmp/test.sh', ['script-ok\n'])
    outputs('echo pipe-ok | cat', ['pipe-ok\n'])
    require('edit-ok\n' in text and 'sh exited=3\n' in text, 'edit/exit result')
    require(text.count('OS32 External Shell Started') == (2 if restart else 1), 'sh restart')
    require(('sc-ok\n' in text) == fd1, 'fd1 redirect/cat routing')
    require('\ncancel-me\n' not in text, 'cancelled command executed')


def link_main(args):
    """PPC_P5_REST_DESIGN §2・§5: RSH nesting and shell output contracts."""
    from link_sh_run import Session, settings, check_map, RSHELL, EXECUTE
    base = Path(os.environ['TMPDIR'])/'sc-run'
    base.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='link-', dir=base))
    print('evidence='+str(work), flush=True)
    for kind in (() if args.build_only else (socket.SOCK_DGRAM, socket.SOCK_STREAM)):
        try:
            with socket.socket(socket.AF_INET, kind) as probe:
                probe.bind(('127.0.0.1', 0))
        except OSError as exc:
            print('SKIP ppc-sc link socket: '+str(exc))
            return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 3
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    hashes = set()
    for name in ['normal'] + (['parent-result', 'fault-break'] if args.mutate else []):
        folder = work/name; folder.mkdir()
        opts = settings(folder, 'fault-break' if name == 'fault-break' else None)
        bins = ' '.join(str(folder/'user'/(n+'.bin')) for n in ('hello', 'nest'))
        opts = [o if not o.startswith('KB_ROOT_BINS=') else 'KB_ROOT_BINS='+bins for o in opts]
        if name == 'parent-result':
            extra = mutant_settings(folder, name, 'arch/ppc/kapi_call.c',
                'return ppc_kapi_invoke(&api, current_slot, values);',
                'static unsigned reads;\n'
                '    if (current_slot == (__builtin_offsetof(KernelAPI, exec_last_result) - 8) / 4 && ++reads == 2) return (u32)-1;\n'
                '    return ppc_kapi_invoke(&api, current_slot, values);')
            opts += [o.replace('KB_MUT_', 'KB_MUT_THIRD_') for o in extra]
        elf = build(folder/'kernel', *opts, 'kb-user', 'kernel')
        check_map(elf.with_suffix('.map'))
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        require(digest not in hashes, 'distinct '+name)
        hashes.add(digest)
        print(name, digest, flush=True)
        if args.build_only:
            continue
        for cpu in cpus if name == 'normal' else cpus[:1]:
            session = Session(elf, cpu, folder/cpu, 180)
            try:
                session.start()
                session.wait(session.ready, 'HELLO to cui READY', 60)
                require(session.agent.operator.ready['mode'] == 'cui', 'link sh mode')
                normal = LINK_NEST_EXPECTED
                expected = normal
                # Only the second result is corrupted, keeping both children alive.
                if name == 'parent-result':
                    expected = (b'hello ok\nnest kind=1 code=7 parent=12345\n'
                                b'hello ok\nnest kind=0 code=0 parent=12345\n')
                result = session.cmd('/bin/nest.bin')
                # F5: no intervening shell command may overwrite this status.
                session.cmd('echo $?', b'5\n')
                session.cmd('echo after-nest', b'after-nest\n')
                from link_sh_run import check_body
                check_body('/bin/nest.bin', result['result'], expected)
                if name != 'parent-result':
                    for line, output in (
                        ('echo sc-ok > /tmp/sc', b''), ('cat /tmp/sc', b'sc-ok\n'),
                        ('cat /nofile > /tmp/e', b'cat: /nofile: No such file or directory\n'),
                        ('cat /tmp/e', b''), ('echo pipe-ok | cat', b'pipe-ok\n'),
                        ('/bin/hello.bin ok', b'hello ok\n'), ('echo $?', b'7\n')):
                        session.cmd(line, output)
                    generation = session.word('g_shell_service', 8)
                    session.agent.notifier.notify_number(RSHELL, EXECUTE, b'00001000 p3-fault\n')
                    if name == 'fault-break':
                        from kboot_run import serial
                        session.wait(lambda: '/sys/shell.bin returned ' in serial(session.out/'serial-a.log'),
                                     'resident fault returned', 60)
                        before = len(session.events)
                        session.agent.notifier.notify_number(RSHELL, EXECUTE, b'00001001 echo afterfault\n')
                        session.settle(2)
                        require(session.word('g_shell_service', 8) == generation
                                and session.word('exec_nest_level') == 0, 'fault stopped restart')
                        require(not any(e['parts'][:2] == ['RSH', 'OUT'] for e in session.events[before:]),
                                'command completed after stopped restart')
                    else:
                        session.wait(lambda: session.word('g_shell_service', 8) > generation
                                     and session.ready(), 'next resident generation', 60)
                        session.cmd('echo afterfault', b'afterfault\n')
                else:
                    require(result['result'] != b'> /bin/nest.bin\n'+normal+b'\x04',
                            'parent-result survived')
                    print('RED link parent-result: second result only; status=5 and echo alive', flush=True)
                session.q.call('quit'); session.p.wait(timeout=5)
                require(session.p.returncode == 0, 'QEMU exit')
            finally:
                session.close()
    print(f'{len(hashes)} distinct nesting images')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--link', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    return link_main(args)


if __name__ == '__main__':
    raise SystemExit(main())
