"""Run actual front/back fixture logic; only KAPI/allocator transport is mocked."""
import os
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


MUTANTS = [
    ('c-stop-focus-missing', 'if (stop_focus) {', 'if (0) {',
     '!stop_focus || (focus_calls == 1 && focus_printed == 1)'),
    ('c-stop-focus-error-ignored', 'if (rc != 0) {', 'if (0) {', '!focus_rc'),
]


def run(mutate=False):
    cc = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
    with tempfile.TemporaryDirectory(prefix='trim-fixtures-') as directory:
        tmp = Path(directory)
        for name in ('front', 'back'):
            (tmp / f'trim_{name}_source.c').write_text(
                (ROOT / f'userland/tests/trim_{name}.c').read_text())
        for name in ('front', 'back'):
            exe = tmp / name
            args = [str(cc), '-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin',
                    '-ffunction-sections', '-fdata-sections', '-nostdlib',
                    '-Wl,--gc-sections', '-Wl,-e,_start', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(tmp), '-Iinclude', '-Isdk/include/os32', '-Isdk/allocator']
            if name == 'back':
                args += ['-DTEST_BACK']
            subprocess.run(args + ['tools/tests/trim_fixtures_host.c', '-o', str(exe)],
                           cwd=ROOT, check=True)
            host32.run([str(exe)], check=True, timeout=30)
            print('PASS actual trim_' + name + ' fixture', flush=True)
            if mutate and name == 'back':
                original = (ROOT / 'userland/tests/trim_back.c').read_text()
                for label, old, new, expected in MUTANTS:
                    assert original.count(old) == 1, label
                    (tmp / 'trim_back_source.c').write_text(original.replace(old, new))
                    subprocess.run(args + ['tools/tests/trim_fixtures_host.c', '-o', str(exe)], cwd=ROOT, check=True)
                    result = host32.run([str(exe)], capture_output=True, text=True, timeout=30)
                    assert result.returncode == 1 and 'FAIL: ' + expected in result.stdout.splitlines(), (label, result)
                    print('RED: ' + label + ' rc=1 -> FAIL: ' + expected, flush=True)


if __name__ == '__main__':
    run()
