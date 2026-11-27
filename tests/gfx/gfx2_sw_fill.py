"""4×4 INDEX8 全 0、(1,1,2,2) に 7 → 内側 4 画素だけ 7"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_fill', ['sys/hal/gfx/v2/sw_fill.c'],
               {2: 'fill pixels'})
