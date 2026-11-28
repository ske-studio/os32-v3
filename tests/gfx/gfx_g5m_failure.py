"""Check the G5m borrowed window and boot-long failure latch."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx_transfer_bench", [], {1: "forced transfer failure",
               2: "G5m retried after DMA failure", 3: "transfer pixels or guard"}, gc_sections=True, cflags=["-D__cdecl="])
