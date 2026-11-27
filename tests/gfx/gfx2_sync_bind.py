"""Y4 は無効な hook で SHADOW に戻り、override の解除で bind 時の設定を復元する"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import host_check


if __name__ == "__main__":
    host_check('gfx2_sync_bind', ['sys/hal/gfx/sync/sync_bind.c'],
               {5: 'sync read', 6: 'wait port', 7: 'readback', 8: 'direct read', 16: 'override clear'})
