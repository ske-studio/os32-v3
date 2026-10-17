"""T4a-5 actual module transaction core under ILP32.
記録: tools/tests/module_runtime_tdd.md
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import host32
from test_module_image import fixture, crc, word, cases as image_cases
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'kernel/module_runtime.c'
INCLUDES = ['-I' + str(ROOT / d) for d in
            ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
             'sdk/include', 'sdk/include/os32', 'exec')]


def image():
    data = fixture()
    word(data, 22, 0)  # No relocation can rewrite the executable test probes.
    data[4096:4120] = b'\x90' * 24
    data[4096:4099] = b'\x31\xc0\xc3'  # init: return 0
    data[4106:4112] = b'\xb8\x2a\0\0\0\xc3'  # export: return 42
    return crc(data)


CASES = [('running-prefix-reuse', 0, 0)]
CASES += [(f'allocation-{i}', 1, i) for i in range(1, 5)]
CASES += [('init-failure', 2, 0), ('init-changed-exports', 3, 0)]
CASES += [(f'cleanup-stage-{i}', 4, i) for i in range(1, 4)]
CASES += [('record-full', 5, 0), ('dependency-reverse', 6, 0),
          ('dependency-live', 7, 0)]
CASES += [(f'unstopped-ref-{i}', 8, i) for i in range(5)]
CASES += [('prefix-return-reclaimed', 10, 0), ('prefix-return-isolated', 10, 1)]
REJECTIONS = [(name, 9, rc) for _, name, _, rc, _ in image_cases()
              if name in ('export-slot-19', 'export-duplicate', 'crc')]
CASES += REJECTIONS


def main():
    with tempfile.TemporaryDirectory(prefix='module-runtime-') as td:
        folder = Path(td)
        exe = folder / 'runtime'

        def build(source):
            subprocess.run(['gcc', *FLAGS, '-Werror=vla', *INCLUDES,
                            '-nostdlib', '-static', '-no-pie',
                            ROOT / 'tools/tests/module_runtime_host.c', source,
                            ROOT / 'kernel/module_image.c', '-o', exe], check=True)

        def run(cases, expected=0):
            for label, mode, arg in cases:
                data = image()
                if mode == 9:
                    data = next(data for _, name, data, _, _ in image_cases() if name == label)
                payload = struct.pack('<2I', mode, arg) + data
                for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner,
                                        capture_output=True)
                    assert result.returncode == expected, (label, runner, result)
                    if expected:
                        assert b'FAIL ' in result.stdout, (label, result)
                print(('PASS ' if not expected else 'MUTATE RED ') + label, flush=True)

        build(SOURCE)
        run(CASES)
        if '--mutate' in sys.argv:
            source = SOURCE.read_text()
            for label, old, new, cases in (
                ('early-publication', 'r->state == MODULE_RUNNING ? r->exports : 0',
                 '(r->state == MODULE_RUNNING || r->state == MODULE_STARTING) ? r->exports : 0',
                 [CASES[0]]),
                ('unconditional-free',
                 'if (!stopped || r->irq_refs || r->tick_refs || r->callback_refs ||\n'
                 '        r->dma_refs || r->active_refs) goto quarantined;',
                 '(void)stopped;', [case for case in CASES if case[1] in (4, 8)]),
                ('dependency-free', 'if (has_dependents(h)) goto quarantined;',
                 'if (0 && has_dependents(h)) goto quarantined;',
                 [('dependency-live', 7, 0)]),
                ('load-validation-bypass',
                 '    if (rc) goto rejected;\n    r->state = MODULE_VALIDATED;',
                 '    if (0 && rc) goto rejected;\n    r->state = MODULE_VALIDATED;',
                 REJECTIONS),
                ('export-copy-count',
                 'i < h[MI_EXPORT_COUNT]; i++', 'i + 1 < h[MI_EXPORT_COUNT]; i++', [CASES[0]]),
                ('export-copy-slot', 'r->exports[slot] =', 'r->exports[slot - 1] =', [CASES[0]])):
                assert source.count(old) == 1, label
                mutant = folder / (label + '.c')
                mutant.write_text(source.replace(old, new))
                build(mutant)
                run(cases, expected=1)
                print(f'MUTATE {label} RED (runtime assertions)', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
