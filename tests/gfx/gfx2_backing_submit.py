"""Verify unregistered adapter invalidates prior submission generation."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import BACKING_SOURCES, host_check


if __name__ == "__main__":
    host_check("gfx2_backing", BACKING_SOURCES,
               {4: "GART mapping resolves each physical page", 6: "unregistered adapter invalidates prior submission generation"}, gc_sections=True)
