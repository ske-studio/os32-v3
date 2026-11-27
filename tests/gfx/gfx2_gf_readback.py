"""Verify display readback resolves the current reservation."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_readback", ["sys/dev/video/geforce/gf_v2_life.c", "sys/dev/video/geforce/accel_surface.c", "sys/dev/video/geforce/accel_transfer.c", "sys/dev/video/geforce/accel_m2mf.c", "sys/dev/video/geforce/vram_view.c"],
               {75: "readback preserves pitch padding", 74: "readback preserves pitch padding", 94: "readback reports fence timeout", 72: "display readback resolves the current reservation"}, gc_sections=True)
