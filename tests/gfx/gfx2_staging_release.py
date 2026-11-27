"""Verify address space teardown drops alias staging."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_owner_release", ["sys/hal/gfx/v2/owner_release.c"],
               {1: "address space teardown drops alias staging"}, gc_sections=True)
