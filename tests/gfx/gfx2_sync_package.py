"""Read the deployed GUI profile through the product synchronization parser."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
from mkpkg import read_pkg
from tests.support.gfx2_host import run_host
from tests.support.media import main


def build(config):
    subprocess.run(["make", "-j8", "packages"], check=True)


def verify(config):
    # Repack using the effective manifest, including a mutation applied after build.
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run([sys.executable, "-B", "tools/mkpkg.py", "--plan",
                        "build/packages.yaml", "--base", ".", "-o", directory], check=True)
        for package in (Path(directory) / "GUI.PKG", Path("packages/GUI.PKG")):
            _, entries = read_pkg(package)
            matches = [data for name, _, data in entries if name == "/etc/gfx/sync.cfg"]
            if len(matches) != 1:
                raise SystemExit("FAIL missing packaged sync.cfg")
            profile = Path(directory) / "profile.cfg"
            profile.write_bytes(matches[0])
            run_host("gfx2_sync_defaults", ["sys/hal/gfx/sync/sync_cfg.c"],
                     {1: "packaged PEGC", 2: "packaged Trident gap", 3: "packaged GF default"},
                     cflags=[f'-DGFX2_SYNC_PROFILE="{profile}"'])


if __name__ == "__main__":
    main(build, verify)
