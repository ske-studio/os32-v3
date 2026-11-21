"""Compile the product IDE driver against a port protocol model."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from kernel_includes import include_flags

CHECK = dict(name='ide-pio-host', tier=1, budget=4.3,
             covers=['include/ide_bus.h', 'platform/pc98/platform_ide_bus.h', 'platform/pc98/platform_ide_ports.h',
                     'drivers/ata/ide.c', 'drivers/ata/ide.h',
                     'drivers/ata/ide_addr.c', 'drivers/ata/ide_addr.h',
                     'drivers/ata/ide_multiple.inc', 'drivers/ata/ide_pio.inc', 'drivers/ata/ide_request.inc',
                     'tools/tests/ide_pio_host.c', 'tools/tests/ide_pio_host_multiple.inc', 'tools/tests/ide_hostshim/io.h',
                     'platform/powermac/ide_compatible.h', 'tools/tests/ide_be_hostshim/kstring.h',
                     'tools/tests/ide_be_hostshim/platform_ide_bus.h', 'tools/tests/test_ide_pio.py', 'tools/tests/kernel_includes.py'], needs=[],
    basis=['docs/design/drivers/ata/ide.md', 'docs/ppc/ata.md §2'],
)
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-Wno-sign-compare', '-D__cdecl=']
MUTANTS = [
    ('multiple-opcode', 'ide_pio.inc', 'ide_bus_out(IDE_REG_STATUS, multiple > 1',
     'ide_bus_out(IDE_REG_STATUS, multiple > IDE_COMMAND_MAX_SECTORS', 'multiple'),
    ('multiple-phase', 'ide_pio.inc', 'u32 n = sectors - done < multiple ? sectors - done : multiple;',
     'u32 n = 1;', 'multiple'),
    ('multiple-drain', 'ide_pio.inc', 'for (w = 0; w < phase_words; w++) (void)ide_bus_data_word();',
     'for (w = 0; w < IDE_PIO_WORDS; w++) (void)ide_bus_data_word();', 'multiple-errors'),
    ('multiple-valid', 'ide_multiple.inc', '!(current & IDE_ID_MULTI_VALID)',
     '0', 'multiple-identify'),
    ('multiple-current', 'ide_multiple.inc', '(current & IDE_ID_MULTI_COUNT_MASK) != drive_multiple[drive].active',
     '0', 'multiple-identify'),
    ('multiple-reset-bank', 'ide_multiple.inc', 'int drive = bank * IDE_DRIVES_PER_BANK + drv;',
     'int drive = (bank ^ 1) * IDE_DRIVES_PER_BANK + drv;', 'multiple-lifecycle'),
    ('multiple-pending', 'ide_multiple.inc', 'drive_multiple[drive].pending = drive_multiple[drive].max > 1;',
     'drive_multiple[drive].pending = 0;', 'multiple-lifecycle'),
    ('multiple-reinit', 'ide.c', '    ide_multiple_init();',
     '    if (0) ide_multiple_init();', 'multiple-lifecycle'),
    ('multiple-reject', 'ide_multiple.inc', 'drive_multiple[drive].active = drive_multiple[drive].max;',
     'drive_multiple[drive].active = 0;', 'multiple'),
    ('multiple-residual', 'ide_multiple.inc', 'if (st & IDE_ST_DRQ) return IDE_ERR_IO;',
     'if (st & IDE_ST_DRQ) return IDE_OK;', 'multiple-setup'),
    ('split-single', 'ide_request.inc',
     'u32 n = count < IDE_COMMAND_MAX_SECTORS ? count : IDE_COMMAND_MAX_SECTORS;', 'u32 n = 1;', 'sizes'),
    ('count-256', 'ide_pio.inc', 'sectors == IDE_COMMAND_MAX_SECTORS ? 0 : sectors',
     'sectors == IDE_COMMAND_MAX_SECTORS ? 1 : sectors', 'sizes'),
    ('chs-track', 'ide_request.inc', 'if (n > left_in_track) n = left_in_track;',
     '(void)left_in_track;', 'boundaries'),
    ('sasi', 'ide_request.inc', 'n = 1;', 'n = n;', 'boundaries'),
    ('phase-bsy', 'ide_pio.inc', 'if (st & IDE_ST_BSY) continue;\n        *status',
     '/* mutation: inspect busy status */\n        *status', 'errors'),
    ('settle', 'ide_pio.inc', 'for (i = 0; i < IDE_SEL_SETTLE; i++) ide_bus_alt();',
     'for (i = 0; i < 0; i++) ide_bus_alt();', 'errors'),
    ('read-drain', 'ide_pio.inc', 'for (w = 0; w < phase_words; w++) (void)ide_bus_data_word();',
     'for (w = 0; w < phase_words; w++) (void)0;', 'errors'),
    ('finish-fault', 'ide_pio.inc',
     'if (st & (IDE_ST_ERR | IDE_ST_DF)) return IDE_ERR_IO;\n        if (!(st & IDE_ST_DRQ))',
     '/* mutation: omit final errors */\n        if (!(st & IDE_ST_DRQ))', 'errors'),
]


def build(directory, exe, be=False):
    subprocess.run(['gcc', *FLAGS, *(['-DTEST_BE_BUS'] if be else []), '-I' + str(ROOT / 'tools/tests/ide_hostshim'),
                    '-I' + str(directory), *include_flags(ROOT),
                    str(ROOT / 'tools/tests/ide_pio_host.c'), str(directory / 'ide.c'),
                    str(directory / 'ide_addr.c'), '-o', str(exe)], check=True)


def main(args):
    with tempfile.TemporaryDirectory(prefix='os32-ide-pio-') as tmp:
        exe = Path(tmp) / 'ide-pio'
        build(ROOT / 'drivers/ata', exe)
        subprocess.run([str(exe), *[a for a in args if a != '--mutate']], check=True)
        be_tree = Path(tmp) / 'be'
        shutil.copytree(ROOT / 'drivers/ata', be_tree)
        shutil.copy2(ROOT / 'include/ide_bus.h', be_tree)
        shutil.copy2(ROOT / 'tools/tests/ide_be_hostshim/platform_ide_bus.h', be_tree)
        shutil.copy2(ROOT / 'platform/powermac/ide_compatible.h', be_tree)
        shutil.copy2(ROOT / 'tools/tests/ide_be_hostshim/kstring.h', be_tree)
        build(be_tree, exe, be=True)
        subprocess.run([str(exe), 'be'], check=True)
        if '--mutate' in args:
            mirror = Path(tmp) / 'ata'
            shutil.copytree(ROOT / 'drivers/ata', mirror)
            for header in ('include/ide_bus.h', 'platform/pc98/platform_ide_bus.h',
                           'platform/pc98/platform_ide_ports.h'):
                shutil.copy2(ROOT / header, mirror)
            # An unchanged copy must pass before runtime failures count as kills.
            build(mirror, exe)
            subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
            be_mutants = [
                ('compatible-tail', 'ide_compatible.h', 'end < bytes', 'end <= bytes', 'be'),
                ('identify-order', 'platform_ide_bus.h', 'return __builtin_bswap16(raw_be_data());', 'return raw_be_data();', 'be'),
                ('sector-read-order', 'platform_ide_bus.h', 'u16 raw = raw_be_data();', 'u16 raw = ide_bus_data_word();', 'be'),
                ('sector-write-order', 'platform_ide_bus.h', 'outpw(IDE_DATA, __builtin_bswap16(raw))', 'outpw(IDE_DATA, raw)', 'be'),
                ('sector-as-words', 'ide_pio.inc', 'ide_bus_data_read(dst, phase_words);',
                 'for (w = 0; w < phase_words; w++) { u16 v = ide_bus_data_word(); '
                 '__builtin_memcpy(dst + 2 * w, &v, 2); }', 'be'),
                ('absent-ctl-write', 'platform_ide_bus.h', 'absent_ctl++;\n        return;',
                 'absent_ctl++;', 'be'),
                ('absent-probe', 'ide.c', 'probe == IDE_BUS_OPEN8', 'probe == 0', 'be'),
            ]
            for name, file, old, new, case in MUTANTS + be_mutants:
                tree = be_tree if case == 'be' else mirror
                path = tree / file
                original = path.read_text()
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: mutation anchor is not unique')
                path.write_text(original.replace(old, new))
                build(tree, exe, be=case == 'be')
                result = subprocess.run([str(exe), case], capture_output=True, text=True)
                path.write_text(original)
                if result.returncode != 1 or 'FAIL ' not in result.stderr:
                    raise RuntimeError(f'{name}: expected runtime assertion, got {result.returncode}')
                print(f'MUTANT {name}: runtime RED', flush=True)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
