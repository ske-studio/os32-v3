"""Verify KEEP preserves ROP and emits rectangle."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_gf_geometry", ["sys/dev/video/geforce/accel_surface.c", "sys/dev/video/geforce/accel_fill.c"],
               {4: "KEEP preserves ROP and emits rectangle"}, gc_sections=True)
