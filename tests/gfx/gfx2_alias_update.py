"""Verify rebind updates scanned alias generation."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_surface", ["sys/hal/gfx/v2/surface.c"],
               {3: "rebind updates scanned alias generation"}, gc_sections=True)
