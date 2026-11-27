"""Verify failed staging unmap retains its pages."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_lock", ["sys/hal/gfx/v2/lock.c"],
               {145: "unlock returns staging pages", 142: "discard clears reused pages", 149: "failed staging unmap retains its pages"}, gc_sections=True)
