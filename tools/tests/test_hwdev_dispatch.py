"""M3b actual kdebug dispatch, absent module and external cleanup ordering."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import test_hwdev_module as init

ROOT = init.ROOT
MUTATIONS = [
    ('bypass-module', 'int rc = ops->call(op, buf, size, &recovery_ops, &recovery_transport, &recovery_s0);',
     'int rc = gfinit_call(op, buf, size, &recovery_ops, &recovery_transport, &recovery_s0);'),
    ('absent-fallback', 'if (!ops || recovery_attach()) return -1;',
     'if (!ops) return 0;\n        if (recovery_attach()) return -1;'),
    ('cleanup-before-recover', 'int rc = recover_now_session(r, out->session);',
     'const struct hwdev_ops *early = hwdev_module_ops();\n        if (early) early->release_work();\n        int rc = recover_now_session(r, out->session);'),
]


def run(folder, source):
    exe = folder/'dispatch'
    subprocess.run(['gcc', *init.FLAGS, *init.INCLUDES, '-DHWDEV_DISPATCH',
                    '-nostdlib', '-static', '-no-pie', str(ROOT/'tools/tests/kdebug_init_host.c'),
                    str(source), '-o', str(exe)], check=True)
    return host32.run([str(exe)], capture_output=True, timeout=30).returncode


def main():
    source = ROOT/'kdebug/kdebug_main.c'
    with tempfile.TemporaryDirectory(prefix='hwdev-dispatch-') as td:
        folder = Path(td)
        assert run(folder, source) == 0
        print('PASS dispatch op5-10, absent -1, S0 op3/4, PAUSED, recover then release')
        if '--mutate' in sys.argv:
            for name, old, new in MUTATIONS:
                text = source.read_text()
                assert text.count(old) == 1, name
                print('ANCHOR '+name+': 1')
                mutant = folder/'mutant.c'
                mutant.write_text(text.replace(old, new))
                assert run(folder, mutant) == 1, name
                print('MUTATE '+name+' RED')


if __name__ == '__main__':
    main()
