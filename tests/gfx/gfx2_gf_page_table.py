"""Verify failed PTE clear retains binding."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check("gfx2_gf_page_table", ["sys/dev/video/geforce/gf_v2_gart.c"],
               {126: "stale epoch rejects mapped range",
                131: "new epoch replays all retained bindings",
                108: "best fit reuses smaller hole",
                112: "failed bind clears installed PTEs",
                250: "failed PTE clear retains binding", 249: "faulted channel retains binding",
                248: "published table retained after resume failure"}, gc_sections=True)
