"""Run unchanged S3/S4/S5/S6 trace expectations against relocated GF code."""
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import host32
import test_hwdev_module as module
import test_gfinit_plan as plan

ROOT = module.ROOT
# Host C still supplies static helpers used by existing white-box tests. All
# public calls below each implementation include enter the loaded production body.
FUNCTIONS = {
    'gfinit_decode': ['gfinit_decode_rom', 'gfinit_decode_instruction'],
    'gfinit_exec': ['gfinit_exec_run'],
    'gfinit_pll': ['gfinit_pll_parse', 'gfinit_pll_solve'],
    'gfinit_bus': ['gfinit_bus_init', 'gfinit_bus_probe', 'gfinit_bus_read',
                   'gfinit_bus_write', 'gfinit_bus_local_cleanup'],
    'gfinit_plan': ['gfinit_plan_build', 'gfinit_plan_read', 'gfinit_plan_verify'],
}


def run(folder):
    elf = module.mkmod.Elf((ROOT/'build/out/hwdev/hwdev.elf').read_bytes())
    header = '#define TRACE_IMPORT_SLOTS %dU\n' % elf.symbol('hwdev_import_slots')['value']
    (folder/'hwdev_trace_symbols.h').write_text(header)
    # Expand only harness includes, retaining real production includes at their
    # original location. This preserves the independent trace oracle verbatim.
    def expand(path):
        lines = []
        for line in path.read_text().splitlines(True):
            if line.startswith('#include "'):
                name = line.split('"')[1]
                target = (path.parent/name).resolve()
                if target.name.endswith('_host.c'):
                    lines.append(expand(target))
                    continue
                line = '#include "' + str(target) + '"\n'
                lines.append(line)
                for fn in FUNCTIONS.get(target.stem, []):
                    lines.append('#define %s (*(__typeof__(&%s))hwdev_image_symbol(%dU))\n' %
                                 (fn, fn, elf.symbol(fn)['value']))
                continue
            lines.append(line)
        return ''.join(lines)
    source = 'extern unsigned hwdev_image_symbol(unsigned);\n' + expand(ROOT/'tools/tests/gfinit_plan_host.c')
    # Add the existing PLL scenarios and call all trace suites before S6.
    pll = (ROOT/'tools/tests/gfinit_pll_host.c').read_text()
    pll = pll[pll.index('static void pll_parser_tests'):pll.index('void _start(void)')]
    prefix, suffix = source.rsplit('void _start(void)\n{', 1)
    source = prefix + pll + ('\nvoid trace_entry(void)\n{\n'
        '    all_trace(); log_tests(); depth_tests(); budget_tests(); condition_tests();\n'
        '    selection_tests(); force_reset_test(); failure_tests(); frame_mismatch_tests(); pre_read_tests();\n'
        '    pll_parser_tests(); pll_solver_tests(); pll_execution_tests();\n'
        '    resource_tests(); accessor_tests(); production_plan(0); production_plan(RECOVER_CR1F_UNLOCKED);') + suffix
    names = json.loads((ROOT/'hwdev/generated/hwdev_imports.json').read_text())['imports']
    core = {e['name'] for e in names[:25]} - {'kpeek_call', 'recover_hal_wait_frame'}
    source += '\nstatic void unused_import(void) { CHECK(0); }\n'
    source += 'extern void hwdev_trace_load(const unsigned *);\nvoid _start(void) {\n'
    source += '    const unsigned imports[] = {\n' + ''.join(
        '        (unsigned)' + (e['name'] if e['name'] in core or e['name'] == 'kprintf' else 'unused_import') + ',\n'
        for e in names) + '    };\n    hwdev_trace_load(imports); trace_entry();\n}\n'
    harness = folder/'trace.c'
    harness.write_text(source)
    exe = folder/'trace'
    command = plan.command(exe)
    command[command.index(str(ROOT/'tools/tests/gfinit_plan_host.c'))] = str(harness)
    command[-2:-2] = [str(ROOT/'tools/tests/hwdev_trace_image.c'), str(ROOT/'kernel/module_image.c'),
                      *module.INCLUDES, '-I'+str(folder), '-I'+str(ROOT/'build/out/hwdev')]
    subprocess.run(command, check=True)
    image = (ROOT/'build/out/hwdev.mod').read_bytes()
    result = host32.run([str(exe)], input=struct.pack('<I', len(image))+image,
                        capture_output=True, timeout=120)
    print(result.stdout.decode(), end='')
    assert result.returncode == 0, (result.returncode, result.stderr)
    print('PASS relocated GF image: S3/S4/S5/S6 existing trace oracles')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='hwdev-trace-') as td:
        run(Path(td))
