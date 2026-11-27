"""Verify returned capabilities respect buffer size."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_caps", ["sys/hal/gfx/v2/caps.c"],
               {55: "returned capabilities respect buffer size"}, gc_sections=True)
