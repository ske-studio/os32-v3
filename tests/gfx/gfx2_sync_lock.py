"""Check DIRECT alias staging through the product lock and unlock."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_sync_lock", ["sys/hal/gfx/v2/lock.c"],
               {1: "staging map", 2: "readonly pixels", 3: "unlock cleanup",
                4: "writable pixels", 5: "alias mode"})
