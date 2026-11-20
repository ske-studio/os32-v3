from pathlib import Path
import subprocess
import sys
import tempfile

SOURCE = 'platform/powermac/session_shell.c'
HARNESS = 'tools/tests/ppc_session_shell.c'
CHECK = dict(name='ppc-session-shell-host', tier=1, needs=[], budget=4,
             covers=['platform/powermac/session_shell.c', 'tools/tests/ppc_session_shell.c', 'tools/tests/host32.py', 'tools/tests/cc_cache.py',
                     'platform/powermac/platform_session_shell.h', 'include/*.h',
                     'exec/*.h', 'kernel/gui.h', 'arch/ppc/*.h', 'mm/*.h',
                     'fs/vfs_stdin.h', 'sdk/include/os32/*.h'],
             basis=['PPC_KAPI_GUI_SWITCH_DESIGN §2-2・確定 (段 5d)'])


def run(ctx):
    sys.path.insert(0, str(ctx.root / 'tools/tests'))
    import host32
    original = (ctx.root / SOURCE).read_text()
    cases = [('normal', original, 0)]
    if ctx.mutate:
        for name, old, new, expected in [
                ('consumer', '!consumer || ', '', 11),
                ('origin', ' || !ring3_call_from_user()', '', 13),
                ('parent', 'slot && slot->parent == APP_ID_SHELL', 'slot != 0', 15)]:
            if original.count(old) != 1:
                raise ValueError(f'{name}: mutation anchor changed')
            cases.append((name, original.replace(old, new), expected))
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / 'session_shell.c'
        exe = Path(tmp) / 'test'
        for name, code, expected in cases:
            source.write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-nostdinc',
                            '-nostdlib', '-static', '-no-pie', '-fno-pie', '-fno-stack-protector',
                            '-D__cdecl=', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude',
                            '-Iplatform/powermac', '-Iexec', '-Ikernel', '-Imm', '-Iarch/ppc',
                            '-Isdk/include/os32', source, HARNESS, '-o', exe], cwd=ctx.root, check=True)
            for runner in ctx.runners:
                result = host32.run([str(exe)], runner=runner)
                if result.returncode != expected:
                    raise RuntimeError(f'{name}: expected {expected}, got {result.returncode}')
            print(('RED ' if expected else 'OK ') + name, flush=True)
    return 0 if ctx.runners else 1
