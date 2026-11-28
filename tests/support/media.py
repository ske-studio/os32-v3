"""Build phases shared by the media and reproducibility checks."""
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tarfile
import zlib

PPC = {
    "core": (["KBOOT_LINK=0"], "ppc-kboot"),
    "link": (["KBOOT_LINK=1"], "ppc-kboot/link"),
    "sh": (["KBOOT_SH=1", "KBOOT_LINK=0"], "ppc-kboot/link-sh"),
    "link-sh": (["KBOOT_SH=1"], "ppc-kboot/link-sh"),
    "powermac": (["IMAGE=powermac"], "ppc-powermac"),
    "neg1": (["NEGATIVE=1", "KBOOT_LINK=0"], "ppc-kboot/neg1"),
}
PPC_MAKE = ["ARCH=ppc", "PLATFORM=powermac"]
OUT = Path("build/out")
FD = Path("images")


class Failure(Exception):
    """Product violated the declared contract."""


def require(path):
    try:
        info = path.stat()
    except FileNotFoundError:
        raise Failure(f"missing {path.name}") from None
    if not stat.S_ISREG(info.st_mode) or info.st_size == 0:
        raise Failure(f"invalid {path}")


def run(*args):
    subprocess.run(args, check=True)


def make(*args):
    run("make", "-j" + os.environ.get("OS32_BUILD_JOBS", "8"), *args)


def distributed(config):
    directory = os.environ.get("OS32_PRODUCT_DIR")
    if not directory:
        return None
    name = "pc98" if config == "pc98" else (
        f"ppc-{config}-{os.environ['GITHUB_SHA']}-a{os.environ['GITHUB_RUN_ATTEMPT']}")
    product = Path(directory) / name
    subprocess.run(["sha256sum", "--quiet", "-c", "SHA256SUMS"], cwd=product, check=True)
    return product


def ppc_output(config):
    return OUT / PPC[config][1]


def ppc_build(config):
    build_ppc_media(config, distributed(config))


def build_ppc_media(config, product):
    if product:
        folder = ppc_output(config)
        folder.mkdir(parents=True, exist_ok=True)
        for name in ("PPCKERN.ELF", "boot.iso"):
            shutil.copy2(product / name, folder / name)
        return
    make(*PPC_MAKE, *PPC[config][0], "all", "iso")


def ppc_verify(config):
    for name in ("PPCKERN.ELF", "boot.iso"):
        require(ppc_output(config) / name)


def x86_build(config):
    product = distributed(config)
    if product:
        OUT.mkdir(parents=True, exist_ok=True)
        FD.mkdir(exist_ok=True)
        for name in ("vmkernel.lz4", "kernel.map", "kernel.elf", "deploy-set.json"):
            shutil.copy2(product / name, OUT / name)
        for name in ("os32_boot.d88", "os32_boot.img", "os32_boot144.img", "os32_install.iso"):
            shutil.copy2(product / name, FD / name)
        shutil.copytree(product / "packages", "packages", dirs_exist_ok=True)
        stage = Path(".check-stage")
        stage.mkdir()
        shutil.copy2(product / "hostdrv.tar.gz", stage)
        with tarfile.open(stage / "hostdrv.tar.gz") as packed:
            packed.extractall(stage, filter="data")
        return
    np21w = Path(".check-stage/np21w").absolute()
    hostdrv = Path(".check-stage/hostdrv").absolute()
    np21w.mkdir(parents=True, exist_ok=True)
    hostdrv.mkdir(parents=True, exist_ok=True)
    paths = [f"NP21W_DIR={np21w}", f"HOSTDRV_DIR={hostdrv}"]
    make(*paths, "all", "kernel-r1")
    make(*paths, "NO_PRUNE=1", "deploy-u3", "iso")
    run(sys.executable, "tools/gen_generation_manifest.py")
    run(sys.executable, "tools/gen_deploy_set.py")
    with tarfile.open(hostdrv.parent / "hostdrv.tar.gz", "w:gz") as archive:
        archive.add(hostdrv, arcname="hostdrv")


def verify_archive(archive_path, manifest):
    name = "hostdrv/.deploy/manifest.txt"
    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            try:
                member = archive.getmember(name)
            except KeyError:
                raise Failure(f"missing {name}") from None
            if not member.isfile():
                raise Failure(f"invalid {name}")
            with archive.extractfile(member) as packed:
                if packed.read() != manifest.read_bytes():
                    raise Failure("HostDrv archive manifest mismatch")
    except (tarfile.TarError, EOFError, OSError, zlib.error) as exc:
        raise OSError(f"cannot read {archive_path}: {exc}") from exc


def x86_verify(config):
    sys.path.insert(0, str(Path("tools").resolve()))
    from fd_reader import d88_to_raw, fat12_files
    from mkpkg import expand_plan, load_plan
    host = Path(".check-stage/hostdrv")
    for path in [OUT / n for n in ("vmkernel.lz4", "kernel.map", "kernel.elf", "deploy-set.json")]:
        require(path)
    for name in ("os32_boot.d88", "os32_boot.img", "os32_boot144.img", "os32_install.iso"):
        require(FD / name)
    manifest = host / ".deploy/manifest.txt"
    require(manifest)
    archive_path = host.parent / "hostdrv.tar.gz"
    require(archive_path)
    verify_archive(archive_path, manifest)
    packages, problems = expand_plan(load_plan("build/packages.yaml"), ".")
    if problems or not packages:
        raise Failure(f"invalid package plan: {problems}")
    for name, _, _, _ in packages:
        require(Path("packages") / f"{name.upper()}.PKG")
    kernel = (OUT / "vmkernel.lz4").read_bytes()
    require(host / "boot/vmkernel.lz4")
    if (host / "boot/vmkernel.lz4").read_bytes() != kernel:
        raise Failure("HostDrv kernel mismatch")
    for name in ("os32_boot.img", "os32_boot144.img"):
        files, _, _ = fat12_files(FD / name)
        if files.get("/VMKRNL.LZ4") != kernel:
            raise Failure(f"{name} kernel mismatch")
    if d88_to_raw((FD / "os32_boot.d88").read_bytes()) != (FD / "os32_boot.img").read_bytes():
        raise Failure("D88 mismatch")
    run(sys.executable, "tools/deploy_source_check.py", "--root", host, "--deploy-set", OUT / "deploy-set.json")


def main(build, verify):
    config, phase = sys.argv[1:]
    try:
        {"build": build, "verify": verify}[phase](config)
    except (Failure, subprocess.CalledProcessError) as exc:
        print(f"{config}: {exc}")
        sys.exit(1)
    except OSError as exc:
        print(f"{config}: {exc}")
        sys.exit(2)
