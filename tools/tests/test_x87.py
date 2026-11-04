"""Run the real x87 instructions without libc, including two nesting levels."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/x86/kernel/x87.c'
MUTATIONS = (
    ('save-omitted', 'fnsave %0; fwait', 'fninit'),
    ('restore-omitted', 'frstor %0', 'fninit'),
)


def run(source):
    with tempfile.TemporaryDirectory(prefix='x87-') as name:
        tmp = Path(name)
        (tmp / 'x87.c').write_text(source)
        exe = tmp / 'test'
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
               '-I' + str(ROOT / 'arch/x86/kernel'), str(tmp / 'x87.c'),
               str(ROOT / 'tools/tests/x87_host.c'), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stderr)
        return host32.run([str(exe)], capture_output=True, timeout=30).returncode


def main():
    source = SOURCE.read_text()
    assert run(source) == 0, 'control failed'
    print('x87 nesting and park: GREEN')
    if '--mutate' in sys.argv:
        for name, before, after in MUTATIONS:
            assert source.count(before) == 1
            rc = run(source.replace(before, after, 1))
            assert rc != 0, name + ' survived'
            print(name + ': runtime RED rc=' + str(rc))


if __name__ == '__main__':
    main()
