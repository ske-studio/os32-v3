"""2×2 の面 → 同じ 4 画素・pitch どおり"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_readback', ['sys/hal/gfx/v2/sw_readback.c'],
               {3: 'output pitch'})
