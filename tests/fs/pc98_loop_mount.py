"""Mount four formatted loop files on a PC-98 ext2 disk and verify persistence."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, require

FIXTURE = Path("tests/support/fixtures/loop_mount/mount.c")


def verify(config):
    cc = Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
    exe = Path(os.environ["OS32_VERIFY_OUT"]) / "loop-mount"
    includes = (".", "sys/include", "sys/arch/x86", "sys/fs", "sys/fs/ext2",
                "sys/kern", "sys/kern/mm", "sys/hal/block", "sys/hal/input",
                "sys/platform/pc98", "sdk/include/os32", "lib")
    ext2 = ("dir", "file", "fmt", "inode", "layout", "super", "vfs")
    sources = [*[f"sys/fs/ext2/ext2_{unit}.c" for unit in ext2], "sys/fs/vfs.c", "sys/fs/vfs_fd.c",
               "sys/fs/pc98pt.c", "sys/dev/base/dev.c", "sys/dev/block/loop_dev.c",
               "sys/dev/block/ext2_medium_loop.c", "sys/platform/pc98/boot_names.c",
               "sys/platform/pc98/storage/ext2_medium_ide.c", "sys/kern/mm/kmalloc.c",
               "sys/kern/kstring.c", "sys/kern/kstring_c.c"]
    subprocess.run([cc, "-O2", "-ffreestanding", "-ffunction-sections", "-fdata-sections",
                    "-D__cdecl=", "-nostdlib", *["-I" + path for path in includes], FIXTURE,
                    "tests/support/fixtures/input_policy/entry.S", *sources,
                    "-Wl,--gc-sections", "-lgcc", "-o", exe], check=True)
    rc = subprocess.run(["qemu-i386", exe], timeout=30).returncode
    if rc:
        raise Failure({2: "loop name resolution", 3: "loop mount count",
                       8: "loop remount content"}.get(rc, f"loop lifecycle step {rc}"))


if __name__ == "__main__":
    main(lambda config: require(FIXTURE), verify)
