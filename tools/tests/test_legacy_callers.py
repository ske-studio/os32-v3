"""Optional legacy provider links and genuine caller admission/unwind slices."""
CHECK = dict(name='legacy-callers-host', tier=1,
    covers=['include/legacy_guest.h', 'arch/x86/**', 'platform/pc98/**', 'exec/*.h', 'mm/*.h',
            'kernel/v86/*.h', 'kernel/module/*.h', 'platform/none/legacy_guest.c',
            'exec/exec_*.inc', 'exec/lease.c', 'kernel/kaudit.c', 'kernel/console/console.c',
            'tools/tests/legacy_callers_host.c', 'tools/tests/legacy_guest_fake.h',
            'tools/tests/test_legacy_callers.py', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py', 'include/**', 'sdk/include/os32/**'],
    needs=[], budget=8)
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    import re
    m = re.search(r'(?m)^(?:static )?(?:int|void|i32) ' + name + r'\(', source)
    assert m, name
    start = source.index('{', m.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'


def sources():
    read = lambda p: (ROOT / p).read_text()
    # Preserve the complete admission prefix of the real launch function;
    # image I/O after admission is covered by exec_image, not mocked here.
    launch = function(read('exec/exec_launch.inc'), 'exec_launch')
    launch = launch[:launch.index('    /* longjmp')] + '    (void)cmdline; (void)gui_arg; return 0;\n}\n'
    return dict(callers=launch + function(read('exec/exec_finish.inc'), 'exec_exit') +
        '\n'.join(function(read('kernel/console/console.c'), n) for n in
                  ['console_render_allowed', 'console_hw_cursor_sync',
                   'console_hw_cursor_enable', 'console_text_gdc_stop']) +
        function(read('exec/lease.c'), 'lease_audit_all') +
        function(read('kernel/kaudit.c'), 'kselftest_run_audit'),
        on=read('arch/x86/legacy/guest.c'), off=read('platform/none/legacy_guest.c'))

MUTANTS = [
    ('on', 'return v86_session.open;', 'return v86_is_active();', 'open-independent'),
    ('off', 'int legacy_session_open(void) { return 0; }',
     'int legacy_session_open(void) { return 1; }', 'none-open'),
    ('callers', 'if (legacy_pinned()) return EXEC_ERR_INVALID;',
     'if (0) return EXEC_ERR_INVALID;', 'pin-ignored'),
    ('callers', 'if (legacy_closing())', 'if (0)', 'closing-ignored'),
    ('callers', '    legacy_end();\n    ring3_context_clear();\n    legacy_cleanup();',
     '    legacy_cleanup();\n    ring3_context_clear();\n    legacy_end();', 'cleanup-before-end'),
    ('off', '(void)fn; (void)args; (void)bytes;',
     '(void)fn; (void)bytes; (void)*(volatile const u32 *)args;', 'none-reads-args'),
]


def main():
    original = sources()
    with tempfile.TemporaryDirectory(prefix='legacy-callers-') as td:
        work = Path(td)
        def run(parts, red=False):
            for key, body in parts.items():
                (work / (key + '.inc')).write_text(body)
            exe = work / 'entry'
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                '-D__cdecl=', '-ffunction-sections', '-fdata-sections',
                '-I' + str(work), *include_flags(ROOT), '-nostdlib', '-static',
                '-no-pie', '-Wl,--gc-sections', ROOT / 'tools/tests/legacy_callers_host.c',
                '-o', exe], check=True)
            result = host32.run([str(exe)], runner='qemu', capture_output=True,
                                text=True, timeout=host32.RUN_TIMEOUT)
            if red:
                assert result.returncode == 1, result
            else:
                assert result.returncode == 0 and 'GREEN' in result.stdout, result
            print(('RED runtime' if red else 'GREEN on/off callers'), flush=True)
        run(original)
        if '--mutate' in sys.argv:
            for key, old, new, label in MUTANTS:
                assert original[key].count(old) == 1, label
                changed = dict(original)
                changed[key] = changed[key].replace(old, new)
                run(changed, True)
                print(label, flush=True)
    return 0


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_legacy_callers.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    sys.exit(main())
