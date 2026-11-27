"""整列・結合と入力拒否、2048 区間への成功、2049 区間への失敗で全区間が不変"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_heap', ['sys/hal/gfx/v2/vram_heap.c'],
               {2: 'merge suffix', 3: 'double free', 10: 'reject 2048', 7: 'change heap on 2049'})
