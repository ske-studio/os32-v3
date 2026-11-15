#!/usr/bin/env python3
"""LAN_OPERATOR_DESIGN P5-sd: host verdicts and private-copy anchors."""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/ppc'), str(ROOT/'tools')]
import sd_run as meter
import link_sh_run as link

CHECK = dict(name='ppc-sd-meter', env=['ppc-cross'], tier=1, budget=10, needs=['make:ppc-ci-image'],
    covers=[
        'arch/ppc/entry.S',
        'arch/ppc/kernel_dec.c',
        'arch/ppc/kernel_mmu.S',
        'arch/ppc/kernel_probe.S',
        'arch/ppc/kernel_trap.c',
        'arch/ppc/ofw_note.S',
        'init/ppc_kernel.c',
        'kernel/console/console.c',
        'platform/powermac/boot.c',
        'platform/powermac/clock.c',
        'platform/powermac/console_stream.c',
        'platform/powermac/escc.c',
        'platform/powermac/escc_tree.c',
        'platform/powermac/input_boot.c',
        'platform/powermac/mpic.c',
        'platform/powermac/ofw.c',
        'platform/powermac/shell_boot.c',
        'platform/powermac/usb_boot.c',
        'platform/powermac/usb_platform.c',
        'tools/tests/test_ppc_sd_run.py',
        'tools/ppc/sd_run.py',
            'tools/ppc/**', 'tools/*.py', 'userland/shell/rshell_lan.c',
            'kernel/console/con_sink.c', 'mm/kmalloc.c',
            'fs/serialfs/serialfs_persistent.c'],
    basis=['LAN_OPERATOR_DESIGN §5 P5-sd/fs'])


def exercise(scope):
    scope['link_observation'](b'x\n', b'x\n', 'persisted')
    for wrong in (b'', b'x', b'x\nx\n', b'boot1\n'):
        try: scope['link_observation'](wrong, b'x\n', 'persisted')
        except link.CriterionFailure as exc: assert str(exc) == 'persisted'
        else: raise AssertionError('accepted corrupt persistence')
    try: scope['result']('cat /host/f\nx\nmissing', 'cat /host/f')
    except AssertionError: pass
    else: raise AssertionError('accepted missing prompt')
    scope['link_absence'](b'> cat /host/rw/f\ncat: /host/rw/f: Host is down\n\x04', 1100)
    for body, ticks in ((b'x\n', 1100), (b'cat: /host/rw/f: error', 5001),
                        (b'cat: /host/rw/f: error', 0)):
        try: scope['link_absence'](body, ticks)
        except link.CriterionFailure: pass
        else: raise AssertionError('accepted unbounded/successful absence')
    import serialfs_host as sfs
    req = sfs.encode(sfs.T_WRITE, 1, 1, bytes(1028), sfs.LAN_MAX_PAYLOAD)
    scope['link_large']([(req, bytes(1044))])
    for calls in ([], [(req, bytes(1043))], [(sfs.encode(sfs.T_READ, 1, 1, b''), bytes(1044))]):
        try: scope['link_large'](calls)
        except link.CriterionFailure: pass
        else: raise AssertionError('accepted small/missing transfer')
    scope['exact']('cat /host/f\nx\nsh> ', 'cat /host/f', 'x\n')
    for text in ('cat /host/f\nx\n', 'cat /host/g\nx\nsh> ', 'cat /host/f\ny\nsh> '):
        try: scope['exact'](text, 'cat /host/f', 'x\n')
        except AssertionError: pass
        else: raise AssertionError('accepted incomplete/wrong PLN1 result')


def main():
    exercise(vars(meter))
    # Check actual launch arguments without starting QEMU or opening sockets.
    import inspect
    from unittest.mock import patch
    import usb_run
    assert link.Session.qemu_clock == usb_run.USB_QEMU_CLOCK
    scope = dict(link=link)
    source = inspect.getsource(meter.link_accept)
    import ast
    tree = ast.parse(source)
    cls = next(node for node in tree.body[0].body if isinstance(node, ast.ClassDef))
    exec(compile(ast.fix_missing_locations(ast.Module(body=[cls], type_ignores=[])),
                 '<LinkSession>', 'exec'), scope)
    for session, expected in ((link.Session, True), (scope['LinkSession'], False)):
        obj = session.__new__(session)
        obj.elf, obj.cpu = Path(__file__), '7450'
        with tempfile.TemporaryDirectory() as folder:
            obj.out = Path(folder)
            with patch.object(link.socket, 'socket') as sock, patch.object(link, 'UDP') as udp, \
                 patch.object(link, 'command'), patch.object(link.subprocess, 'Popen') as popen, \
                 patch.object(link, 'PumpQMP'):
                sock.return_value.__enter__.return_value.getsockname.return_value = ('127.0.0.1', 1)
                udp.return_value.sock.getsockname.return_value = ('127.0.0.1', 2)
                obj.deadline = 0
                obj.start()
                args = popen.call_args.args[0]
                assert ('-icount' in args) == expected
                obj.err.close(); obj.frames.close()
    path, before, after = meter.LINK_NONCE
    assert (ROOT/path).read_text().count(before) == 1
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        for mutate in (False, True):
            out = work/str(mutate); out.mkdir()
            opts = meter.link_settings(out, mutate)
            user = Path(next(v.split('=', 1)[1] for v in opts if v.startswith('PPC_SH_MUT_FILE=')))
            text = user.read_text()
            for command in ('p5-hold', 'p5-stale', 'p5-copy'):
                assert text.count('"'+command+'"') == 1
            assert 'OS32_ERR_STALE' in text and 'char buf[4096]' in text
            assert len([v for v in opts if v.startswith('KB_MUT_') and 'SRC=' in v]) == (3 if mutate else 2)
            if mutate:
                copy = Path(next(v.split('=', 1)[1] for v in opts if v.startswith('KB_MUT_THIRD_FILE=')))
                assert copy.read_text() == (ROOT/path).read_text().replace(before, after)
    from kboot_accept import mutant_settings
    with tempfile.TemporaryDirectory() as folder:
        opts = mutant_settings(Path(folder), 'nonce', *meter.LINK_NONCE)
        copy = Path(next(v.split('=', 1)[1] for v in opts if v.startswith('KB_MUT_FILE=')))
        assert copy.read_text() == (ROOT/path).read_text().replace(before, after)
    # PLN1 has two protections (host fs.reset on seq rollback, guest per-boot
    # nonce); its no-boot-follow mutant removes both, so both must be present.
    text = Path(meter.__file__).read_text()
    assert 's.host.fs.reset = lambda' in text and "'nonce = 0; (void)token;'" in text
    if '--mutate' in sys.argv:
        source = Path(meter.__file__).read_text()
        for old, new in (('data == expected', 'True'), ('actual == expected', 'True'),
                         ("text.endswith('sh> ')", 'True'), ('newline and echo == cmd', 'True'),
                         ("b'cat: /host/rw/f: ' in body and 0 < ticks <= 5000", 'True'),
                         ('len(req) >= 1030 and sfs.try_frame(req, 0, sfs.LAN_MAX_PAYLOAD)[2].type == sfs.T_WRITE', 'True'),
                         ('len(reply) == 1044', 'True')):
            assert source.count(old) == 1, old
            scope = dict(__name__='sd_mutant', __file__=meter.__file__)
            exec(compile(source.replace(old, new), meter.__file__, 'exec'), scope)
            try: exercise(scope)
            except AssertionError: print('RED runtime meter: '+old)
            else: raise AssertionError('SURVIVED '+old)
    print('PASS S-D verdicts, unique anchors and isolated product/USER copies')
    return 0


def run(ctx):
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__': sys.exit(main())
