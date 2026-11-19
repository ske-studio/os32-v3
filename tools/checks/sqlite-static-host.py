from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = dict(name='sqlite-static-host', tier=1, needs=[], budget=3,
             covers=['kernel/sqlite_static.c', 'kernel/sqlite_engine*.h', 'lib/sqlite3/**',
                     'mm/*.h', 'include/*.h', 'tools/tests/sqlite_static_host.c',
                     'tools/tests/kernel_includes.py', 'tools/tests/host32.py'],
             basis=['PowerPC KAPI db 詳細設計 §2-2・§2-3 の表'])


def run(ctx):
    sys.path.insert(0, str(ctx.root / 'tools/tests'))
    import host32
    from kernel_includes import include_flags

    source = (ctx.root / 'kernel/sqlite_static.c').read_text()
    cases = [('boot', source)]
    if ctx.mutate:
        for anchor, replacement in [
            ('services_public = 1;', 'services_public = 0;'),
            ('sqlite_module_api = &sqlite_stub_api;', 'sqlite_module_api = &static_api;'),
            ('sqlite_engine_body_stop(static_api.shutdown)', 'SQLITE_STOP_DONE'),
            ('rc != SQLITE_BODY_INIT', 'rc == SQLITE_BODY_INIT'),
        ]:
            assert source.count(anchor) == 1
            cases.append((anchor, source.replace(anchor, replacement)))
    compiler_headers = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        for name, contents in cases:
            src = tmp / 'sqlite_static.c'
            src.write_text(contents)
            exe = tmp / 'fixture'
            subprocess.run(['gcc', '-m32', '-ffreestanding', '-nostdinc', '-nostdlib',
                            '-static', '-fno-pie', '-no-pie', '-isystem', compiler_headers,
                            '-Ilib/sqlite3', *include_flags(ctx.root), str(src),
                            'tools/tests/sqlite_static_host.c', '-o', str(exe)],
                           cwd=ctx.root, check=True)
            for runner in ctx.runners:
                rc = host32.run([str(exe)], runner=runner).returncode
                if rc != (0 if name == 'boot' else 1):
                    raise RuntimeError(f'{runner} {name}: rc={rc}')
                print('GREEN' if rc == 0 else 'RED', runner, name)
    return 0 if ctx.runners else 1
