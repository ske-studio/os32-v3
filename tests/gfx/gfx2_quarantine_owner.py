"""Verify failed quarantine owner creation can be retried."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_backend_reg", ["sys/hal/gfx/v2/backend_reg.c"],
               {3: "failed quarantine owner creation can be retried"}, gc_sections=True)
