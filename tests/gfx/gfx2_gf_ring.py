"""Verify full fence ring waits before reuse."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_fence", ["sys/dev/video/geforce/gf_v2_fence.c"],
               {20: "full fence ring waits before reuse"}, gc_sections=True)
