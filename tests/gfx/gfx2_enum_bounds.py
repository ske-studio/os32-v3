"""端の mode・head を INVAL とし出力を保存"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_enum_bounds', ['sys/hal/gfx/v2/enum.c', 'sys/kern/kstring_c.c'],
               {2: 'mode at count'})
