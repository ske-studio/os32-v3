"""Verify failed allocation restores backing."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_surface", ["sys/hal/gfx/v2/surface.c"],
               {1: "failed allocation restores backing"}, gc_sections=True)
