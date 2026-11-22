"""Run the real IRQ0 branch as i386 NASM with host substitutes for I/O and frame."""
CHECK = dict(name='tick-irq-host', tier=4, needs=[], budget=3,
    covers=['kernel/time/tick.c', 'kernel/time/tick_clock.h', 'kernel/time/tick_probe_ids.h', 'include/kernel_tick.h',
            'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/kernel_includes.py', 'arch/x86/kernel/isr_stub.asm', 'tools/tests/tick_irq_host.c',
            'tools/tests/test_tick_irq.py', 'tools/tests/host32.py'],
    basis=['TICK_1KHZ_DESIGN', 'TICK_ARCH_DESIGN §5-3'],
)
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def main(mutate=False):
    asm = (ROOT / 'arch/x86/kernel/isr_stub.asm').read_text()
    macro = asm[asm.index('%macro V86_REFLECT 1'):asm.index('%endmacro', asm.index('%macro V86_REFLECT 1')) + len('%endmacro')]
    start = asm.index('        call    tick_clock_step', asm.index('irq_stub_0:'))
    body = asm[start:asm.index('.done:', start)]
    mutants = [('.phys_only EOI', '        out     PIC1_CMD, al', '', True),
               ('physical V86 reflection', '        TP_BEGIN TP_EOI', '        V86_REFLECT 0\n        TP_BEGIN TP_EOI', True),
               ('physical EOI probe', 'TP_END TP_EOI', 'TP_END TP_SLOT7', True),
               ('logical reflection probe', 'TP_BEGIN TP_REFLECT', 'TP_BEGIN TP_EOI', False),
               ('timer after EOI', 'call    timer_handler', 'nop', False),
               ('timeout exit', '        call    v86_exit_to_kernel', '        nop', False)]
    with tempfile.TemporaryDirectory(prefix='tick-irq-') as td:
        work = Path(td)
        def verify(mutation=None, late_path=None):
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
                    if label == 'timer after EOI':
                        code = code.replace('out     PIC1_CMD, al',
                                            'out     PIC1_CMD, al\n        call    timer_handler', 1)
            code = code.replace('out     PIC1_CMD, al', 'call    eoi')
            source = ('BITS 32\nsection .text\nextern tick_clock_step, timer_handler, eoi, v86_reflect_irq\n'
                      'extern v86_tick_and_check_timeout, v86_exit_to_kernel, frame_flags\n'
                      '%define ISR_IRQ_EFLAGS_OFF 40\n%define EFLAGS_VM 0x20000\n'
                      '%define ABI_WORD_BYTES 4\n%define OCW2_EOI 0x20\n'
                      'extern probe_begin, probe_end\n'
                      '%macro TP_BEGIN 1\n pushad\n push dword %1\n call probe_begin\n add esp,4\n popad\n%endmacro\n'
                      '%macro TP_END 1\n pushad\n push dword %1\n call probe_end\n add esp,4\n popad\n%endmacro\n'
                      '%macro TP_LEAVE 1\n%endmacro\n' + macro + '\n'
                      'global irq_body\nirq_body:\n' + code + '.done:\n ret\n')
            clock = (ROOT / 'kernel/time/tick.c').read_text()
            if late_path is not None:
                # Defer the real notification to just after the selected EOI.
                anchor = 'return notify_step(result);'
                assert clock.count(anchor) == 1
                clock = ('static unsigned int deferred;\n' + clock.replace(anchor,
                         '{ if (result == ' + str(late_path) + ') deferred = 1;\n'
                         '        else return notify_step(result); }') +
                         '\nvoid flush_notify(void) { if (deferred) { deferred = 0; notify_step(0); } }\n')
                source = 'extern flush_notify\n' + source.replace('call    eoi', 'call    eoi\n        call    flush_notify')
            clock_src = work / 'tick.c'
            clock_src.write_text(clock)
            source = source.replace('[esp + ISR_IRQ_EFLAGS_OFF]', '[frame_flags]')
            src = work / 'irq.asm'; src.write_text(subprocess.check_output(
                ['cpp', '-traditional-cpp', '-P', '-include', str(ROOT / 'kernel/time/tick_probe_ids.h'), '-'],
                input=source, text=True))
            obj = work / 'irq.o'; exe = work / 'irq'
            subprocess.run(['nasm', '-f', 'elf32', str(src), '-o', str(obj)], check=True)
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', '-D__cdecl=', *include_flags(ROOT),
                            str(clock_src), str(ROOT / 'tools/tests/tick_irq_host.c'),
                            str(obj), '-o', str(exe)], check=True)
            return host32.run([str(exe)], cwd=work).returncode
        assert verify() == 0
        print('GREEN IRQ0 EOI each physical IRQ; timer, V86 reflection and timeout only on jiffy')
        if mutate:
            for path, name in enumerate(('physical', 'jiffy')):
                rc = verify(late_path=path)
                assert rc == 42, (name, rc)
                print('RED runtime VM=1 notification after EOI', name, 'rc=', rc)
            for mutation in mutants:
                rc = verify(mutation)
                assert rc != 0, mutation[0]
                print('RED runtime', mutation[0], 'rc=', rc)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
