"""最近点 [10,20] 2→4 / 双線形 [10,20] 2→4 → [10,10,20,20] / [10,13,18,20]"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_stretch', ['sys/hal/gfx/v2/sw_stretch.c', 'lib/divdi3.c'],
               {2: 'bilinear pixels'})
