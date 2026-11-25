from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import main, ppc_build, ppc_verify

if __name__ == "__main__":
    main(ppc_build, ppc_verify)
