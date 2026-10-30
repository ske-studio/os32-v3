"""T7a-2d: real recorder/session/exec kill paths, modeled CPU and ROM.
No signal stack emulation: kernel #PF/#GP enter the real syscall-fault
handlers at explicit boundaries; modeled V86 runs return HOTKEY separately.
Hardware IRQ delivery, IRET/CR3 and visible restoration need NP21/W.
Normal controls always run before compiled runtime-RED omissions.
"""
CHECK = dict(name='v86-gcap-abort-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'arch/x86/kernel/isr_handlers.c', 'kernel/v86/v86_mem.*', 'kernel/v86/v86.c', 'kernel/v86/v86_gp_emulate.inc', 'kernel/v86/v86.h', 'kernel/v86/v86_gcap.*', 'kernel/v86/v86_io.*', 'kernel/module/module.h', 'exec/v86_call.c', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_kapi_init.inc', 'exec/appslot.h', 'kdebug/v86_gcap.c', 'kdebug/v86_gcap_math.c', 'kdebug/v86_gcap_diag.h', 'kernel/v86/v86_gcap_math.h', 'include/**', 'arch/x86/**', 'platform/pc98/**', 'tools/tests/host_arch/**', 'tools/tests/host32.py', 'tools/tests/v86_gcap_abort_host.c', 'tools/tests/v86_gp_host.inc', 'tools/check_map.d/check-v86-gcap-abort-host.yaml', 'build/checks.d/check-v86-gcap-abort-host.mk', 'include/platform_emergency.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.256406)

from kernel_includes import include_flags
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32


def function(text, name):
    match = re.search(r'^(?:static )?(?:void|int|u8 \*)\s*' + name + r'\([^)]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'


ROOT = Path(__file__).resolve().parents[2]
GP_GATE = '''    if (kdebug.ops.gcap_insn_abort && kdebug.ops.gcap_insn_abort(opsize16, pc[len])) {
        v86_exit_reason = V86_EXIT_UNKNOWN_OP;
        return 1;
    }

    return v86_gp_emulate(frame, pc, len);'''
MUTATIONS = (
    ('kernel/v86/v86_mem.c', '    if (v86_session.aborting && kdebug.ops.gcap_detach) kdebug.ops.gcap_detach();', '', 'detach'),
    ('kernel/v86/v86_mem.c', '    if (v86_session.aborting && kdebug.ops.gcap_release) kdebug.ops.gcap_release();', '', 'release'),
    ('kdebug/v86_gcap.c', 'void v86_gcap_detach(void)\n{\n    v86_gcap_rec = 0;\n    gcap_ops = 0;',
     'void v86_gcap_detach(void)\n{\n    v86_gcap_rec = 0;', 'ops'),
    ('kdebug/v86_gcap.c', '    g->restore = V86G_RST_FALLBACK;', '', 'interrupted-ROM-sync'),
    ('kdebug/v86_gcap.c', '    tv_restore(tv);', '    (void)tv;', 'TVRAM-tail'),
    ('kdebug/v86_gcap.c', '    if (v86_session.aborting) v86_io_reset_policy();', '', 'post-session-policy'),
    ('kernel/v86/v86.c', 'return v86_gp_emulate(frame, pc, len);',
     'return v86_gp_emulate(frame, pc, 0);', 'GP-prefix-length-handoff'),
    ('kernel/v86/v86.c', 'return v86_gp_emulate(frame, pc, len);',
     'return v86_gp_emulate(frame, pc + len, len);', 'GP-pc-handoff'),
    ('kernel/v86/v86.c', GP_GATE,
     '    int result = v86_gp_emulate(frame, pc, len);\n' +
     GP_GATE.replace('return v86_gp_emulate(frame, pc, len);', 'return result;'),
     'GP-capture-after-dispatch'),
    ('kernel/v86/v86_gp_emulate.inc',
     'v86_set_al(frame, v86_io_in(port, 1));\n        v86_advance_ip(frame, len + 2);',
     'v86_set_al(frame, v86_io_in(port, 1));\n        v86_advance_ip(frame, len + 1);',
     'GP-immediate-IP-increment'),
    ('kernel/v86/v86_gp_emulate.inc', 'if (!v86_inject_int(frame, vec)) {',
     'if (v86_inject_int(frame, vec)) {', 'GP-INT-failure'),
    ('kernel/v86/v86_gp_emulate.inc', 'v86_exit_reason = V86_EXIT_HLT;\n        return 1;',
     'v86_exit_reason = V86_EXIT_HLT;\n        return 0;', 'GP-HLT-return'),
)


def main():
    paths = ('kernel/v86/v86_mem.c', 'kdebug/v86_gcap.c', 'exec/exec.c',
             'exec/v86_call.c', 'kernel/v86/v86_gcap.c', 'kernel/v86/v86_io.c',
             'arch/x86/kernel/isr_handlers.c', 'kernel/v86/v86.c',
             'kernel/v86/v86_gp_emulate.inc')
    sources = {p: (ROOT / p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='v86-gcap-abort-') as td:
        work = Path(td)
        def run(parts, label=None):
            gp = parts['kernel/v86/v86.c']
            (work / 'gp.inc').write_text(
                ''.join(line for line in gp.splitlines(keepends=True)
                        if line.startswith(('#define V86_FETCH_BYTES ', '#define V86_PREFIX_MAX '))) +
                ''.join(function(gp, n) for n in (
                    'v86_ptr', 'v86_advance_ip', 'v86_set_al', 'v86_set_ax')) +
                parts['kernel/v86/v86_gp_emulate.inc'] + function(gp, 'v86_gp_handler'))
            for rel in ('include/platform_emergency.h', 'platform/pc98/emergency.c'):
                (work / Path(rel).name).write_text((ROOT / rel).read_text())
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
            cmd += (['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'kdebug')] + include_flags(ROOT))
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
                assert sources[path].count(old) == 1, label
                parts = dict(sources)
                parts[path] = parts[path].replace(old, new)
                run(parts, label)
    return 0

if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_v86_gcap_abort.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
