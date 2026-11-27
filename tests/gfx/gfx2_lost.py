"""Verify lost adapter bypasses hardware check."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import run_host
from tests.support.media import main


def verify(config):
    run_host("gfx2_check_op", ["sys/hal/gfx/v2/check_op.c"],
               {4: "lost adapter bypasses hardware check"}, gc_sections=True)
    run_host("gf_v2_display", ["sys/dev/video/geforce/gf_v2_display.c"],
             {6: "mode set preserves hard LOST"}, gc_sections=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
