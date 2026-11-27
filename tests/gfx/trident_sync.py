"""Verify changed display generation rebinds sync context."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("trident_sync", ["sys/dev/video/trident/present.c", "sys/dev/video/trident/regs.c", "tests/support/fixtures/gfx2/trident_device_fake.c", "tests/support/fixtures/gfx2/trident_sync_fake.c"],
               {44: 'changed display generation rebinds sync context', 49: 'publication failure discards context', 39: 'clear restores default sync settings'}, gc_sections=True)
