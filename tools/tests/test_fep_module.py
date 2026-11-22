#!/usr/bin/env python3
"""Freestanding ILP32 production FEP facade validation and boot count relay."""
CHECK = dict(name='fep-module', tier=1, needs=[], budget=3.3,
    covers=['tools/tests/host_arch/arch_io.h', 'input/hal/input_ime_gate.c', 'input/hal/input_context.h', 'kernel/ime/fep_module.c', 'kernel/ime/fep_module.h',
            'kernel/ime/fep_body.c', 'kernel/ime/ime.h', 'kernel/ime/ime_store.h',
            'tools/tests/fep_module_host.c',
            'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk',
            'lib/kstring.c', 'lib/kstring_c.c'],
    basis=['docs/design/kernel/ime/ime_dict_info.md 仕組み', 'docs/design/kernel/ime/ime_dict.md core との境目'],
)
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kernel/ime/fep_module.c'
OLD = 'return candidate_valid ? candidate.boot_connections() : stub_api.boot_connections();'
NEW = 'return candidate_valid ? 0 : stub_api.boot_connections();'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-module-') as tmp:
        work = Path(tmp)
        # Import metadata belongs to the loader suite; no load is invoked here.
        (work / 'fep_imports.h').write_text(
            '#include "input/hal/input_context.h"\n#define FEP_IMPORT_COUNT 0u\n#define FEP_MODULE_ABI_GEN 1u\n')
        flags = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                 '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                 '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra',
                 '-Werror', '-I' + str(ROOT), '-I' + str(ROOT / 'kernel/ime'),
                 '-I' + str(work),
                 '-I' + str(ROOT / 'tools/tests/host_arch'), *include_flags(ROOT)]
        body = work / 'body.o'
        subprocess.run([*flags, '-Dime_boot_connections=body_boot_connections',
                        '-c', str(ROOT / 'kernel/ime/fep_body.c'), '-o', str(body)], check=True)
        product = (ROOT / PRODUCT).read_text()
        harness = (ROOT / 'tools/tests/fep_module_host.c').read_text()
        cases = [('normal', product, 0)]
        if args.mutate:
            for label, old, new, expected_rc in [
                    ('stop-keeps-action', '    input_ime_action_flush();', '', 14),
                    ('facade-ignores-running', 'fep_services_public() && module_exports(fep_handle)',
                     'fep_services_public()', 16),
                    ('boot-relay-zero', OLD, NEW, 4),
                    ('stop-keeps-pending', '(void)input_ime_cancel_take(&invalid);', '(void)invalid;', 13),
                    ('stop-takes-after-cleanup', '    fep_close_input();\n    return abort_handle();',
                     '    const KbdPolicySnapshot invalid = {0};\n'
                     '    fep_closed = 1;\n    input_ime_action_flush();\n    kbd_policy_ime_state(0, 0, 0);\n'
                     '    int rc = abort_handle();\n'
                     '    (void)input_ime_cancel_take(&invalid);\n    return rc;', 13)]:
                if product.count(old) != 1:
                    raise RuntimeError('mutation anchor not unique: ' + label)
                cases.append((label, product.replace(old, new), expected_rc))
        for label, code, expected_rc in cases:
            unit = work / 'module.c'
            unit.write_text(harness.replace('#include "kernel/ime/fep_module.c"', code))
            exe = work / label
            subprocess.run([*flags, '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', str(unit), str(body),
                            str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], cwd=ROOT).returncode
            if rc != expected_rc:
                raise RuntimeError(f'{label}: expected runtime rc={expected_rc}, got {rc}')
            print(f'{"PASS" if label == "normal" else "RED"} {label}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_module.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
