"""Verify paired DVI outputs report DVI-I."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gf_v2_enum", ["sys/dev/video/geforce/gf_v2_enum.c"],
               {2: "paired DVI outputs report DVI-I"}, gc_sections=True)
