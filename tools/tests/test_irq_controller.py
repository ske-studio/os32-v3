"""IRQ controller contract: actual core with PC98 ports and a 64-line mock provider."""
CHECK = dict(name='irq-controller-host', tier=1,
    covers=['tools/tests/test_irq_controller.py', 'tools/tests/irq_controller_host.c',
            'kernel/irq/irq.c', 'kernel/irq/irq.h', 'kernel/irq/irq_math.c',
            'kernel/irq/irq_math.h', 'include/platform_irq.h', 'include/kernel_tick.h',
            'platform/pc98/platform_irq_limits.h', 'platform/powermac/platform_irq_limits.h',
            'platform/pc98/irq_policy.c', 'platform/pc98/irq_complete.c',
            'platform/pc98/pic.c', 'include/pc98.h', 'arch/x86/kernel/idt.h',
            'tools/tests/dep_dir_b_io_shim.h', 'tools/tests/ver_test_utils.py',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py'],
    needs=[], budget=3.6)
from pathlib import Path
import subprocess
import tempfile
import sys
import host32
from kernel_includes import include_flags
from ver_test_utils import ROOT, SUPPORT

SOURCES = ['kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'platform/pc98/irq_policy.c',
           'platform/pc98/irq_complete.c', 'platform/pc98/pic.c']
MUTATIONS = [
    ('kernel/irq/irq.c', '    if (claim.needs_finish) platform_irq_complete(&claim);\n    if (!handled &&',
     '    if (claim.needs_finish) { platform_irq_complete(&claim); platform_irq_complete(&claim); }\n    if (!handled &&', 16),
    ('platform/pc98/pic.c', 'outp(PIC2_CMD, OCW2_EOI);\n        outp(PIC1_CMD, OCW2_EOI);',
     'outp(PIC1_CMD, OCW2_EOI);\n        outp(PIC2_CMD, OCW2_EOI);', 16),
    ('platform/pc98/irq_policy.c', 'return slave_isr_bit7 ? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;',
     'return slave_isr_bit7 >= 0 ? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;', 16),
    ('platform/pc98/irq_complete.c', 'platform_irq_reserved(native_token))',
     '(platform_irq_reserved(native_token) && native_token != 7))', 16),
    ('kernel/irq/irq.c', 'return irq < 32 ? (u32)(1u << irq) : 0;',
     'return irq <= 32 ? (u32)(1u << (irq & 31)) : 0;', 64),
    ('kernel/irq/irq.c', 'return irq < 32 ? (u32)(1u << irq) : 0;',
     'return irq < 31 ? (u32)(1u << irq) : 0;', 64),
    ('platform/pc98/irq_policy.c', '3, 5, 6, 8, 9, 10, 11, 14, 15',
     '0, 5, 6, 8, 9, 10, 11, 14, 15', 16),
]


def execute(lines, mutation=None):
    with tempfile.TemporaryDirectory(prefix='irq-controller-') as directory:
        work = Path(directory)
        (work / 'support.h').write_text(SUPPORT)
        (work / 'io.h').write_text((ROOT / 'tools/tests/dep_dir_b_io_shim.h').read_text() + '\nvoid _stop(void);\n')
        if lines == 64:
            (work / 'platform_irq_limits.h').write_text('#define PLATFORM_IRQ_LINES 64\n#define PLATFORM_IRQ_DYNAMIC_COUNT 63\n')
        sources = SOURCES if lines == 16 else SOURCES[:2]
        copies = []
        for rel in sources:
            text = (ROOT / rel).read_text().replace('"../../include/platform_irq.h"', '"platform_irq.h"')
            if mutation and mutation[0] == rel:
                assert text.count(mutation[1]) == 1, mutation[1]
                text = text.replace(mutation[1], mutation[2])
            dest = work / Path(rel).name
            dest.write_text(text)
            copies.append(str(dest))
        exe = work / 'host'
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
               '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-ffreestanding',
               '-fno-builtin', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static',
               '-no-pie', '-D__cdecl=', '-I'+str(work), *include_flags(ROOT)]
        if lines == 64:
            cmd += ['-DMOCK64']
        host32.build(cmd + [str(ROOT / 'tools/tests/irq_controller_host.c'), *copies, '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=20)
        print(result.stdout, end='', flush=True)
        return result.returncode


def main():
    for lines in (16, 64):
        assert execute(lines) == 0
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS:
            assert execute(mutation[3], mutation) == 1, mutation
            print('RED runtime:', mutation[1], flush=True)


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(ROOT / 'tools/tests/test_irq_controller.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
