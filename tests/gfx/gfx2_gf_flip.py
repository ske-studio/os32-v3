"""Observe GF flip submission and retirement through the display backend."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check

if __name__ == "__main__":
    host_check("gfx2_gf_flip", ["sys/dev/video/geforce/display/flip.c",
                                "sys/dev/video/geforce/gf_v2_display.c",
                                "sys/dev/video/geforce/gf_v2_life.c"],
               {1: "pre-write rejection", 2: "post-write uncertainty",
                3: "pending readback", 4: "confirmed retirement",
                5: "vblank retirement"}, gc_sections=True)
