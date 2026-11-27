"""Verify restoration uses the saved mode and scanout."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_display", ["sys/dev/video/geforce/gf_v2_display.c"],
               {2: "restoration uses the saved mode and scanout"}, gc_sections=True)
