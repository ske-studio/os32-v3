"""A1 real aggregation and boot call boundary, ILP32/qemu, runtime mutants."""
CHECK = dict(name='tick-probe-host', tier=4, needs=[], budget=5,
    covers=['kernel/time/tick_probe.c', 'kernel/time/tick_probe.h',
            'net/link.c', 'net/nic.c', 'drivers/net/ne2000/ne2000.c',
            'drivers/net/ne2000/ne2000_data.inc',
            'init/kernel.c', 'tools/tests/tick_probe_host.c',
            'arch/x86/kernel/isr_stub.asm', 'kernel/irq/tick.c',
            'arch/x86/arch_io.h', 'kdebug/tick_probe.c',
            'userland/diag/tickprobe.c', 'build/kernel.mk',
            'build/kernel-objects.mk', 'build/select.mk', 'build/kdebug.mk',
            'kernel/module/debug_probe.c', 'kdebug/kdebug_main.c',
            'platform/pc98/pit.c', 'arch/x86/kernel/mtrr_wc.c',
            'drivers/net/e82557/e82557.c',
            'tools/tests/host32.py'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]

def main():
    original = (ROOT / 'kernel/time/tick_probe.c').read_text()
    kernel = (ROOT / 'init/kernel.c').read_text()
    begin = kernel.index('#ifdef TICK_PROBE\n    tick_probe_boot();')
    end = kernel.index('#endif', kernel.index('    tick_probe_splash(0);', begin))
    boundary = kernel[begin:end + len('#endif')] + '\n'
    link_source = (ROOT / 'net/link.c').read_text()
    link = link_source[link_source.index('void link_tick(void)'):link_source.index('\n}', link_source.index('void link_tick(void)')) + 2]
    tick_source = (ROOT / 'kernel/irq/tick.c').read_text()
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
