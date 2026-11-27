"""ROP3 0x66 (S^D)、D=0x0F・S=0xFF / 0x5A (P^D)、mono P=0xAA の行 → 0xF0 / 交互に反転"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sw_rop', ['sys/hal/gfx/v2/sw_rop.c'],
               {2: 'rop pixels'})
