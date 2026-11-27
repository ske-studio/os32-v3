"""src [1,2,1]・key 1、dst [9,9,9] → [9,2,9]"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_ckey', ['sys/hal/gfx/v2/sw_ckey.c'],
               {1: 'key endpoint'})
