"""Verify reserved HeadInfo array has six words."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_abi", [],
               {9: "reserved HeadInfo array has six words"}, gc_sections=True,
               cflags=['-std=gnu89'])
