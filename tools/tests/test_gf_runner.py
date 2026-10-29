"""P2 runner versus the retained S1 plan/executor, plus bounded controls."""
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='gf-runner-') as td:
        exe = Path(td) / 'host'
        subprocess.run([
            'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Werror=vla',
            '-m32', '-march=i386', '-O2', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            *['-I' + str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32')],
            str(ROOT / 'tools/tests/gf_runner_host.c'),
            *map(str, sorted((ROOT / 'hwdev/gf/op').glob('*.c'))), '-o', str(exe)
        ], cwd=ROOT, check=True)
        return host32.run([str(exe)], cwd=ROOT, timeout=120).returncode


if __name__ == '__main__':
    raise SystemExit(main())
