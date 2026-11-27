"""Y2 は 8.5 回を 9 回とし、整数・ゼロを保存し上限で飽和する"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sync_gap', ['sys/hal/gfx/sync/sync_gap.c'],
               {1: 'minimum wait', 2: 'saturated remainder'})
