"""Verify new channel epoch resets stale fence state."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_life", ["sys/dev/video/geforce/gf_v2_life.c", "sys/dev/video/geforce/gf_v2_fence.c"],
               {1: "new channel epoch resets stale fence state"}, gc_sections=True)
