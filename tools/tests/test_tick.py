"""D1b registry and resident ownership; CPU IF is an explicit host seam."""
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import host32

ROOT = Path(__file__).resolve().parents[2]
OWNERS = {
    'kernel/fm_module.c': ('SND', 'snd_tick', 'int fm_module_load(void)'),
    'kernel/pcm_module.c': ('PCM', 'pcm_tick', 'int pcm_module_load(void)'),
    'drivers/ne2000.c': ('NE2K', 'ne2k_timer_tick', 'int ne2k_init('),
    'kernel/lan_module.c': ('E82557', 'e82557_tick', 'int lan_module_load(void)'),
    'net/link.c': ('LINK', 'link_tick', 'void link_init('),
    'drivers/lgy98.c': ('LGY98', 'lgy98_tick', 'int lgy98_attach('),
}

def wiring():
    isr = (ROOT / 'kernel/isr_handlers.c').read_text()
    body = isr[isr.index('void timer_handler(void)'):]
    assert body.count('tick_dispatch();') == 1
    assert body.index('r1_fixture_timer();') < body.index('tick_dispatch();')
    for rel, (slot, fn, entry) in OWNERS.items():
        src = (ROOT / rel).read_text()
        registration = f'tick_register(PC98_TICK_{slot}, {fn})'
        assert src.count(registration) == 1 and registration in src[src.index(entry):]
        assert fn not in body
        assert '_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT' in src
    for rel, boundary in [('drivers/ne2000.c', 'kmemset(&nic'),
                          ('drivers/lgy98.c', 'cfg.reg_base'),
                          ('net/link.c', 'kmemcpy(link_my_mac'),
                          ('kernel/fm_module.c', 'if (fm_handle.generation)'),
                          ('kernel/pcm_module.c', 'if (pcm_handle.generation)'),
                          ('kernel/lan_module.c', 'if (lan_handle.generation)')]:
        src = (ROOT / rel).read_text()
        src = src[src.index(OWNERS[rel][2]):]
        assert src.index('tick_register(') < src.index(boundary), rel
    assert 'tick_order.h' not in (ROOT / 'kernel/tick.c').read_text()
    assert 'kernel/tick.c' in (ROOT / 'build/kernel.mk').read_text()
    print('PASS timer fixture order, six resident owners, platform boundary', flush=True)


def main():
    wiring()
    with tempfile.TemporaryDirectory(prefix='tick-') as td:
        exe = Path(td) / 'tick'
        def build(source):
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie',
                            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'kernel'),
                            '-I' + str(ROOT / 'platform/pc98'),
                            '-DTICK_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/tick_host.c'), '-o', str(exe)], check=True)
        build(ROOT / 'kernel/tick.c')
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner)
            assert result.returncode == 0, (runner, result.returncode)
            print('PASS real tick registry: reverse order, holes, errors, IF, IRQ context (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            original = (ROOT / 'kernel/tick.c').read_text()
            mutants = [
                ('kctx_irq_depth != 0', 'kctx_irq_depth > 1', 'IRQ registration accepted'),
                ('slots[slot].fn != fn', 'slots[slot].fn == fn', 'duplicate/competition reversed'),
                ('else slots[slot].fn = fn;', 'else slots[slot].fn = 0;', 'registration lost'),
                ('if (slots[slot].fn) slots[slot].fn();',
                 'if (slots[slot].fn) { slots[slot].fn(); slots[slot].fn(); }', 'double callback'),
                ('irq_restore(saved);', 'irq_restore(saved & ~0x200U);', 'IF lost'),
            ]
            for old, new, label in mutants:
                assert old in original
                source = Path(td) / 'mutant.c'
                source.write_text(original.replace(old, new, 1))
                build(source)
                result = host32.run([str(exe)], runner='qemu')
                assert result.returncode, label
                print('MUTATE RED ' + label, flush=True)


if __name__ == '__main__':
    main()
