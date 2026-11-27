"""1 行 [0..7]、(0)→(2) 幅 6 → [0,1,0,1,2,3,4,5]"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_copy', ['sys/hal/gfx/v2/sw_copy.c'],
               {2: 'overlap pixels'})
