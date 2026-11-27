"""Verify stale display generation rejects submission."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx_accel_hal", ["sys/hal/gfx/gfx_hw_submit.c"],
               {1: "stale display generation rejects submission"}, gc_sections=True)
