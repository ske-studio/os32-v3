"""Exercise D1 and Y1 using the shipped configuration and the product reader."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_sync_defaults", ["sys/hal/gfx/sync/sync_cfg.c"],
               {1: "PEGC", 2: "Trident gap", 3: "PCI miss", 4: "missing file",
                5: "exact match", 6: "invalid section"})
