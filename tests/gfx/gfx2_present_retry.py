"""Check C13 palette bounds and retry partial SHADOW transfers."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check

if __name__ == "__main__":
    host_check("gfx2_present_retry", ["sys/hal/gfx/v2/display.c",
                                      "sys/hal/gfx/v2/palette_state.c"],
               {1: "palette bounds", 2: "failed transfer retry",
                3: "center and remaining 20 pixels", 4: "center pixels",
                5: "remaining 20 pixels"}, gc_sections=True)
