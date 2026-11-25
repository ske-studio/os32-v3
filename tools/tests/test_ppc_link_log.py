"""LOG request, retry, session loss, and bounded foreground progress."""
CHECK = dict(name='ppc-link-log-host', tier=2, budget=30, needs=[], covers=[
    'tools/lan_bridge.py', 'tools/lanfs_host.py', 'tools/serialfs_host.py',
    'build/targets/ppc-powermac.mk', 'sys/platform/powermac/link_log.c',
    'tools/ppc/link_*.py', 'tools/host_*.py', 'tools/tests/test_ppc_link_log.py',
    'tools/tests/ppc_link_log_host.c','include/*.h', 'platform/powermac/*.h', 'arch/ppc/*.h',
            'net/*.h', 'sys/kern/mm/kmalloc.h', 'sys/include/kprintf.h',
            'sys/dev/net/gem/gem.h', 'sdk/include/os32/*.h',
            'tools/tests/host32.py', 'tools/tests/ppc_link_pressure_host.c',
            'sys/platform/powermac/log.c', 'sys/platform/powermac/lan_log.c', 'sys/platform/powermac/escc.c'],
    basis=['LAN_OPERATOR_DESIGN §5'],
)
from pathlib import Path
import subprocess
import tempfile
import host32
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from host_agent import HostAgent


def run(ctx):
    source = (ctx.root / 'sys/platform/powermac/link_log.c').read_text()
    mutations = [
        ('if (!rc && status == 200)', 'if (!rc)'),
        ('status >= 400 && status < 500', 'status >= 500 && status < 500'),
        ('lost_bytes = sum(lost_bytes, length);\n            pm_lan_log_complete',
         'lost_bytes = lost_bytes;\n            pm_lan_log_complete'),
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
            cmd = ['gcc', '-m32', '-nostdinc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
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
        pressure = ['platform/powermac/'+name for name in ('link_log.c', 'log.c', 'lan_log.c', 'escc.c')]
        subprocess.run(cmd + pressure + ['tools/tests/ppc_link_pressure_host.c', '-o', str(exe)],
                       cwd=ctx.root, check=True)
        assert host32.run([str(exe)], runner='qemu').returncode == 0
        print('GREEN ESCC delivery, LAN overflow and foreground recovery')
    return 0
