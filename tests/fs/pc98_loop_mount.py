"""Mount four formatted loop files on a PC-98 ext2 disk and verify persistence."""
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, require

FIXTURE = Path("tests/support/fixtures/loop_mount/mount.c")


def probe(flags):
    cc = Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
    exe = Path(os.environ["OS32_VERIFY_OUT"]) / "loop-mount"
    shell = exe.with_suffix(".o")
    subprocess.run([cc, "-O2", "-ffunction-sections", "-fdata-sections", "-nostdlib",
                    "-D__OS32_USERLAND__", "-I.", "-Isdk/include", "-Isdk/include/os32",
                    "-Isys/include", "-Isys/arch/x86", "-Isys/platform/pc98", "-Isys/hal/input",
                    "-Iuserland/lib", "-Iuserland/lib/filer", "-c", FIXTURE.with_name("shell.c"),
                    "-o", shell], check=True)
    includes = (".", "sys/include", "sys/arch/x86", "sys/fs", "sys/fs/ext2",
                "sys/kern", "sys/kern/mm", "sys/hal/block", "sys/hal/input",
                "sys/platform/pc98", "sdk/include/os32", "lib")
    ext2 = ("dir", "file", "fmt", "inode", "layout", "super", "vfs")
    sources = [*[f"sys/fs/ext2/ext2_{unit}.c" for unit in ext2], "sys/fs/vfs.c", "sys/fs/vfs_fd.c",
               "sys/fs/isofs/iso9660.c", "sys/fs/isofs/iso9660_media.c",
               "sys/fs/pc98pt.c", "sys/dev/base/dev.c", "sys/dev/block/loop_dev.c",
               "sys/dev/block/ext2_medium_loop.c", "sys/platform/pc98/boot_names.c",
               "sys/platform/pc98/storage/ext2_medium_ide.c", "sys/kern/mm/kmalloc.c",
               "sys/kern/kstring.c", "sys/kern/kstring_c.c"]
    subprocess.run([cc, *flags, "-O2", "-ffreestanding", "-ffunction-sections", "-fdata-sections",
                    "-D__cdecl=", "-nostdlib", *["-I" + path for path in includes], FIXTURE, shell,
                    "tests/support/fixtures/input_policy/entry.S", *sources,
                    "-Wl,--gc-sections", "-lc", "-lgcc", "-o", exe], check=True)
    result = subprocess.run(["qemu-i386", exe], stdout=subprocess.PIPE, timeout=30)
    rc = result.returncode
    if rc:
        raise Failure({2: "loop name resolution", 3: "loop mount count",
                       8: "loop remount content", 11: "duplicate image", 12: "backing mount",
                       13: "mounted format", 14: "mounted detach", 15: "ISO heap budget"}.get(rc, f"loop lifecycle step {rc}"))

    return result.stdout


def verify(config):
    result = probe([])
    trace = probe(["-DHD_TRACE"])
    actual = [f'{"W" if write else "R"} {lba} {count}'
              for write, lba, count in struct.iter_unpack("<III", trace)]
    if actual != FIXTURE.with_name("hd-io.txt").read_text().splitlines():
        raise Failure("HD request sequence")
    print(f"{len(actual)} HD requests match baseline")
    print("HD + four loop mounts + ISO free heap:", int.from_bytes(result, "little"))


if __name__ == "__main__":
    main(lambda config: require(FIXTURE), verify)
