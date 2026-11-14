"""LOG request, retry, session loss, and bounded foreground progress."""
CHECK = dict(name='ppc-link-log-host', tier=2, budget=30, needs=[], covers=[
    'build/ppc-kboot.mk', 'platform/powermac/link_log.c',
    'tools/ppc/link_*.py', 'tools/host_*.py', 'tools/tests/test_ppc_link_log.py',
    'tools/tests/ppc_link_log_host.c','include/*.h', 'platform/powermac/*.h', 'arch/ppc/*.h',
            'net/*.h', 'mm/kmalloc.h', 'include/kprintf.h',
            'drivers/net/gem/gem.h', 'sdk/include/os32/*.h',
            'tools/tests/host32.py'])
from pathlib import Path
import subprocess
import tempfile
import host32
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from host_agent import HostAgent


def run(ctx):
    source = (ctx.root / 'platform/powermac/link_log.c').read_text()
    mutations = [
        ('if (!rc && status == 200)', 'if (!rc)'),
        ('p = decimal(p, length);', 'p = decimal(p, length + 1);')]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        cases = [('baseline', source)]
        if ctx.mutate:
            for old, new in mutations:
                assert source.count(old) == 1, old
                cases.append((old, source.replace(old, new)))
        for name, text in cases:
            (work / 'link_log.c').write_text(text)
            exe = work / 'probe'
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-fno-pie', '-fno-builtin', '-fno-stack-protector', '-ffunction-sections',
                   '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                   '-Wl,--gc-sections', '-I'+td]
            cmd += ['-I'+str(ctx.root / p) for p in
                    ['.', 'include', 'platform/powermac', 'arch/ppc', 'sdk/include',
                     'mm', 'lib']]
            subprocess.run(cmd + [str(ctx.root / 'tools/tests/ppc_link_log_host.c'),
                                  '-o', str(exe)], check=True)
            result = host32.run([str(exe)], runner='qemu', capture_output=True)
            rc = result.returncode
            if name == 'baseline':
                class Receiver:
                    operator_socket = None
                    def log(self, kind, record):
                        assert kind == 'LOG' and record['seq'] == 0 and record['text'] == b'abc'
                    def _answer(self, rid, ent, status, *args):
                        assert status == 200
                HostAgent._service_log(Receiver(), 1, {'got': b'abc'},
                                       result.stdout.decode().split(), None)
            assert rc == (0 if name == 'baseline' else 10), (name, rc)
            print(('GREEN ' if name == 'baseline' else 'RED runtime ') + name)
    return 0
