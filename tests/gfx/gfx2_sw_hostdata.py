"""2×2 の元、key=3 → key の画素だけ書かない"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_hostdata', ['sys/hal/gfx/v2/sw_hostdata.c'],
               {1: 'keyed pixels'})
