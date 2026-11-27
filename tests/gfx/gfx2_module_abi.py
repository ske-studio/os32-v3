"""Verify backend ABI mismatch is rejected."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_backend_reg", ["sys/hal/gfx/v2/backend_reg.c"],
               {1: "backend ABI mismatch is rejected"}, gc_sections=True)
