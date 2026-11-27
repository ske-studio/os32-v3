"""Verify GART mapping resolves each physical page."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import BACKING_SOURCES, host_check


if __name__ == "__main__":
    host_check("gfx2_backing", BACKING_SOURCES,
               {2: "GART CPU access follows address space", 4: "GART mapping resolves each physical page"}, gc_sections=True)
