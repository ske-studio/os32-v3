"""Verify v2 scan does not replace the v1 front."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_gf_scan", ["sys/dev/video/geforce/backend_geforce.c", "sys/dev/video/geforce/gf_v2_display.c"],
               {1: "v2 scan does not replace the v1 front"}, gc_sections=True,
               cflags=['-DOS32_GEFORCE_MODULE'])
