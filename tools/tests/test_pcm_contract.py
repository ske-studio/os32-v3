"""Exercise v1 PCM behavior through real KAPI/HAL/driver bodies (no libc).
PPC generated absent bodies run as portable C on host32; no PPC toolchain needed.
Only module publication, user-range validation and device I/O are host seams.
"""
# 期待の出所: 現状記録 (characterization、2026-11-14)。設計の約束ではなく、AV_HAL へ移る前の今の PCM の振る舞いを写した基準。
# AV_HAL §3 (共通の stream の状態・snd2_v1 bridge) で意図して変える振る舞い (満杯での自動開始の閾値など) は、設計の決定に合わせて期待を直してよい。
CHECK = dict(name='pcm-contract-host', tier=1,
    covers=['sound/hal/*.[ch]', 'sys/include/av_contract.h', 'sdk/include/os32/os32_snd2.h', 'tools/tests/test_pcm_contract.py', 'tools/tests/pcm_contract_host.c',
            'tools/tests/pcm_cs4231_host.c', 'tools/tests/pcm_hostshim/io.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'drivers/sound/cs4231/*', 'sys/include/pcm_contract.h', 'sys/include/types.h',
            'sys/include/time_units.h', 'sys/include/dma_contract.h', 'sys/include/memmap.h',
            'sys/kern/irq/irq.h', 'sys/kern/irq/irq_math.h', 'sys/hal/time/tick_clock.h',
            'sys/kern/module/module_runtime.h', 'sys/kern/module/module_image.h',
            'sys/kern/mm/dma_pool.h', 'sys/kern/mm/kmalloc.h', 'sys/kern/kstring.h',
            'sys/kapi/kapi_generated.c', 'sdk/gen_kapi.py', 'sdk/kapi.json',
            'sdk/kapi_target_ppc.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py',
            'sdk/kapi_rust_gen.py', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_generations.h'], needs=[], budget=5,
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3-6',
           'docs/design/drivers/sound/cs4231/pcm_cs4231.md §2'],
)
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
DRIVER = 'sys/dev/sound/cs4231/pcm_cs4231.c'
BRIDGE = 'sys/hal/sound/snd2_v1.c'


def section(text, start, end):
    assert text.count(start) == text.count(end) == 1
    return start + text.split(start)[1].split(end)[0]


def main():
    with tempfile.TemporaryDirectory(prefix='pcm-contract-') as td:
        tmp = Path(td)
        subprocess.run([sys.executable, '-B', 'sdk/gen_kapi.py', '--target',
                        'ppc-powermac', '--out-dir', str(tmp)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        wrappers = section((ROOT / 'sys/kapi/kapi_generated.c').read_text(),
                           'int __cdecl wrap_pcm_open(', 'int __cdecl wrap_kbd_diag(')
        ppc = section((tmp / 'sys/kapi/kapi_generated.c').read_text(),
                      'int __cdecl wrap_pcm_open(', 'int __cdecl wrap_kbd_diag(')
        (tmp / 'ppc.c').write_text(ppc.replace('wrap_pcm_', 'ppc_pcm_'))
        (tmp / 'wrappers.c').write_text(wrappers)
        fixture = (ROOT / 'tools/tests/pcm_cs4231_host.c').read_text()
        for header in ('stdio.h', 'stdlib.h', 'string.h'):
            fixture = fixture.replace('#include <' + header + '>', '')
        fixture = fixture.replace('../../sys/dev/sound/cs4231/pcm_cs4231.c', str(tmp / 'driver.c'))
        fixture = fixture.replace('../../sys/dev/sound/cs4231/pcm_cs4231_math.c',
                                  str(ROOT / 'sys/dev/sound/cs4231/pcm_cs4231_math.c'))
        (tmp / 'fixture.c').write_text(fixture)
        driver = (ROOT / DRIVER).read_text()
        bridge = (ROOT / BRIDGE).read_text()
        # The paired module has a private symbol table. Qualify its v1
        # exports here because both the module and kernel share one host ELF.
        def module_source(source):
            return re.sub(r'\bpcm_(open|close|status|write|write_check|set_volume|reclaim)\b',
                          r'module_pcm_\1', source)
        stream = (ROOT / 'sys/hal/sound/snd2_stream.c').read_text()
        def run(label, drv, v1, expected, stream_source=stream):
            (tmp / 'stream.c').write_text(stream_source)
            (tmp / 'driver.c').write_text(module_source(drv))
            (tmp / 'bridge.c').write_text(v1)
            exe = tmp / 'contract'
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                            '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                            '-D__cdecl=', '-I' + str(tmp),
                            '-I' + str(ROOT / 'tools/tests/pcm_hostshim'), *include_flags(ROOT),
                            '-I' + str(ROOT / 'sound/hal'), str(ROOT / 'tools/tests/pcm_contract_host.c'),
                            str(tmp / 'bridge.c'), str(ROOT / 'sys/dev/sound/cs4231/pcm_hw.c'),
                            *[str(ROOT / f'sound/hal/snd2_{unit}.c') for unit in
                              ('caps', 'reg', 'stg', 'seg')], str(tmp / 'stream.c'),
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            '-o', str(exe)], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == expected, (label, runner, result)
                assert (b'FAIL ' if expected else b'GREEN PCM contract') in result.stdout, result
                print(f'{label}: {"RED (runtime)" if expected else "GREEN"} runner={runner}', flush=True)
        run('baseline', driver, bridge, 0)
        if '--mutate' in sys.argv:
            anchor = 'if (!s->v1 && s->state == SND2_ST_DRAIN'
            assert stream.count(anchor) == 1
            run('v1 release deferred in IRQ', driver, bridge, 1,
                stream.replace(anchor, 'if (s->state == SND2_ST_DRAIN'))
            threshold = '>= 4096'
            for label, original, old, new, is_driver in (
                ('start-early', bridge, threshold, '>= 4095', False),
                ('start-late', bridge, threshold, '> 4096', False),
                ('device-absent', driver, 'return pcm_open_fail(OS32_ERR_NOSYS);',
                 'return pcm_open_fail(OS32_ERR_INVAL);', True),
                ('bridge-absent', bridge, '? OS32_ERR_NOSYS :', '? OS32_ERR_INVAL :', False),
            ):
                assert original.count(old) == 1, label
                mutant = original.replace(old, new)
                run(label, mutant if is_driver else driver, bridge if is_driver else mutant, 1)
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_pcm_contract.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
