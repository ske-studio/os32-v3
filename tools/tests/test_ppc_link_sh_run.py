#!/usr/bin/env python3
"""PPC_P3_DESIGN §6 / A-6: independent acceptance meter and mutation anchors."""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/ppc'), str(ROOT/'tools')]
import link_sh_run as meter

CHECK = dict(name='ppc-link-sh-meter', env=['ppc-cross'], tier=1, budget=20, needs=['make:ppc-ci-image'],
    covers=['fs/serialfs/serialfs_persistent.c', 'tools/tests/test_ppc_link_sh_run.py','tools/ppc/link_sh_run.py','tools/ppc/run.py','tools/ppc/link_run.py','tools/ppc/kboot_accept.py',
            'tools/ppc/kboot_run.py','tools/ppc/usb_run.py','tools/ppc/sb_run.py',
            'tools/ppc_lan_host.py','tools/host_agent.py','tools/host_operator_socket.py',
            'tools/host_rshell.py','tools/ppc_link_transport.py','tools/remote_transport.py',
            'userland/shell/rshell_lan.c','kernel/console/con_sink.c','mm/kmalloc.c',
            'platform/powermac/shell_boot.c','kernel/console/console.c',
            'build/ppc-user.mk','build/targets/ppc-powermac.mk','kernel/console/fbcon.h',
            'kernel/console/console_backend.h','tools/*.py','tools/ppc/**',
            'platform/powermac/**','arch/ppc/**','platform/powermac/ppc_kernel.c'],
    basis=['PPC_P3_DESIGN v2 §6 and appendix A-6 T2/T3/T4'])


def rejected(fn, *args):
    try: fn(*args)
    except meter.CriterionFailure: return
    raise AssertionError('accepted invalid observation '+str(args))


def exercise(namespace):
    namespace['check_body']('echo hello', b'> echo hello\nhello\n\x04', b'hello\n')
    for body in (b'> echo hello\nhello\n', b'> echo hello\nhello\nhello\n\x04',
                 b'> echo hello\nKBOOT log\nhello\n\x04', b'> echo hello\n\x04'):
        rejected(namespace['check_body'],'echo hello',body,b'hello\n')
    namespace['check_heap'](1024*1024-1,0)
    for args in ((1024*1024,0),(0,0),(1024,1)):
        rejected(namespace['check_heap'],*args)
    with tempfile.TemporaryDirectory() as td:
        path=Path(td)/'map'
        path.write_text(' /build/platform/powermac/console.o\n'
                        ' /build/platform/powermac/hostfs_link_io.o\n'
                        ' /build/platform/powermac/hostfs_link_peer.o\n'
                        ' /build/platform/powermac/hostfs_boot.o\n')
        namespace['check_map'](path)
        for name in ('hostfs_io','hostfs_peer','lan_wire','lan_terminal','terminal_input'):
            path.write_text(' /build/platform/powermac/'+name+'.o\n')
            rejected(namespace['check_map'],path)


def main():
    exercise(vars(meter))
    for name,(source,old,new,_) in meter.MUTATIONS.items():
        text=(ROOT/source).read_text()
        assert text.count(old)==1,(name,old)
        with tempfile.TemporaryDirectory() as td:
            opts=meter.settings(Path(td),name)
            assert len([v for v in opts if v.startswith('KB_MUT_') and 'SRC=' in v])<=3
            assert any(v.startswith('PPC_SH_MUT_FILE=') for v in opts)
    # QMP shadow layout is an explicit PPC32 ABI assumption, pinned here.
    fb=(ROOT/'kernel/console/fbcon.h').read_text()
    assert 'struct fbcon_cell { u16 code; u8 attr, part; };' in fb
    assert 'struct fbcon_cell *cells, *drawn;\n    struct con_geometry geometry;' in fb
    assert 'struct con_geometry { int cols, rows; };' in (ROOT/'kernel/console/console_backend.h').read_text()
    if '--mutate' in sys.argv:
        source=Path(meter.__file__).read_text()
        for before,after in (
            ('body == b\'> \'+line.encode()+b\'\\n\'+expected+b\'\\x04\'', 'True'),
            ('0 < peak < 1024*1024 and failures == 0','0 < peak <= 1024*1024'),
            ("not re.search(r'[/ ]'+name+r'\\.o\\b', text)", 'True')):
            assert source.count(before)==1,before
            scope=dict(__file__=meter.__file__,__name__='mutant_meter')
            exec(compile(source.replace(before,after),meter.__file__,'exec'),scope)
            try: exercise(scope)
            except AssertionError: print('RED runtime meter:',before)
            else: raise AssertionError('SURVIVED meter '+before)
    print('PASS meter, FB layout, generated copy isolation and unique product anchors')
    return 0


def run(ctx):
    import subprocess
    return subprocess.call([sys.executable,'-B',__file__,*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)

if __name__=='__main__': sys.exit(main())
