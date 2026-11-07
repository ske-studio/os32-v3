"""CUI GeForce parser and the existing owner cleanup notification."""
CHECK = dict(name='gf-cui-host', tier=1, needs=[], budget=20.0,
    covers=['tools/tests/test_gf_cui.py', 'tools/tests/gf_cui_shell_host.c',
            'tools/tests/gf_command_runtime_host.h', 'tools/tests/host32.py',
            'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py',
            'userland/shell/cmd_gf2.c', 'kdebug/gf_command_wire.h',
            'hwdev/gf/*.h', 'hwdev/gf/display/*.h',
            'exec/appslot.c', 'include/types.h', 'sdk/include/os32/os32_kapi_shared.h'])
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('gpio-tag', 'userland/shell/cmd_gf2.c',
     'q.session |= GF5_WIRE_GPIO_TAG;', 'q.session |= 0;'),
    ('feature-skip', 'userland/shell/cmd_gf2.c',
     'if (q.test == GF5_FEATURE) {\n        int result',
     'if (false) {\n        int result'),
    ('enter-wire', 'userland/shell/cmd_gf2.c',
     '? GF4_CUI_ENTER : GF4_CUI_LEAVE', '? GF4_CUI_LEAVE : GF4_CUI_ENTER'),
    ('feature-wire', 'userland/shell/cmd_gf2.c',
     'if (q.test > GF5_FEATURE)', 'if (q.test >= GF5_FEATURE)'),
    ('operand-width', 'userland/shell/cmd_gf2.c',
     'operand[1] << GF5_WIRE_VALUE_SHIFT', 'operand[1] << GF5_WIRE_INDEX_SHIFT'),
    ('cleanup-notification', 'exec/appslot.c',
     'if (gf_display_owner_exit) gf_display_owner_exit(id);',
     'if (gf_display_owner_exit) gf_display_owner_exit(id + 1);'),
]

def run_case(root, directory):
    flags = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
             '-nostdlib', '-static', '-no-pie', *include_flags(root), '-I'+str(root)]
    shell = directory/'shell'
    subprocess.run([*flags, str(root/'tools/tests/gf_cui_shell_host.c'), '-o', str(shell)], check=True)
    result = host32.run([str(shell)], timeout=20, capture_output=True)
    if result.returncode:
        return result
    # Execute the shipped notification body with an observable GF callback.
    text = (root/'exec/appslot.c').read_text()
    body = text.split('void appslot_gfx_owner_exit(int id)\n', 1)[1].split('\n}\n', 1)[0]+'\n}\n'
    source = directory/'cleanup.c'
    source.write_text('''#include "tools/tests/gf_command_runtime_host.h"
static int calls, last_id, g_gfx_owner;
#define GFX_OWNER_WM 1
void gf_display_owner_exit(int id) { ++calls; last_id = id; }
void appslot_gfx_owner_exit(int id)
'''+body+'''
void _start(void) {
    g_gfx_owner=2;appslot_gfx_owner_exit(3);
    assert(calls==1 && last_id==3 && g_gfx_owner==2);
    appslot_gfx_owner_exit(2);
    assert(calls==2 && last_id==2 && g_gfx_owner==1);
    gf2_host_exit(0);
}
''')
    exe = directory/'cleanup'
    subprocess.run([*flags, '-Wno-address', str(source), '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=20, capture_output=True)

def run(ctx) -> int:
    """Check runner entry (tools/checks/run.py calls run(ctx))."""
    import os
    argv = ['python3', '-B', 'tools/tests/test_gf_cui.py']
    if ctx.mutate:
        argv.append('--mutate')
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='gf-cui-') as td:
        directory = Path(td)
        result = run_case(ROOT, directory)
        assert result.returncode == 0, result.stderr
        print('PASS CUI parser / owner notification runtime GREEN', flush=True)
        if '--mutate' in sys.argv:
            for name, rel, old, new in MUTATIONS:
                original = (ROOT/rel).read_text()
                assert original.count(old) == 1, name
                with tempfile.TemporaryDirectory(dir=td) as mt:
                    tree = mutpar.mutant_tree(ROOT, Path(mt)/'tree',
                        {rel: original.replace(old, new, 1)},
                        real=('tools/tests/gf_cui_shell_host.c', 'userland/shell/cmd_gf2.c'))
                    red = run_case(tree, directory)
                    assert red.returncode != 0, name
                    print(f'PASS {name}: runtime RED', flush=True)
            assert run_case(ROOT, directory).returncode == 0
            print('PASS restored runtime GREEN', flush=True)
