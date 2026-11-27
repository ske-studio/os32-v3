"""Verify unexpected mapped page fails audit."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_map_audit", ["sys/hal/gfx/v2/map_audit.c"],
               {3: "unexpected mapped page fails audit"}, gc_sections=True)
