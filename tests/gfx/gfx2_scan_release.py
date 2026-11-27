"""Check scanout lifetime across asynchronous flip retirement."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check

if __name__ == "__main__":
    host_check("gfx2_scan_release", ["sys/hal/gfx/v2/display.c",
                                      "sys/hal/gfx/v2/surface.c"],
               {1: "active scan reference", 2: "retired backing",
                3: "failed restore backing", 4: "restored backing"}, gc_sections=True)
