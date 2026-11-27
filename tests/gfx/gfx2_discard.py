"""Verify discard still waits for concurrent device access."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_lock", ["sys/hal/gfx/v2/lock.c"],
               {127: "discard still waits for concurrent device access"}, gc_sections=True)
