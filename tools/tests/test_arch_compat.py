#!/usr/bin/env python3
"""Shared compatibility vectors; partial host evidence never closes an ID."""
# Privileged MSR operations are fake; the real idle C body runs on host.
# Saved values remain opaque. Cross compilation independently checks PPC code.
CHECK = dict(env=['ppc-cross'], name='arch-compat-host', tier=2, budget=30, needs=[], covers=[
    'tools/tests/test_arch_compat.py', 'tools/tests/arch_compat_vectors.c',
    'tools/tests/host32.py', 'docs/decisions/ARCH_COMPAT_CONTRACT.md',
    'platform/pc98/link_platform.c', 'platform/powermac/link_platform.c',
    'net/link_platform.h', 'include/types.h', 'include/kernel_tick.h',
    'kernel/irq/tick.h', 'platform/pc98/tick_order.h',
    'platform/powermac/mpic.c', 'platform/powermac/mpic.h',
    'platform/powermac/mpic_tree.h', 'arch/ppc/ppc_frame.h',
    'arch/ppc/ppc_trap.h', 'include/platform_irq.h',
    'platform/powermac/platform_irq_limits.h', 'kernel/irq/irq_math.h',
    'kernel/idle_work.h', 'arch/ppc/arch_io.h', 'arch/ppc/kernel_dec.c',
    'arch/ppc/ppc_oea.h',
    'arch/ppc/ppc_pd.h', 'kernel/time/tick_clock.h',
    'platform/powermac/kboot_info.h', 'kernel/irq/irq.h',
    'platform/pc98/d2_bind.c', 'platform/powermac/input_boot.c',
    'platform/powermac/input_boot.h', 'platform/powermac/input_usb.h',
    'platform/powermac/input_decode.c', 'platform/powermac/lan_boot.h',
    'kernel/cli/resident_cli.h', 'kernel/input_service.c', 'kernel/input_legacy.c',
    'kernel/input_wait.c', 'input/hal/in2_int.h', 'include/con_sink.h', 'exec/exec.h',
    'include/input_service.h', 'include/pointer_service.h', 'include/kbd.h',
    'include/clock_source.h', 'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_gfx2.h',
    'sdk/include/os32/os32_kapi_generated.h', 'include/memmap.h',
    'sdk/gen_kapi.py', 'sdk/kapi_target_ppc.py', 'sdk/kapi_abi_x86.py',
    'sdk/target_profile.py', 'sdk/kapi.json', 'kapi/kapi_generated.c',
    'sdk/include/os32/os32_kapi_slots.h'],
    pins=['input-hal/4b'], basis=['ARCH_COMPAT_CONTRACT §4'],
)
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
IDS = ('AC-TIME-1/rational-wrap-context', 'AC-TIME-2/source-unavailable',
       'AC-TIME-3/jiffy-unit', 'AC-SYNC-1/nested-restore',
       'AC-SYNC-2/return-enabled-no-lost-wakeup', 'AC-IRQ-1/claim-spurious-complete',
       'AC-KAPI-1/absent-types-capability', 'AC-INPUT-1/empty-vs-absent-cancel')
# Independent inventory makes a missing registration a failing negative control.
REQUIRED = {'AC-TIME-1', 'AC-TIME-2', 'AC-TIME-3',
            'AC-SYNC-1', 'AC-SYNC-2', 'AC-IRQ-1', 'AC-KAPI-1', 'AC-INPUT-1'}


def input_kapi(root, work, vectors, mutation=None):
    """Real provider/table + generated wrappers; fake keyboard/USB/LAN ingress."""
    sources = {p: (root / p).read_text() for p in (
        'platform/pc98/d2_bind.c', 'platform/powermac/input_boot.c',
        'sdk/gen_kapi.py', 'sdk/kapi_target_ppc.py')}
    if mutation:
        path, old, new = mutation
        assert sources[path].count(old) == 1, (path, old)
        sources[path] = sources[path].replace(old, new)
    generator = work / 'sdk'
    generator.mkdir(exist_ok=True)
    for name in ('gen_kapi.py', 'kapi_target_ppc.py', 'kapi_abi_x86.py', 'target_profile.py'):
        (generator / name).write_text(sources.get('sdk/' + name, (root / 'sdk' / name).read_text()))
    api = json.loads((root / 'sdk/kapi.json').read_text())['api']
    slot = next(i for i, a in enumerate(api) if a['name'] == 'kbd_trygetkey')
    # All pointer slots now have providers on PowerMac.
    names = ('kbd_trygetkey', 'db_last_error', 'snd_bgm_is_playing', 'ime_is_active', 'snd_bgm_stop')
    statuses, slots = {}, []
    for provider, target in (('pc98', 'x86-pc98'), ('powermac', 'ppc-powermac')):
        output = work / target
        subprocess.run([sys.executable, '-B', str(generator / 'gen_kapi.py'),
            '--target', target, '--out-dir', str(output), str(root / 'sdk/kapi.json')],
            cwd=root, check=True, stdout=subprocess.DEVNULL)
        slots.append((output / 'sdk/include/os32/os32_kapi_slots.h').read_bytes())
        generated = (output / 'kapi/kapi_generated.c').read_text()
        selected = []
        for name in ('kapi_reserved_nosys', *('wrap_' + n for n in names)):
            match = re.search(r'^[^\n]*\b' + name + r'\([^\n]*\)\n\{.*?^\}', generated, re.M | re.S)
            assert match, name
            selected.append(match[0])
        (work / 'kapi.inc').write_text('\n'.join(selected))
        if provider == 'pc98':
            source = sources['platform/pc98/d2_bind.c']
            source = source[source.index('static u8 pc98_shift_state'):source.index('static const PointerOps')]
        else:
            source = sources['platform/powermac/input_boot.c']
        (work / 'input.inc').write_text(source)
        (work / 'input-vectors.c').write_text(vectors)
        exe = work / ('input-' + provider)
        subprocess.run(['gcc', '-m32', '-nostdinc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie', '-fno-stack-protector',
            '-I' + str(work), '-I' + str(root), '-I' + str(root / 'include'),
            '-I' + str(root / 'sdk/include/os32'), '-I' + str(root / 'platform/powermac'),
            '-DINPUT_SOURCE="input.inc"', '-DKAPI_SOURCE="kapi.inc"', '-DTRY_SLOT=' + str(slot),
            *(['-DPC98_INPUT', '-DKBD_IRQ=1'] if provider == 'pc98' else []),
            str(work / 'input-vectors.c'), str(root / 'platform/powermac/input_decode.c'),
            '-o', str(exe)], check=True)
        try:
            rc = subprocess.run(['qemu-i386', str(exe)], timeout=2).returncode
        except subprocess.TimeoutExpired:
            rc = 'timeout'
        statuses[provider] = rc
    assert slots[0] == slots[1], 'public slot order differs by target'
    return statuses


def check_input_kapi(root, work, vectors, mutate):
    expected = {'pc98': 0, 'powermac': 14}
    actual = input_kapi(root, work, vectors)
    assert actual == expected, ('FAIL/XPASS input/KAPI baseline', actual)
    print('PASS AC-INPUT-1/empty-try-peek-cooked-foreground (partial): pc98 wiring, powermac reader', flush=True)
    print('PASS AC-KAPI-1/selected-generated-types-reserved-slots (partial): both targets', flush=True)
    for bit, name in ((2, 'raw=-1 aliases empty'),
                      (4, 'pressed=0 aliases released'), (8, 'modifiers=0 aliases none')):
        assert actual['powermac'] & bit
        print('XFAIL AC-INPUT-1/powermac ' + name + ' (not a conformance pass)', flush=True)
    print('UNIMPL AC-INPUT-1/independent-capability-query both providers (F6); '
          'pc98 driver/IRQ and guest wait progress unverified', flush=True)
    print('UNIMPL AC-KAPI-1/all-groups-void-query-pairs-context (F8); IDs remain open', flush=True)
    if mutate:
        mutations = (
            ('pc98-try-blocks', 'platform/pc98/d2_bind.c',
             '.trygetkey = kbd_trygetkey,', '.trygetkey = kbd_getkey,', {'pc98': 'timeout', 'powermac': 14}),
            ('pc98-char-wiring', 'platform/pc98/d2_bind.c',
             '.trygetchar = kbd_trygetchar,', '.trygetchar = kbd_trygetkey,', {'pc98': 64, 'powermac': 14}),
            ('ppc-empty', 'platform/powermac/input_boot.c',
             'return key >= 0 ? key : pm_input_usb_peek();', 'return key >= 0 ? key : 0;', {'pc98': 0, 'powermac': 64}),
            ('ppc-consume', 'platform/powermac/input_boot.c',
             'return pm_input_usb_take();', 'return pm_input_usb_peek();', {'pc98': 0, 'powermac': 64}),
            ('ppc-usb-lost', 'platform/powermac/input_boot.c',
             'return pm_input_usb_take();', 'return -1;', {'pc98': 0, 'powermac': 64}),
            ('ppc-peek-consumes', 'platform/powermac/input_boot.c',
             'return key >= 0 ? key : pm_input_usb_peek();', 'return key >= 0 ? key : pm_input_usb_take();', {'pc98': 0, 'powermac': 64}),
            ('ppc-cancel-xpass', 'platform/powermac/input_boot.c',
             '.wait_pending = ring3_wait_pending,', '.wait_pending = 0,', {'pc98': 0, 'powermac': 15}),
            ('ppc-raw-xpass', 'platform/powermac/input_boot.c',
             'static int raw(void) { return -1; }',
             'static int raw(void) { int c = raw_key; raw_key = -1; return c; }', {'pc98': 0, 'powermac': 12}),
            ('ppc-pressed-xpass', 'platform/powermac/input_boot.c',
             '(void)scancode; return 0;', '(void)scancode; return down;', {'pc98': 0, 'powermac': 10}),
            ('ppc-modifiers-xpass', 'platform/powermac/input_boot.c',
             'static u32 zero32(void) { return 0; }', 'static u32 zero32(void) { return mods; }', {'pc98': 0, 'powermac': 6}),
            ('db-pointer-absent', 'sdk/kapi_target_ppc.py',
             'PROVIDER_GROUPS = {', "PROVIDER_GROUPS = {\n    'db': ({'db_last_error'}, set()),", {'pc98': 0, 'powermac': 64}),
            ('absent-query', 'sdk/kapi_target_ppc.py',
             "\n    'snd_bgm_is_playing',\n", "\n", {'pc98': 0, 'powermac': 64}),
            ('ime-not-zero', 'sdk/kapi_target_ppc.py',
             'else "OS32_ERR_NOSYS"', 'else "0"', {'pc98': 0, 'powermac': 64}),
            ('absent-void', 'sdk/gen_kapi.py',
             '        if ret != "void":',
             '        if name == "snd_bgm_stop": c_content += "    snd_bgm_stop();\\n"\n        if ret != "void":',
             {'pc98': 0, 'powermac': 64}),
        )
        for label, path, old, new, observed in mutations:
            actual = input_kapi(root, work, vectors, (path, old, new))
            assert actual == observed and actual != expected, (label, actual, observed)
            print('RED runtime: ' + label + ' ' + str(actual), flush=True)


def exercise(root, work, vectors, mpic=None, idle=None, provider_text=None):
    product = (root / 'arch/ppc/kernel_dec.c').read_text()
    start = product.index('void arch_cpu_idle(void)')
    end = product.index('void arch_cpu_halt(void)', start)
    (work / 'idle.c').write_text(idle if idle is not None else product[start:end])
    (work / 'io.h').write_text('u32 irq_save(void);\n'
        'void irq_restore(u32); void _idle(void); int _irq_enabled(void);\n')
    (work / 'arch_io.h').write_text('u32 ppc_irq_save(void);\n'
        'void ppc_irq_restore(u32); void _enable(void); void _halt(void); void _idle(void); int _irq_enabled(void);\n')
    (work / 'idt.h').write_text('#include "types.h"\nextern volatile u32 tick_count;\n')
    src = work / 'vectors.c'
    src.write_text(vectors)
    for provider in ('powermac', 'pc98'):
        exe = work / provider
        source = root / 'platform' / provider / 'link_platform.c'
        if provider == 'powermac' and provider_text is not None:
            source = work / 'provider.c'
            source.write_text(provider_text)
        subprocess.run(['gcc', '-m32', '-nostdinc', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-nostdlib', '-static', '-fno-pie',
            '-no-pie', '-fno-stack-protector', '-I' + str(work), '-I' + str(root),
            '-I' + str(root / 'include'), '-DPROVIDER_SOURCE="' + str(source) + '"',
            '-DIDLE_SOURCE="' + str(work / 'idle.c') + '"',
            *(['-DPM_MPIC_HOST_TEST', '-DMPIC_SOURCE="' + str(mpic or root / 'platform/powermac/mpic.c') + '"',
               '-I' + str(root / 'arch/ppc'), '-I' + str(root / 'platform/powermac')]
              if provider == 'powermac' else []), str(src), '-o', str(exe)], check=True)
        try:
            rc = subprocess.run(['qemu-i386', str(exe)], timeout=2).returncode
        except subprocess.TimeoutExpired:
            print('RED runtime: idle did not return ' + provider, flush=True)
            return False
        if rc != 0:
            print('FAIL compatibility vectors ' + provider + ' rc=' + str(rc), flush=True)
            return False
        print('PASS AC-TIME-3/host-counter ' + provider, flush=True)
    return True


def inventory(ids):
    return {s.split('/')[0] for s in ids} == REQUIRED and len(ids) == len(REQUIRED)


def run(ctx):
    if not shutil.which('qemu-i386'):
        print('SKIP compatibility runtime: qemu-i386 unavailable (not a pass)', flush=True)
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    if not inventory(IDS):
        print('FAIL ID inventory', flush=True)
        return 1
    vectors = (ctx.root / 'tools/tests/arch_compat_vectors.c').read_text()
    with tempfile.TemporaryDirectory(prefix='arch-compat-') as td:
        work = Path(td)
        if not exercise(ctx.root, work, vectors):
            return 1
        check_input_kapi(ctx.root, work, vectors, ctx.mutate)
        print('PASS AC-SYNC-2/return-enabled-no-lost-wakeup powermac '
              '(host real idle C, fake EE/nap)', flush=True)
        for name in IDS:
            if not name.startswith(('AC-SYNC-2/', 'AC-KAPI-', 'AC-INPUT-')):
                print(('PASS ' if name.startswith('AC-IRQ-1') else 'UNIMPL ') + name +
                      (' powermac (pc98 UNIMPL)' if name.startswith('AC-IRQ-1') else ''), flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if not cc.is_file() or not os.access(cc, os.X_OK):
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1':
                print('FAIL PowerPC toolchain required', flush=True)
                return 1
        else:
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-I' + str(ctx.root), '-I' + str(ctx.root / 'include'),
                '-I' + str(ctx.root / 'arch/ppc'), '-c',
                str(ctx.root / 'platform/powermac/link_platform.c'), '-o', str(work / 'ppc.o')], check=True)
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-I' + str(ctx.root), '-I' + str(ctx.root / 'include'),
                '-I' + str(ctx.root / 'arch/ppc'),
                '-I' + str(ctx.root / 'platform/powermac'), '-c',
                str(ctx.root / 'arch/ppc/kernel_dec.c'),
                '-o', str(work / 'dec.o')], check=True)
            print('PASS PowerPC provider + real idle cross compile', flush=True)
        if ctx.mutate:
            product = (ctx.root / 'platform/powermac/mpic.c').read_text()
            for label, old, new in (
                ('claim-zero', ')) return IRQ_ERR_INVAL;', ')) return 0;'),
                ('missing-finish', '*out = (struct irq_claim){token, token, 1, 1};',
                 'out->line = token; out->native_token = token; out->deliver = 1;'),
                ('double-eoi', 'active = 0;\n    write_reg(EOI, 0);',
                 'active = 0;\n    write_reg(EOI, 0); write_reg(EOI, 0);')):
                assert product.count(old) == 1
                mutant_path = work / 'mpic.c'
                mutant_path.write_text(product.replace(old, new).replace(
                    'struct irq_claim claim;', 'struct irq_claim claim = {0};'))
                assert not exercise(ctx.root, work, vectors, mutant_path)
                print('RED runtime: ' + label, flush=True)
            product = (ctx.root / 'arch/ppc/kernel_dec.c').read_text()
            a = product.index('void arch_cpu_idle(void)')
            b = product.index('void arch_cpu_halt(void)', a)
            idle = product[a:b]
            anchor_idle = '__asm__ volatile("nop" ::: "memory");'
            assert idle.count(anchor_idle) == 1
            for replacement in ('for (;;) __asm__ volatile("" ::: "memory");',
                                'irq_state = 0;'):
                assert not exercise(ctx.root, work, vectors,
                                    idle=idle.replace(anchor_idle, replacement))
                print('RED runtime: idle ' + replacement, flush=True)
            # f6980232 AC-SYNC-2: an EE=1 _idle must rest, not spin.
            anchor_nap = '    ppc_cpu_nap();'
            assert idle.count(anchor_nap) == 1
            assert not exercise(ctx.root, work, vectors,
                                idle=idle.replace(anchor_nap, '    /* mutation: spin */'))
            print('RED runtime: idle does not nap', flush=True)
            provider = (ctx.root / 'platform/powermac/link_platform.c').read_text()
            anchor_work = '    idle_wait_step();'
            assert provider.count(anchor_work) == 1
            assert not exercise(ctx.root, work, vectors,
                                provider_text=provider.replace(anchor_work, ''))
            print('RED runtime: omitted idle work', flush=True)
            anchor = '0, 3, 0xfffffffdu, 0xffffffffu };'
            assert vectors.count(anchor) == 2
            before, after = vectors.rsplit(anchor, 1)
            mutant = before + '0, 4, 0xfffffffdu, 0xffffffffu };' + after
            assert not exercise(ctx.root, work, mutant)
            print('RED runtime: vector expectation', flush=True)
            assert not inventory(IDS[:-1])
            print('RED inventory: removed ID', flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
