"""mask 0xF0、前景 ROP 0xCC・背景 0x00 → 左 4 画素 = S、右 4 = 0"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_rop4', ['sys/hal/gfx/v2/sw_rop4.c'],
               {2: 'mask pixels'})
