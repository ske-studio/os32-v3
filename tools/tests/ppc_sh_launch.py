"""Exercise the actual shell launch with the PPC profile and a fake KAPI."""
CHECK = dict(name='ppc-sh-launch-host', tier=1,
    covers=['tools/tests/ppc_sh_launch.py', 'tools/tests/sh_status_host.c',
            'tools/tests/test_sh_status.py', 'userland/ppc/sh/profile.h',
            'userland/shell/sh_exec.inc', 'userland/shell/sh_launch.inc'],
    needs=[], budget=10)

import pathlib
import shutil
import subprocess
import sys
import tempfile

import host32
import test_sh_status as status

ROOT = pathlib.Path(__file__).resolve().parents[2]


def check_variant(mutant):
    with tempfile.TemporaryDirectory(prefix='ppc-sh-launch-') as name:
        tmp = pathlib.Path(name)
        shell = tmp / 'shell'
        shutil.copytree(ROOT / 'userland/shell', shell, ignore=shutil.ignore_patterns('*.o', '*.d'))
        if mutant:
            path = shell / 'sh_exec.inc'
            src = path.read_text()
            old = '    rc = g_api->exec_run(cmdline);\n    if (g_api->exec_last_result'
            assert src.count(old) == 1
            path.write_text(src.replace(old, '    rc = g_api->exec_run(cmdline);\n    g_api->kprintf(0, "%s", "interposed");\n    if (g_api->exec_last_result'))
        src = (ROOT / 'tools/tests/sh_status_host.c').read_text()
        src = src.replace('#include "../../userland/shell/main.c"', 'KernelAPI *sh_runtime_init(KernelAPI *api) { return api; }\n#include "../../userland/shell/main.c"')
        src = src.replace('../../userland/shell/', str(shell) + '/')
        src = src.replace('../../', str(ROOT) + '/')
        src = src.replace('static void __cdecl h_kprintf(u8 attr, const char *fmt, ...)\n{',
                          'static int launch_order;\nstatic void __cdecl h_kprintf(u8 attr, const char *fmt, ...)\n{\n    if (launch_order == 1) launch_order = -1;')
        src = src.replace('    exec_log(cmdline);\n    /* exec_run', '    launch_order = 1;\n    exec_log(cmdline);\n    /* exec_run')
        src = src.replace('static int __cdecl h_exec_last_result(int *kind, int *code)\n{',
                          'static int __cdecl h_exec_last_result(int *kind, int *code)\n{\n    if (launch_order == 1) launch_order = 2;')
        start = src.index('void _start(void)\n{')
        end = src.index('\n/* Caller tests mock enumeration', start)
        src = src[:start] + '''void _start(void)
{
    int kind, code;
    keep_windows(); build_api(); sh_boot(); fresh();
    prog_add("ok.bin", EXEC_KIND_EXITED, 7);
    (void)sh_exec_result("ok.bin", &kind, &code);
    check(launch_order == 2, "exec_run followed immediately by exec_last_result");
    check(kind == EXEC_KIND_EXITED && code == 7, "exit kind and code");
    check(st("ok.bin") == 7 && echoed("echo $?", "7"), "exit status");
    fresh(); prog_add("fault.bin", EXEC_KIND_FAULT, 0);
    check(st("fault.bin") == 139, "fault status");
    fresh(); out_reset(); sh_pipeline_enter();
    (void)sh_exec_result("ok.bin", &kind, &code); sh_pipeline_leave();
    check(out_has("sh: pipe to external command is not supported"), "pipe refusal");
    check(g_exec_calls == 0 && kind == EXEC_KIND_NOMEM, "pipe stops launch");
    fresh(); out_reset(); sh_pipeline_enter();
    (void)st("missing"); sh_pipeline_leave();
    check(g_exec_calls == 0 && out_has("pipe to external command"), "refusal stops PATH scan");
    fresh(); out_reset(); sh_redirect_mark();
    (void)sh_exec_result("ok.bin", &kind, &code); sh_redirect_clear();
    check(out_has("sh: redirect to external command is not supported"), "redirect refusal");
    check(g_exec_calls == 0 && kind == EXEC_KIND_NOMEM, "redirect stops launch");
    if (failures) { report("SOME FAIL\\n"); die(1); }
    report("ALL PASS\\n"); die(0);
}
''' + src[end:]
        source = tmp / 'host.c'
        source.write_text(src)
        shim = status.write_shims(tmp)
        exe = tmp / 'host'
        cmd = ['gcc', *status.BASE, '-Wextra', '-Werror', '-Wno-unused-function',
               '-Wno-unused-variable', '-Wno-unused-parameter', '-O0',
               '-include', str(ROOT / 'userland/ppc/sh/profile.h'), *shim,
               *status.INCLUDES, '-nostdlib', '-static', '-no-pie', str(source), '-o', str(exe)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True, text=True)
        print(result.stdout, end='')
        expected = '  FAIL exec_run followed immediately by exec_last_result\n'
        if mutant and (result.returncode != 1 or expected not in result.stdout or
                       result.stdout.count('  FAIL ') != 1 or
                       '  ok   redirect stops launch' not in result.stdout):
            raise RuntimeError('mutation failed at an unexpected point')
        return result.returncode


def main():
    if check_variant(False) != 0:
        return 1
    if '--mutate' in sys.argv:
        if check_variant(True) != 1:
            print('mutation did not produce the expected failure')
            return 1
        print('RED: interposed KAPI detected')
    return 0


if __name__ == '__main__':
    sys.exit(main())
