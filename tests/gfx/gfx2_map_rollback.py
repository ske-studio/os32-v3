"""Verify mapping failure removes installed PTEs."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_lock", ["sys/hal/gfx/v2/lock.c"],
               {107: "mapping failure removes installed PTEs"}, gc_sections=True)
