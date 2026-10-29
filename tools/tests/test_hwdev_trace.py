"""Run init/C sequences and existing trace expectations against relocated GF code."""
import json
import os
import re
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
    'recover_gf': ['recover_gf_profile_expand'],
    'gfinit_decode': ['gfinit_decode_rom', 'gfinit_decode_instruction'],
    'gfinit_exec': ['gfinit_exec_run', 'gfinit_exec_prepare'],
    'gfinit_pll': ['gfinit_pll_parse', 'gfinit_pll_solve'],
    'gfinit_bus': ['gfinit_bus_init', 'gfinit_bus_probe', 'gfinit_bus_read',
                   'gfinit_bus_write', 'gfinit_bus_local_cleanup'],
    'gfinit_plan': ['gfinit_plan_build', 'gfinit_plan_read', 'gfinit_plan_verify'],
    'gfinit_plan_c': ['gfinit_plan_build_c', 'gfinit_plan_baseline',
                      'gfinit_plan_rebuild_c', 'gfinit_plan_c_control',
                      'gfinit_plan_c_vram', 'gfinit_plan_c_color'],
    'gf_stage_state': ['gf_stage_capture', 'gf_stage_entry'],
}


def run(folder, image=None, mode_c=False):
    elf = module.mkmod.Elf((ROOT/'build/out/hwdev/hwdev.elf').read_bytes())
    header = '#define TRACE_IMPORT_SLOTS %dU\n' % elf.symbol('hwdev_import_slots')['value']
    if mode_c:
        owner_source = (ROOT/'kdebug/gfinit.c').read_text()
        header += '#include "kdebug/gfinit_bus.h"\n#include "kdebug/gfinit_plan.h"\n#include "hwdev/owner.h"\n'
        header += re.search(r'struct gfinit_owner \{.*?\n\};', owner_source, re.S)[0] + '\n'
        # The decoder's local control() has the same name as the owner's BSS.
        symbols = {line.split()[-1]: int(line.split()[0],16) for line in
                   subprocess.check_output([str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-nm'),
                       '--defined-only', str(ROOT/'build/out/hwdev/hwdev.elf')],text=True).splitlines()
                   if line.split()[-1] not in ('control', 'stage') or line.split()[-2] == 'b'}
        for name in ('control', 'owner', 'gf_lease', 'stage', 'gf_read', 'gf_write', 'gfinit_call'):
            header += '#define SEQUENCE_' + name.upper() + ' ' + str(symbols[name]) + 'U\n'
    (folder/'hwdev_trace_symbols.h').write_text(header)
    # Expand only harness includes, retaining real production includes at their
    # original location. This preserves the independent trace oracle verbatim.
    def expand(path):
        lines = []
        for line in path.read_text().splitlines(True):
            if line.startswith('#include "'):
                name = line.split('"')[1]
                target = (path.parent/name).resolve()
                if name == 'hwdev_trace_symbols.h':
                    lines.append(line)
                    continue
                if target.name.endswith(('_host.c', '_host.inc')):
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
    entry = 'gfinit_stages_host.c' if mode_c else 'gfinit_plan_host.c'
    source = 'extern unsigned hwdev_image_symbol(unsigned);\n' + expand(ROOT/'tools/tests'/entry)
    if mode_c:
        source = source.replace('void GF_C_ENTRY(void)', 'void trace_entry(void)')
    else:
        # Add the existing PLL scenarios and call all trace suites before S6.
        pll = (ROOT/'tools/tests/gfinit_pll_host.c').read_text()
        pll = pll[pll.index('static void pll_parser_tests'):pll.index('void _start(void)')]
        prefix, suffix = source.rsplit('void _start(void)\n{', 1)
        suffix = suffix.replace('    gf_fixture_init();\n', '', 1)
        source = prefix + pll + ('\nvoid trace_entry(void)\n{\n'
            '    gf_fixture_init();\n'
            '    adapter_cases();\n'
            '    pll_parser_tests(); pll_solver_tests(); pll_sequence_tests();\n'
            '    resource_tests(); accessor_tests(); production_plan(0); production_plan(RECOVER_CR1F_UNLOCKED);') + suffix
    names = json.loads((ROOT/'hwdev/generated/hwdev_imports.json').read_text())['imports']
    core = {e['symbol'] for e in module.gen.slots() if e['group']=='core'} - {'kpeek_call', 'recover_hal_wait_frame'}
    source += '\nstatic void unused_import(void) { CHECK(0); }\n'
    source += 'static int trace_production_selected(void) { return 0; }\n'
    source += 'extern void hwdev_trace_load(const unsigned *);\nvoid _start(void) {\n'
    replacements = {name: 'sequence_' + name for name in
                    ('recover_owner', 'recover_owner_configure', 'kmemset',
                     'pgalloc_alloc_phys', 'pgalloc_free_n_owner', 'serial_get_setup')} if mode_c else {}
    if mode_c:
        replacements['recover_hal_wait_frame'] = 'exec_delay'
    replacements['gf_production_selected'] = 'trace_production_selected'
    source += '    const unsigned imports[] = {\n' + ''.join(
        '        (unsigned)' + replacements.get(e['name'],
            e['name'] if e['name'] in core or e['name'] == 'kprintf' else 'unused_import') + ',\n'
        for e in names) + '    };\n    hwdev_trace_load(imports); trace_entry();\n}\n'
    harness = folder/'trace.c'
    harness.write_text(source)
    exe = folder/'trace'
    command = plan.command(exe)
    command[command.index(str(ROOT/'tools/tests/gfinit_plan_host.c'))] = str(harness)
    if mode_c:
        command.insert(1, '-DHWDEV_SEQUENCE_IMAGE')
    command[-2:-2] = [str(ROOT/'tools/tests/hwdev_trace_image.c'), str(ROOT/'kernel/module_image.c'),
                      *module.INCLUDES, '-I'+str(folder), '-I'+str(ROOT/'build/out/hwdev')]
    subprocess.run(command, check=True)
    if image is None:
        image = (ROOT/'build/out/hwdev.mod').read_bytes()
    result = host32.run([str(exe)], input=struct.pack('<I', len(image))+image,
                        capture_output=True, timeout=120)
    print(result.stdout.decode(), end='')
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    if mode_c:
        assert b'KMOD gfinit_call A2 -> main0..4 -> baseline required/record/A4 stable/changing GPIO PASS' in result.stdout
    print('PASS relocated GF image: ' + ('A2/main0..4/C' if mode_c else 'S3/S4/S5/S6') + ' existing trace oracles')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='hwdev-trace-') as td:
        run(Path(td))
