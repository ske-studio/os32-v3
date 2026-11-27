"""Verify unmap and release precede unbind and page release."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_free_order", ["sys/hal/gfx/v2/surface.c", "sys/hal/gfx/v2/gart.c"],
               {2: "unmap and release precede unbind and page release"}, gc_sections=True)
