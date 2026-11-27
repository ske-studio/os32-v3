"""Verify invalid preflight prevents every operation."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_submit", ["sys/hal/gfx/v2/submit.c", "sys/hal/gfx/v2/check_op.c", "sys/hal/gfx/v2/sw_table.c"],
               {3: "invalid preflight prevents every operation"}, gc_sections=True)
