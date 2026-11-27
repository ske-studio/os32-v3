"""Verify shim reports an empty device heap."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import BACKING_SOURCES, run_host
from tests.support.media import main


def verify(config):
    run_host("gfx2_v1_shim", ["sys/hal/gfx/v2/v1_shim.c"],
               {88: "shim reports an empty device heap"}, gc_sections=True)
    run_host("gfx2_backing", BACKING_SOURCES,
             {9: "v1 registration selects shim"}, gc_sections=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
