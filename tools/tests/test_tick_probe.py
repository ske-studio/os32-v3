"""A1 real aggregation and boot call boundary, ILP32/qemu, runtime mutants."""
CHECK = dict(name='tick-probe-host', tier=4, needs=[], budget=5,
    covers=['sys/hal/sound/snd_wake_contract.h', 'sys/kern/time/tick_probe.c', 'sys/kern/time/tick_probe.h', 'sys/kern/time/tick_probe_ids.h',
            'sys/hal/net/link_platform.h', 'sys/platform/pc98/platform_link_inline.h', 'sys/platform/pc98/link_platform.c', 'sys/net/link.c', 'sys/net/nic.c', 'sys/dev/net/ne2000/ne2000.c',
            'sys/dev/net/ne2000/ne2000_data.inc',
            'sys/platform/pc98/kernel.c', 'tools/tests/tick_probe_host.c',
            'sys/arch/x86/kernel/isr_stub.asm', 'sys/kern/irq/tick.c',
            'sys/arch/x86/arch_io.h', 'sys/kern/kdebug/tick_probe.c',
            'userland/diag/tickprobe.c', 'build/kernel.mk',
            'build/kernel-objects.mk', 'build/select.mk', 'build/kdebug.mk',
            'sys/kern/module/debug_probe.c', 'sys/platform/pc98/kdebug/kdebug_main.c',
            'sys/platform/pc98/pit.c', 'sys/arch/x86/kernel/mtrr_wc.c',
            'sys/dev/net/e82557/e82557.c',
            'tools/tests/host32.py'],
    basis=['TICK_1KHZ_DESIGN', 'docs/decisions/sound/AV_HAL_DESIGN.md §13-3'],
)
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]

def main():
    original = (ROOT / 'sys/kern/time/tick_probe.c').read_text()
    kernel = (ROOT / 'sys/platform/pc98/kernel.c').read_text()
    begin = kernel.index('#ifdef TICK_PROBE\n    tick_probe_boot();')
    end = kernel.index('#endif', kernel.index('    tick_probe_splash(0);', begin))
    boundary = kernel[begin:end + len('#endif')] + '\n'
    link_source = (ROOT / 'sys/net/link.c').read_text()
    link = link_source[link_source.index('void link_tick(void)'):link_source.index('\n}', link_source.index('void link_tick(void)')) + 2]
    tick_source = (ROOT / 'sys/kern/irq/tick.c').read_text()
    tick = tick_source[tick_source.index('void tick_dispatch(void)'):]
    with tempfile.TemporaryDirectory(prefix='tick-probe-') as td:
        work = Path(td)
        def run(source, body, link_body=link, tick_body=tick):
            src = work / 'tick_probe.c'; src.write_text(source)
            inc = work / 'boundary.inc'; inc.write_text(body)
            link_inc = work / 'link.inc'; link_inc.write_text(link_body)
            tick_inc = work / 'tick.inc'; tick_inc.write_text(tick_body)
            exe = work / 'probe'
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall',
                '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin',
                '-fno-pie', '-no-pie', '-nostdlib', '-static',
                '-I' + str(ROOT / 'kernel/time'), '-DTP_SOURCE="' + str(src) + '"',
                '-DTP_BOUNDARY="' + str(inc) + '"', '-DTP_LINK="' + str(link_inc) + '"',
                '-DTP_TICK="' + str(tick_inc) + '"',
                str(ROOT / 'tools/tests/tick_probe_host.c'), '-o', str(exe)], check=True)
            return host32.run([str(exe)], runner='qemu').returncode
        assert run(original, boundary) == 0
        print('GREEN aggregate/histogram/p99/u64/splash returns/crossing/IF0/IRQ/short/lost', flush=True)
        if '--mutate' in sys.argv:
            budget_end = '    tick_probe_extra_end(TP_BUDGET);'
            assert tick.count(budget_end) == 1
            assert run(original, boundary, tick_body=tick.replace(budget_end, '')) == 1
            print('RED runtime exit=1 missing budget end', flush=True)
            hist_shift = 'id == TP_INTERVAL ? 8 : 0'
            assert original.count(hist_shift) == 2
            assert run(original.replace(hist_shift, '0'), boundary) == 1
            print('RED runtime exit=1 interval histogram upper bound', flush=True)
            mutants = [
                ('PCM IRQ10 enclosure', 'if (irq == 10) tick_probe_extra_end(TP_PCM_IRQ10);', '', False),
                ('sound outer span', 'if (sound_depth) sound_metric = metric;', '(void)metric;', False),
                ('fixture callback', '    s1_notify();', '    (void)now_ms;', False),
                ('rearm', '    snd_wake_arm(1000);', '    snd_wake_arm(0);', False),
                ('worst', 'if (d > s->worst) s->worst = d;', 's->worst = d;', False),
                ('histogram', 's->hist[bin]++;', 's->hist[0]++;', False),
                ('p99', 's->count - s->count / 100', 's->count', False),
                ('crossing', 'm.splash || splash || m.epoch != epoch;\n    add_stat_shift', 'm.splash || splash || 0;\n    add_stat_shift', False),
                ('total', 's->total += d;', 's->total += (unsigned int)d;', False),
                ('IF nesting', '(flags & (1U << 9)) && if0.valid', 'if0.valid && (flags || !flags)', False),
                ('entry mark', '    tick_probe_splash(1);', '    tick_probe_splash(0);', True),
                ('return mark', '    tick_probe_splash(0);', '    tick_probe_splash(1);', True),
            ]
            assert link.count('    tick_probe_net_end(TP_NET2);') == 1
            assert run(original, boundary, link.replace('    tick_probe_net_end(TP_NET2);', '')) == 1
            print('RED runtime exit=1 missing TP_NET2 end', flush=True)
            address = '(unsigned long)__builtin_return_address(0)'
            assert original.count(address) == 1
            assert run(original.replace(address, '0'), boundary) == 1
            print('RED runtime exit=1 zero caller', flush=True)
            for label, old, new, boot in mutants:
                text = boundary if boot else original
                assert text.count(old) == 1, (label, text.count(old))
                changed = text.replace(old, new)
                rc = run(original if boot else changed, changed if boot else boundary)
                assert rc == 1, (label, rc)
                print('RED runtime exit=1 ' + label, flush=True)

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_tick_probe.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
