from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from test_gfxsync_cfg import CHECK, run
