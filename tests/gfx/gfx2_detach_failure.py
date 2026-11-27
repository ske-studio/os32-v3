"""Verify failed unmap retains the mapping."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import run_host
from tests.support.media import main


def verify(config):
    run_host("gfx2_surface", ["sys/hal/gfx/v2/surface.c"],
               {2: "failed unmap retains the mapping"}, gc_sections=True)
    run_host("gfx2_owner_release", ["sys/hal/gfx/v2/owner_release.c"],
             {1: "teardown detaches a failed mapping"}, gc_sections=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
