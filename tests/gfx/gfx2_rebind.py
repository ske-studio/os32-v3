"""Verify rebind revokes the borrowed locked surface."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_v1_bridge", ["sys/hal/gfx/v2/v1_bridge.c", "sys/hal/gfx/v2/surface.c", "sys/hal/gfx/v2/lock.c"],
               {122: "rebind revokes the borrowed locked surface"}, gc_sections=True)
