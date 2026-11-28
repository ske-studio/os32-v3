"""Product-shell acceptance shared by the mac99 runner and the PC-98 operator."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import pc98pt

GROUP_LIMIT = 524290
LENGTH = {"short": 2400, "long": 4000, "groups": 524320,
          "empty": 2400, "broken": 2400, "tiny": 64}
REJECTION = {"empty": "no approved partition (-13)", "broken": "invalid partition or length (-10)",
             "tiny": "insufficient space (-5)", "busy": "device busy or reservation unavailable (-17)",
             "gui": "disk write not allowed (-1)"}
USAGE = "format <0-3|lo0-lo3> [SECTORS]"
OLD = "r4-old-file"
NEW = "r4-new-file"
LOOP = "r4-loop-file"


def commands(case, mounted=False, explicit=False):
    """Yield (command, ordered output substrings). Wait for the prompt after every row."""
    if case == "spelling":
        for args in ("00", "lo4", "0 0", "0 4294967296"):
            yield "format " + args, (USAGE,)
        return
    if case in REJECTION:
        yield "format 0", ("Format failed: " + REJECTION[case],)
        return
    if mounted:
        yield from commands("busy")
        yield "cat /hd0/old.txt", (OLD,)
        yield "umount /hd0", ()
    args = " 524320" if case == "groups" else (" 4000" if explicit else "")
    yield "format 0" + args, ("Formatting 0...", "Format complete.")
    yield "mount /hd0 hd0 ext2", ()
    yield "cat /hd0/old.txt", ("cat: /hd0/old.txt: No such file or directory",)
    yield "echo " + NEW + " > /hd0/new.txt", ()
    yield "cat /hd0/new.txt", (NEW,)
    yield "umount /hd0", ()
    yield "mount /hd0 hd0 ext2", ()
    yield from commands("busy")
    yield "cat /hd0/new.txt", (NEW,)
    yield "cp /loop.img /hd0/loop.img", ()
    yield "losetup /hd0/loop.img 0", ("losetup: lo0 attached: /hd0/loop.img",)
    yield "format lo0", ("Formatting lo0...", "Format complete.")
    yield "mount /loop lo0 ext2", ()
    yield "echo " + LOOP + " > /loop/new.txt", ()
    yield "umount /loop", ()
    yield "mount /loop lo0 ext2", ()
    yield "cat /loop/new.txt", (LOOP,)
    yield "umount /loop", ()
    yield "losetup -d 0", ("losetup: lo0 detached",)
    yield "umount /hd0", ()


def create_pc98(case, path):
    # 4 heads x 8 sectors makes both 2400 and 4000 exact cylinder multiples.
    length = LENGTH[case]
    start = 32
    total = start + length + 32
    table = bytearray(512)
    if case != "empty":
        table[:32] = pc98pt.make_os32(start, length, 4, 8)
    if case == "broken":
        table[8] = 255
        table[12:14] = b'\xff\xff'
    with path.open("xb") as image:
        image.write(pc98pt.make_nhd_header(total // 32, 4, 8))
        image.truncate((total + 1) * 512)
        image.seek(1024)
        image.write(table)
        for sector in (start - 1, start + length):
            image.seek((1 + sector) * 512)
            image.write(b'\xa5' * 512)
        if case in REJECTION:
            return
        with tempfile.TemporaryDirectory(dir=path.parent) as tmp:
            folder = Path(tmp)
            seed = folder / "seed"
            seed.mkdir()
            (seed / "old.txt").write_text(OLD + "\n")
            fs = folder / "root"
            subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", "1024", "-I", "128",
                            "-O", "none", "-d", seed, fs, "1200"], check=True)
            image.seek((1 + start) * 512)
            with fs.open("rb") as source:
                shutil.copyfileobj(source, image)


def compare_region(before, after, start, end):
    before.seek(start)
    after.seek(start)
    while start < end:
        size = min(end - start, 1 << 20)
        left, right = before.read(size), after.read(size)
        if len(left) != size or left != right:
            raise ValueError(f"media differ in byte range {start}..{start + size}")
        start += size


def compare_pc98(before, after, case, unchanged=False):
    size = before.stat().st_size
    if size != after.stat().st_size:
        raise ValueError("image size changed")
    with before.open("rb") as original, after.open("rb") as candidate:
        if unchanged or case in REJECTION:
            compare_region(original, candidate, 0, size)
            return
        base = 33 * 512  # NHD header and start cylinder
        blocks = {"short": 1200, "long": 1440, "groups": GROUP_LIMIT // 2}[case]
        compare_region(original, candidate, 0, base)
        compare_region(original, candidate, base + blocks * 1024, size)
        candidate.seek(base + 1024)
        sb = candidate.read(1024)
        if struct.unpack_from("<I", sb, 4)[0] != blocks or sb[56:58] != b'\x53\xef':
            raise ValueError(f"expected ext2 superblock with {blocks} blocks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    create = sub.add_parser("create")
    create.add_argument("folder", type=Path)
    cmd = sub.add_parser("commands")
    cmd.add_argument("case", choices=sorted(set(LENGTH) | set(REJECTION) | {"spelling"}))
    cmd.add_argument("--mounted", action="store_true")
    cmd.add_argument("--explicit", action="store_true")
    cmp = sub.add_parser("compare")
    cmp.add_argument("case", choices=LENGTH)
    cmp.add_argument("before", type=Path)
    cmp.add_argument("after", type=Path)
    cmp.add_argument("--unchanged", action="store_true")
    args = parser.parse_args()
    if args.action == "create":
        args.folder.mkdir(parents=True, exist_ok=True)
        for case in LENGTH:
            create_pc98(case, args.folder / (case + ".nhd"))
        with (args.folder / "loop.img").open("xb") as backing:
            backing.truncate(163840)
    elif args.action == "commands":
        for command, expected in commands(args.case, args.mounted, args.explicit):
            print(command)
            for line in expected:
                print("    " + line)
    else:
        compare_pc98(args.before, args.after, args.case, args.unchanged)


if __name__ == "__main__":
    main()
