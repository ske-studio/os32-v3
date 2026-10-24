"""T7a-2d: real recorder/session/exec kill paths, modeled CPU and ROM.
No signal stack emulation: kernel #PF/#GP enter the real syscall-fault
handlers at explicit boundaries; modeled V86 runs return HOTKEY separately.
Hardware IRQ delivery, IRET/CR3 and visible restoration need NP21/W.
Normal controls always run before compiled runtime-RED omissions.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32


def function(text, name):
    match = re.search(r'^(?:static )?(?:void|int) ' + name + r'\([^)]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'


ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('kernel/v86_mem.c', '    if (v86_session.aborting && kdebug.ops.gcap_detach) kdebug.ops.gcap_detach();', '', 'detach'),
    ('kernel/v86_mem.c', '    if (v86_session.aborting && kdebug.ops.gcap_release) kdebug.ops.gcap_release();', '', 'release'),
    ('kdebug/v86_gcap.c', '    gcap_ops = 0;', '', 'ops'),
    ('kdebug/v86_gcap.c', '    g->restore = V86G_RST_FALLBACK;', '', 'interrupted-ROM-sync'),
    ('kdebug/v86_gcap.c', '    tv_restore(tv);', '    (void)tv;', 'TVRAM-tail'),
    ('kdebug/v86_gcap.c', '    if (v86_session.aborting) v86_io_reset_policy();', '', 'post-session-policy'),
)


def main():
    paths = ('kernel/v86_mem.c', 'kdebug/v86_gcap.c', 'exec/exec.c',
             'exec/v86_call.c', 'kernel/v86_gcap.c', 'kernel/v86_io.c',
             'kernel/isr_handlers.c')
    sources = {p: (ROOT / p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='v86-gcap-abort-') as td:
        work = Path(td)
        def run(parts, label=None):
            (work / 'session.c').write_text(parts[paths[0]])
            # The production import thunks are replaced by direct fixture edges;
            # the complete recorder and math still compile and execute.
            (work / 'recorder.c').write_text(parts[paths[1]].replace(
                '#include "generated/kdebug_import_macros.h"', '').replace(
                'int v86_gdc_capture(int mode, V86Gcap *out)',
                'int capture_body(int mode, V86Gcap *out)'))
            (work / 'facade.c').write_text(parts[paths[4]])
            (work / 'kill.inc').write_text(''.join(function(parts[paths[2]], n) for n in (
                'exec_exit', 'exec_pending_transfer', 'exec_pending_finish',
                'ring3_kill_kind', 'ring3_fault_kill', 'ring3_abort_kill',
                'ring3_abort_request', 'ring3_abort_check')))
            handlers = ''.join(function(parts[paths[6]], n).split(marker)[0] + '}\n'
                               for n, marker in (
                                   ('exception_handler', '    /* 画面上部クリア */'),
                                   ('page_fault_handler', '    /* 画面上部をクリア')))
            handlers = handlers.replace('    const char *name;\n    int row = 0;', '    (void)error_code;')
            handlers = handlers.replace('    int row = 0;  /* 画面最上部から表示 (最大限の情報量) */', '')
            (work / 'fault.inc').write_text(handlers)
            (work / 'cleanup.inc').write_text(next(line for line in parts[paths[3]].splitlines(keepends=True) if line.startswith('#define V86_CALL_MAX_PATHS ')) + function(parts[paths[3]], 'v86_call_cleanup'))
            (work / 'policy.inc').write_text(function(parts[paths[5]], 'v86_io_reset_policy'))
            exe = work / 'fixture'
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-array-bounds',
                   '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
                   '-fno-delete-null-pointer-checks', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(work)]
            cmd += ['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
                    'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'gfx', 'fs',
                    'drivers', 'sdk/include/os32', 'kdebug')]
            subprocess.run(cmd + [str(ROOT / 'tools/tests/v86_gcap_abort_host.c'),
                                 str(ROOT / 'kdebug/v86_gcap_math.c'), '-o', str(exe)], check=True)
            result = subprocess.run(['qemu-i386', '-B', '0x100000000', str(exe)],
                                    capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
            assert result.returncode == (1 if label else 0), (label, result)
            assert ('FAIL ' if label else 'GREEN ') in result.stdout, result
            print(('RED runtime ' + label + ': ' if label else '') + result.stdout.strip(), flush=True)
        run(sources)
        if '--mutate' in sys.argv:
            for path, old, new, label in MUTATIONS:
                assert old in sources[path], label
                parts = dict(sources)
                parts[path] = parts[path].replace(old, new)
                run(parts, label)
    return 0

if __name__ == '__main__':
    sys.exit(main())
