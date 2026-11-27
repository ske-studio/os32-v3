"""Verify CPU write to VRAM serializes once."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_fence", ["sys/hal/gfx/v2/fence.c"],
               {51: "CPU write to VRAM serializes once"}, gc_sections=True,
               cflags=['-DGFX2_TEST_CPU_END', '-Itests/support/fixtures/gfx2/cpu_model'])
