"""Link host fixtures against layout symbols from the current kernel ELF.
Run make kernel first, so the ELF and generated memmap_layout.h agree.
"""
from functools import lru_cache
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SYMBOLS = ('__bss_end',
           '__kernel_resident_end', '__ledger_fixed_start', '__ledger_fixed_end')


@lru_cache(maxsize=1)
def linker_flags():
    output = subprocess.check_output(
        ['nm', '--defined-only', str(ROOT / 'build/out/kernel.elf')], text=True)
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in SYMBOLS:
            symbols[fields[2]] = int(fields[0], 16)
    missing = set(SYMBOLS) - symbols.keys()
    if missing:
        raise RuntimeError(f'kernel ELF missing {sorted(missing)}; run make kernel')
    return [f'-Wl,--defsym={name}={symbols[name]:#x}' for name in SYMBOLS]
