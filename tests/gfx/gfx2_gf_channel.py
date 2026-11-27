"""Verify custom emit restores default ROP on failure."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_ops", ["sys/dev/video/geforce/gf_v2_weak.c", "sys/dev/video/geforce/gf_v2_fence.c", "sys/dev/video/geforce/gf_v2_life.c", "sys/dev/video/geforce/accel_rop3.c", "sys/dev/video/geforce/accel_rop.c", "sys/dev/video/geforce/vram_view.c"],
               {1: "custom emit restores default ROP on failure"}, gc_sections=True)
