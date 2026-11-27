"""形式 0・11・0x40 を INVAL とする"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_format_id', ['sys/hal/gfx/v2/format.c'],
               {1: 'invalid format'})
