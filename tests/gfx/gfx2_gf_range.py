"""Verify GART reads require a bound range."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import run_host
from tests.support.media import main


def verify(config):
    run_host("gfx2_gf_geometry", ["sys/dev/video/geforce/accel_surface.c", "sys/dev/video/geforce/accel_fill.c"],
               {1: "GART reads require a bound range", 2: "bound GART source allowed"}, gc_sections=True)
    run_host("gf_v2_ops", ["sys/dev/video/geforce/gf_v2_weak.c", "sys/dev/video/geforce/gf_v2_fence.c", "sys/dev/video/geforce/gf_v2_life.c", "sys/dev/video/geforce/accel_rop3.c", "sys/dev/video/geforce/accel_rop.c", "sys/dev/video/geforce/vram_view.c"],
             {1: "GART destination FILL rejected"}, gc_sections=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
