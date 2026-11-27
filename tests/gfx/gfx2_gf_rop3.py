"""Verify color pattern selects color pattern commands."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_rop3", ["sys/dev/video/geforce/accel_rop3.c"],
               {35: "color pattern selects color pattern commands"}, gc_sections=True)
