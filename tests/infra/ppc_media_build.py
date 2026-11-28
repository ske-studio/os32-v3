"""期待の出所は PPC_SHELL_ALIGN_X86_DESIGN.md「製品媒体と互換名」参照。"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import (Failure, PPC, PPC_MAKE, build_ppc_media, distributed,
                                main, make, ppc_output, ppc_verify, require)

sys.path.insert(0, str(Path("tools/ppc").resolve()))
from root_lz4_inventory import measure


def build(config):
    folder = ppc_output(config)
    root = folder / "root.iso"
    product = distributed(config)
    if product and config in ("sh", "link-sh"):
        source = product / "root.iso"
        if not source.is_file():
            # The isolated check tree can rebuild the pair at its canonical path.
            cache = Path(os.environ.get("OS32_MEDIA_ROOT_CACHE", ".check-stage"))
            source = cache / "root.iso"
            if not source.is_file():
                make(*PPC_MAKE, "IMAGE=kboot-link-sh", "kernel")
                cache.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root, source)
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, root)
    build_ppc_media(config, product)
    if product and config == "powermac":
        with root.open("wb") as output:
            subprocess.run(["isoinfo", "-i", folder / "boot.iso", "-x", "/ROOT.ISO;1"],
                           stdout=output, check=True)
        require(root)


def selection(*flags):
    return subprocess.run(["make", "-s", "--no-print-directory", *PPC_MAKE, *flags,
                           "--eval", "media-selection: ; @echo $(IMAGE) $(KB_OUT)", "media-selection"],
                          text=True, capture_output=True)


def verify(config):
    ppc_verify(config)
    legacy = ["KBOOT_LINK=0", "KBOOT_SH=0", "KBOOT_SHELL=1"]
    if config == "powermac":
        cases = [([], config, config), (PPC[config][0], config, config)]
        for name in ("sh", "shell", "link-sh"):
            cases.append(([f"IMAGE=kboot-{name}"], "kboot-link-sh", "link-sh"))
        cases.extend((flags, "kboot-link-sh", "link-sh") for flags in
                     (PPC["sh"][0], PPC["link-sh"][0], [*legacy, "NEGATIVE=0"]))
        for flags, image, output in cases:
            result = selection(*flags)
            if result.returncode or result.stdout.split() != [image, str(ppc_output(output))]:
                raise Failure(f"{flags}: {result.stdout} {result.stderr}")
        result = selection(*legacy, "NEGATIVE=1")
        if result.returncode == 0:
            raise Failure("legacy shell accepted NEGATIVE")
        if "KBOOT_SHELL does not support NEGATIVE" not in result.stderr:
            raise Failure(f"wrong legacy shell rejection: {result.stderr}")

    folder = ppc_output(config)
    nm = Path(os.environ.get("PPC_CROSS_DIR", Path.home() / "opt/cross-ppc")) / "bin/powerpc-elf-nm"
    try:
        sizes = measure(folder / "PPCKERN.ELF", nm)
    except ValueError as exc:
        raise Failure(str(exc)) from exc
    if sizes["resident_end"] > 0x375000 or sizes["chain_end"] > 0x3e0000:
        raise Failure(f"kernel exceeds resident or chain limit: {sizes}")
    if config in ("powermac", "sh", "link-sh") and not sizes["frame"]:
        raise Failure("missing root LOAD")
    print(config, sizes, "boot.iso bytes", (folder / "boot.iso").stat().st_size)


if __name__ == "__main__":
    main(build, verify)
