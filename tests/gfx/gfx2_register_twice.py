"""Check repeated registration and scanout lifetime across GF fallback."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check

if __name__ == "__main__":
    host_check("gfx2_register_twice", [
        "sys/hal/gfx/v2/backend_reg.c", "sys/hal/gfx/v2/display.c",
        "sys/hal/gfx/v2/v1_bridge.c", "sys/hal/gfx/v2/surface.c", "sys/hal/gfx/v2/fence.c",
        "sys/dev/video/geforce/gf_v2_ops.c", "sys/dev/video/geforce/gf_v2_life.c",
    ], {1: "repeated registration", 2: "active fallback", 3: "confirmed stop",
        4: "unconfirmed stop", 5: "alias rebind", 6: "held alias", 7: "full alias table", 8: "replacement alias",
        9: "failed switch revoked alias", 10: "retired alias still public",
        11: "fallback console after failed switch"}, gc_sections=True)
