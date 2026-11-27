"""Y3 の非整列 7 バイトを 2 行読み書きし、待ち回数・画素・READBACK flush を照合"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sync_rows', ['sys/hal/gfx/sync/sync_write.c'],
               {7: 'byte tail', 8: 'readback flush'})
