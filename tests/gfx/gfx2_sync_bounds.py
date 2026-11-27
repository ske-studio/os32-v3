"""Y3 は末尾を越す 2 行の write を INVAL とし、最初の行も不変"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sync_bounds', ['sys/hal/gfx/sync/sync_write.c'],
               {2: 'partial write'})
