"""(0,0)→(2,1) → (0,0)・(1,0) だけ"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_line', ['sys/hal/gfx/v2/sw_line.c'],
               {1: 'line pixels'})
