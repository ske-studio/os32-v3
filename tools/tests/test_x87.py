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
               '-I' + str(ROOT / 'arch/x86/kernel'), '-I' + str(ROOT / 'include'),
               '-I' + str(ROOT / 'arch/x86'), str(tmp / 'x87.c'),
               str(ROOT / 'tools/tests/x87_host.c'), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stderr)
        return host32.run([str(exe)], capture_output=True, timeout=30).returncode


def boundary():
    """The common exec code names no x87: the save area and its three entry points
    are arch-neutral (include/exec_arch.h), and both arch providers size them."""
    for name in ('exec/appslot.h', 'exec/exec.c', 'exec/exec_launch.inc',
                 'exec/exec_schedule.inc', 'exec/exec_syscall.inc'):
        text = (ROOT / name).read_text()
        assert 'x87' not in text and 'X87' not in text, name
    contract = (ROOT / 'include/exec_arch.h').read_text()
    for word in ('struct arch_fp_context', 'void arch_fp_save(struct arch_fp_context *);',
                 'void arch_fp_restore(const struct arch_fp_context *);', 'void arch_fp_init(void);'):
        assert word in contract, word
    x86 = (ROOT / 'arch/x86/arch_exec_storage.h').read_text()
    assert '#define ARCH_FP_BYTES 108\n#define ARCH_FP_ALIGN 1\n' in x86
    ppc = (ROOT / 'arch/ppc/arch_exec_storage.h').read_text()
    assert '#define ARCH_FP_BYTES ' in ppc and '#define ARCH_USER_BYTES 176\n' in ppc


def main():
    source = SOURCE.read_text()
    boundary()
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
