"""Verify allocated page tail is initialized."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_sysmem", ["sys/hal/gfx/v2/sysmem.c"],
               {5: "failed page release preserves backing",
                3: "allocated page tail is initialized"}, gc_sections=True)
