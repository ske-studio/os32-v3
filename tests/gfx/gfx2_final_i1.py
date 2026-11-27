"""Check stage I1 providers in the shipped ELF images."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.gfx2_final import verify
from tests.support.media import main

if __name__ == "__main__":
    main(lambda config: None, lambda config: verify("I1"))
