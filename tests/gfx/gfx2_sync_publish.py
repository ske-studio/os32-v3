"""Verify published context reflects the effective sync mode."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import BACKING_SOURCES, run_host
from tests.support.media import main


def verify(config):
    run_host("gfx2_backend_reg", ["sys/hal/gfx/v2/backend_reg.c"],
               {5: "published context reflects the effective sync mode"}, gc_sections=True)
    run_host("gfx2_backing", BACKING_SOURCES,
             {11: "published shadow reaches submission and present"}, gc_sections=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
