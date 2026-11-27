"""Verify successful idle fence completes stop."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_life", ["sys/dev/video/geforce/gf_v2_life.c", "sys/dev/video/geforce/gf_v2_fence.c"],
               {16: "stop drains pending fence ring", 15: "successful idle fence completes stop"}, gc_sections=True)
