"""Compile the product IDE driver against a port protocol model."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from kernel_includes import include_flags

CHECK = dict(name='ide-pio-host', tier=1, budget=4,
             covers=['drivers/ata/ide.c', 'drivers/ata/ide.h',
                     'drivers/ata/ide_addr.c', 'drivers/ata/ide_addr.h',
                     'drivers/ata/ide_pio.inc', 'drivers/ata/ide_request.inc',
                     'tools/tests/ide_pio_host.c', 'tools/tests/ide_hostshim/io.h',
                     'tools/tests/test_ide_pio.py', 'tools/tests/kernel_includes.py'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-Wno-sign-compare', '-D__cdecl=']
MUTANTS = [
    ('split-single', 'ide_request.inc',
     'u32 n = count < IDE_COMMAND_MAX_SECTORS ? count : IDE_COMMAND_MAX_SECTORS;', 'u32 n = 1;', 'sizes'),
    ('count-256', 'ide_pio.inc', 'sectors == IDE_COMMAND_MAX_SECTORS ? 0 : sectors',
     'sectors == IDE_COMMAND_MAX_SECTORS ? 1 : sectors', 'sizes'),
    ('chs-track', 'ide_request.inc', 'if (n > left_in_track) n = left_in_track;',
     '(void)left_in_track;', 'boundaries'),
    ('sasi', 'ide_request.inc', 'n = 1;', 'n = n;', 'boundaries'),
    ('phase-bsy', 'ide_pio.inc', 'if (st & IDE_ST_BSY) continue;\n        *status',
     '/* mutation: inspect busy status */\n        *status', 'errors'),
    ('settle', 'ide_pio.inc', 'for (i = 0; i < IDE_SEL_SETTLE; i++) inp(IDE_ALT_STATUS);',
     'for (i = 0; i < 0; i++) inp(IDE_ALT_STATUS);', 'errors'),
    ('read-drain', 'ide_pio.inc', 'for (w = 0; w < IDE_PIO_WORDS; w++) (void)inpw(IDE_DATA);',
     'for (w = 0; w < 0; w++) (void)inpw(IDE_DATA);', 'errors'),
    ('finish-fault', 'ide_pio.inc',
     'if (st & (IDE_ST_ERR | IDE_ST_DF)) return IDE_ERR_IO;\n        if (!(st & IDE_ST_DRQ))',
     '/* mutation: omit final errors */\n        if (!(st & IDE_ST_DRQ))', 'errors'),
]


def build(directory, exe):
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT / 'tools/tests/ide_hostshim'),
                    '-I' + str(directory), *include_flags(ROOT),
                    str(ROOT / 'tools/tests/ide_pio_host.c'), str(directory / 'ide.c'),
                    str(directory / 'ide_addr.c'), '-o', str(exe)], check=True)


def main(args):
    with tempfile.TemporaryDirectory(prefix='os32-ide-pio-') as tmp:
        exe = Path(tmp) / 'ide-pio'
        build(ROOT / 'drivers/ata', exe)
        subprocess.run([str(exe), *[a for a in args if a != '--mutate']], check=True)
        if '--mutate' in args:
            mirror = Path(tmp) / 'ata'
            shutil.copytree(ROOT / 'drivers/ata', mirror)
            # An unchanged copy must pass before runtime failures count as kills.
            build(mirror, exe)
            subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
            for name, file, old, new, case in MUTANTS:
                path = mirror / file
                original = path.read_text()
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: mutation anchor is not unique')
                path.write_text(original.replace(old, new))
                build(mirror, exe)
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
