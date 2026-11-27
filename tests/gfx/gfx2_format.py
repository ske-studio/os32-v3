"""Verify five bit colors expand with replicated bits."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_format", ["sys/hal/gfx/v2/format.c"],
               {10: "five bit colors expand with replicated bits"}, gc_sections=True)
