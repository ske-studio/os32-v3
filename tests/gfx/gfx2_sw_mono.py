"""0xA0・幅 3・fg=1・bg 透過 → [1,_,1]"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_mono', ['sys/hal/gfx/v2/sw_mono.c'],
               {1: 'mono pixels'})
