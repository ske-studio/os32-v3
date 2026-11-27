"""Verify palette ties choose the lower index."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_user", ["userland/lib/gfx/v2/color.c", "sys/hal/gfx/v2/format.c"],
               {4: "palette uses squared RGB distance", 2: "palette ties choose the lower index"}, gc_sections=True)
