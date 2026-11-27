"""src 0x80404040 (premult)・dst 0xFF000000 → 0xFF404040"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_alpha', ['sys/hal/gfx/v2/sw_alpha.c', 'sys/hal/gfx/v2/format.c'],
               {1: 'alpha pixels'})
