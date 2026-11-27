"""Verify failed mapping restores page tables."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("paging_gfx2", [],
               {1: "failed mapping restores page tables"}, gc_sections=True)
