"""Run the real IRQ0 branch as i386 NASM with host substitutes for I/O and frame."""
CHECK = dict(name='tick-irq-host', tier=4, needs=[], budget=3,
    covers=['arch/x86/kernel/isr_stub.asm', 'tools/tests/tick_irq_host.c',
            'tools/tests/test_tick_irq.py', 'tools/tests/host32.py'])
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def main(mutate=False):
    asm = (ROOT / 'arch/x86/kernel/isr_stub.asm').read_text()
    macro = asm[asm.index('%macro V86_REFLECT 1'):asm.index('%endmacro', asm.index('%macro V86_REFLECT 1')) + len('%endmacro')]
    start = asm.index('        call    tick_clock_step', asm.index('irq_stub_0:'))
    body = asm[start:asm.index('.done:', start)]
    mutants = [('.phys_only EOI', '        out     PIC1_CMD, al', '', True),
               ('physical V86 reflection', '        TP_BEGIN 9', '        V86_REFLECT 0\n        TP_BEGIN 9', True),
               ('timeout exit', '        call    v86_exit_to_kernel', '        nop', False)]
    with tempfile.TemporaryDirectory(prefix='tick-irq-') as td:
        work = Path(td)
        def verify(mutation=None):
            code = body
            if mutation:
                label, old, new, physical_only = mutation
                if physical_only:
                    prefix, physical = code.split('.phys_only:', 1)
                    assert physical.count(old) == 1, label
                    code = prefix + '.phys_only:' + physical.replace(old, new)
                else:
                    assert code.count(old) == 1, label
                    code = code.replace(old, new)
            code = code.replace('out     PIC1_CMD, al', 'call    eoi')
            source = ('BITS 32\nsection .text\nextern tick_clock_step, timer_handler, eoi, v86_reflect_irq\n'
                      'extern v86_tick_and_check_timeout, v86_exit_to_kernel, frame_flags\n'
                      '%define ISR_IRQ_EFLAGS_OFF 40\n%define EFLAGS_VM 0x20000\n'
                      '%define ABI_WORD_BYTES 4\n%define OCW2_EOI 0x20\n'
                      '%macro TP_BEGIN 1\n%endmacro\n%macro TP_END 1\n%endmacro\n'
                      '%macro TP_LEAVE 1\n%endmacro\n' + macro + '\n'
                      'global irq_body\nirq_body:\n' + code + '.done:\n ret\n')
            source = source.replace('[esp + ISR_IRQ_EFLAGS_OFF]', '[frame_flags]')
            src = work / 'irq.asm'; src.write_text(source)
            obj = work / 'irq.o'; exe = work / 'irq'
            subprocess.run(['nasm', '-f', 'elf32', str(src), '-o', str(obj)], check=True)
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', str(ROOT / 'tools/tests/tick_irq_host.c'),
                            str(obj), '-o', str(exe)], check=True, stderr=subprocess.PIPE)
            return host32.run([str(exe)], cwd=work).returncode
        assert verify() == 0
        print('GREEN IRQ0 EOI each physical IRQ; timer, V86 reflection and timeout only on jiffy')
        if mutate:
            for mutation in mutants:
                rc = verify(mutation)
                assert rc != 0, mutation[0]
                print('RED runtime', mutation[0], 'rc=', rc)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
