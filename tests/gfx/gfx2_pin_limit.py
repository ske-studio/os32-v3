"""Verify pin count saturates at 255."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("pgalloc_pin", ["sys/kern/mm/physmem.c"],
               {98: "pin count saturates at 255", 97: "device owner received pinned RAM"}, gc_sections=True,
               cflags=['-Wl,--defsym=__kernel_resident_end=0x375000', '-Wl,--defsym=__ledger_fixed_start=0x17d000', '-Wl,--defsym=__ledger_fixed_end=0x180000'])
