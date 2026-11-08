"""D1b registry and resident ownership; CPU IF is an explicit host seam."""
CHECK = dict(name='tick-host', tier=3,
    covers=['drivers/video/geforce/geforce_tick.[ch]', 'kernel/irq/tick.c', 'kernel/irq/tick.h', 'kernel/irq/irq.h', 'include/types.h', 'platform/pc98/tick_order.h', 'arch/x86/kernel/isr_handlers.c', 'build/kernel.mk', 'tools/tests/tick_host.c', 'tools/tests/host32.py', 'drivers/sound/fm/fm_module.c', 'drivers/sound/cs4231/pcm_module.c', 'drivers/net/e82557/lan_module.c', 'drivers/net/ne2000/ne2000.c', 'drivers/net/ne2000/ne2000_control.inc', 'drivers/net/ne2000/lgy98.c', 'net/link.c', 'net/nic.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.9)

from kernel_includes import include_flags
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import host32

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query

ROOT = Path(__file__).resolve().parents[2]
OWNERS = {
    'drivers/sound/fm/fm_module.c': ('SND', 'snd_tick', 'int fm_module_load(void)'),
    'drivers/sound/cs4231/pcm_module.c': ('PCM', 'pcm_tick', 'int pcm_module_load(void)'),
    'drivers/net/ne2000/ne2000.c': ('NE2K', 'ne2k_timer_tick', 'int ne2k_init('),
    'drivers/net/e82557/lan_module.c': ('E82557', 'e82557_tick', 'int lan_module_load(void)'),
    'net/nic.c': ('LINK', 'net_tick', 'int net_bind('),
    'drivers/net/ne2000/lgy98.c': ('LGY98', 'lgy98_tick', 'int lgy98_attach('),
    'drivers/video/geforce/geforce_tick.c': ('GF', 'geforce_tick', 'int geforce_tick_bind('),
}

def wiring():
    isr = (ROOT / 'arch/x86/kernel/isr_handlers.c').read_text()
    body = isr[isr.index('void timer_handler(void)'):]
    assert body.count('tick_dispatch();') == 1
    assert body.index('r1_fixture_timer();') < body.index('tick_dispatch();')
    for rel, (slot, fn, entry) in OWNERS.items():
        # A0 reserves and exercises slot 6; B6 supplies its resident owner.
        if rel == 'drivers/video/geforce/geforce_tick.c' and not (ROOT / rel).exists():
            print('PENDING B6: GF resident owner; slot exercised by tick_host', flush=True)
            continue
        src = (ROOT / rel).read_text()
        if rel == 'drivers/net/ne2000/ne2000.c':
            src = src.replace('#include "ne2000_control.inc"',
                              (ROOT / 'drivers/net/ne2000/ne2000_control.inc').read_text())
        registration = f'tick_register(PC98_TICK_{slot}, {fn})'
        assert src.count(registration) == 1 and registration in src[src.index(entry):]
        assert fn not in body
        if rel != 'net/nic.c':
            assert '_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT' in src
    for rel, boundary in [('drivers/net/ne2000/ne2000.c', 'kmemset(&nic'),
                          ('drivers/net/ne2000/lgy98.c', 'cfg.reg_base'),
                          ('net/nic.c', 'link_init(local_mac)'),
                          ('drivers/sound/fm/fm_module.c', 'if (fm_handle.generation)'),
                          ('drivers/sound/cs4231/pcm_module.c', 'if (pcm_handle.generation)'),
                          ('drivers/net/e82557/lan_module.c', 'if (lan_handle.generation)')]:
        src = (ROOT / rel).read_text()
        if rel == 'drivers/net/ne2000/ne2000.c':
            src = src.replace('#include "ne2000_control.inc"',
                              (ROOT / 'drivers/net/ne2000/ne2000_control.inc').read_text())
        src = src[src.index(OWNERS[rel][2]):]
        assert src.index('tick_register(') < src.index(boundary), rel
    assert 'tick_register' not in (ROOT / 'net/link.c').read_text()
    assert 'tick_order.h' not in (ROOT / 'kernel/irq/tick.c').read_text()
    assert 'kernel/irq/tick.c' in query(ROOT, config_only=True)['C_KERNEL']
    print('PASS timer fixture order, six resident owners plus GF slot (owner checked when present), platform boundary', flush=True)


def main():
    wiring()
    with tempfile.TemporaryDirectory(prefix='tick-') as td:
        exe = Path(td) / 'tick'
        def build(source):
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie',
                            *include_flags(ROOT),
                            '-DTICK_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/tick_host.c'), '-o', str(exe)], check=True)
        build(ROOT / 'kernel/irq/tick.c')
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner)
            assert result.returncode == 0, (runner, result.returncode)
            print('PASS real tick registry: reverse order, holes, errors, IF, IRQ context (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            original = (ROOT / 'kernel/irq/tick.c').read_text()
            mutants = [
                ('kctx_irq_depth != 0', 'kctx_irq_depth > 1', 'IRQ registration accepted'),
                ('slots[slot].fn != fn', 'slots[slot].fn == fn', 'duplicate/competition reversed'),
                ('else slots[slot].fn = fn;', 'else slots[slot].fn = 0;', 'registration lost'),
                ('if (slots[slot].fn) slots[slot].fn();',
                 'if (slots[slot].fn) { slots[slot].fn(); slots[slot].fn(); }', 'double callback'),
                ('slot < TICK_SLOT_COUNT;', 'slot < TICK_SLOT_COUNT - 1;', 'seventh slot omitted'),
                ('irq_restore(saved);', 'irq_restore(saved & ~0x200U);', 'IF lost'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1
                source = Path(td) / 'mutant.c'
                source.write_text(original.replace(old, new, 1))
                build(source)
                result = host32.run([str(exe)], runner='qemu')
                assert result.returncode, label
                print('MUTATE RED ' + label, flush=True)


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_tick.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
