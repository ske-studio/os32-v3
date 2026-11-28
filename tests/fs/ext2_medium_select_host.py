"""PC-98 formatter observations from the compatibility table in the FS decision."""
from dataclasses import dataclass, replace
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import Failure, main

FIXTURE = ROOT / "tests/support/fixtures/ext2_medium_select/medium.c"
LIMIT = 524290
IO, NOSPC, INVAL, NOPART = -1, -5, -10, -13


@dataclass(frozen=True)
class Case:
    drv: int = 0
    providers: int = 3
    state: str = "ok"
    capacity: int = 340
    start: int = -1
    length: int = 128
    image: str = "-"
    rc: int = 0


def cases():
    # Run mutation witnesses before the larger images.
    yield Case(drv=0x600, rc=IO)
    yield Case(drv=0x100, start=18, rc=INVAL)
    for start in (-1, 18):
        hd = Case(start=start)
        implicit = start < 0
        for drv in range(4):
            yield replace(hd, drv=drv)
        for state, rc in (("absent", IO), ("geometry-error", IO), ("no-part", NOPART),
                          ("read-error", IO), ("no-bios", NOPART), ("identify", INVAL),
                          ("write-error", IO), ("data-read-error", IO)):
            if not implicit and state in ("no-part", "no-bios", "identify"):
                rc = 0
            if state in ("absent", "geometry-error"):
                yield replace(hd, drv=3, state=state, rc=rc)
            yield replace(hd, state=state, rc=rc)
        for providers in (0, 2):
            yield replace(hd, providers=providers, rc=IO)
        for drv in (-1, 4, 0x604, 0x100, 0x10000, *range(0x600, 0x604)):
            providers = 1 if 0x600 <= drv < 0x604 else 3
            rc = IO if implicit and (drv & 255) > 3 else INVAL
            yield replace(hd, drv=drv, providers=providers, rc=rc)
        for length in (LIMIT-2, LIMIT, LIMIT+2):
            yield replace(hd, length=length, capacity=LIMIT+64,
                          rc=NOSPC if length > LIMIT and not implicit else 0)
        yield replace(hd, length=126, rc=NOSPC)
        yield replace(hd, length=400, rc=0 if implicit else INVAL)
        for slot in range(4):
            lo = replace(hd, drv=0x600+slot, start=-1 if implicit else 8)
            yield replace(lo, rc=IO)
            for image in ("old.hdi", "new.nhd"):
                yield replace(lo, image=image)
            for state, image, rc in (("detach", "old.hdi", IO), ("readonly", "old.hdi", INVAL),
                                     ("ok", "disk.d88", INVAL), ("ok", "wide.hdi", INVAL),
                                     ("reattach", "old.hdi", 0)):
                yield replace(lo, state=state, image=image, rc=rc,
                              length=256 if state == "reattach" else 128)
        yield replace(hd, drv=0x600, providers=0, rc=IO)
    for start, length in ((17, 128), (18, 0), (340, 128)):
        yield Case(start=start, length=length, rc=INVAL)
    yield Case(start=18, capacity=0, rc=INVAL)
    yield Case(drv=0x100, length=126, rc=NOSPC)
    for state, rc in (("no-part", NOPART), ("read-error", IO), ("identify", INVAL)):
        yield Case(drv=0x100, state=state, rc=rc)


def compile_fixture(root, exe):
    includes = ("sys/include", "sys/fs", "sys/fs/ext2", "sys/kern", "sys/hal/block",
                "sys/platform/pc98", "lib", "sdk/include/os32")
    sources = ("sys/fs/ext2/ext2_fmt.c", "sys/fs/ext2/ext2_layout.c", "sys/fs/pc98pt.c",
               "sys/platform/pc98/storage/ext2_medium_ide.c", "sys/dev/block/loop_dev.c",
               "sys/dev/block/ext2_medium_loop.c")
    subprocess.run(["gcc", "-std=gnu11", "-O1", "-D__cdecl=", *["-I"+i for i in includes],
                    FIXTURE, *sources, "-o", exe], cwd=root, check=True)


def loop_image(name):
    if name == "disk.d88":
        data = bytearray(688+16+512)
        struct.pack_into("<II", data, 28, len(data), 688)
        struct.pack_into("<BH", data, 691, 2, 8)
        return data, 688
    nhd = name.endswith(".nhd")
    header, sectors = (512, 320) if nhd else (4096, 192)
    bps = 1024 if name == "wide.hdi" else 512
    data = bytearray(b'\xa5' * (header+sectors*bps))
    if nhd:
        data[:16] = b"T98HDDIMAGE.R0\0\0"
        struct.pack_into("<IIHHH", data, 0x110, header, sectors//8, 1, 8, bps)
    else:
        struct.pack_into("<6I", data, 8, header, sectors*bps, bps, 8, 1, sectors//8)
    return data, header


def observe(exe, case, folder):
    c = case
    hd = f"hd{c.drv & 3}"
    table = bytearray(512)
    if c.state != "no-part":
        table[:2] = bytes([0x80, 0xe2])
        struct.pack_into("<H", table, 10, 1)
        struct.pack_into("<H", table, 14, max(1, c.capacity//17-1))
    if c.state == "legacy":
        struct.pack_into("<HHHH", table, 8, 17, 0, 18, 0)
    if c.state == "range":
        table[8] = 255
        table[12:14] = b'\xff\xff'  # Invalid under the old layout too.
    with (folder/hd).open("wb") as f:
        f.truncate(max(c.capacity, 2)*512)
        f.seek(512)
        f.write(table)
    images = {}
    for name in {c.image, "new.nhd" if c.state == "reattach" else "-"} - {"-"}:
        images[name] = loop_image(name)
        (folder/name).write_bytes(images[name][0])
    args = (c.drv, c.providers, c.state, c.capacity, c.start, c.length, c.image)
    trace = subprocess.check_output(["qemu-x86_64", exe, *map(str, args)], cwd=folder, text=True)
    lines = trace.splitlines()
    if c.drv == 0x600 and c.providers == 3 and c.image == "-" and "R hd0 1 1" in lines:
        raise Failure("unattached loop performed HD I/O")
    if c.drv == 0x100 and c.start == 18 and lines[-1] == "result 0" and any(
            line.startswith("W hd0 ") for line in lines):
        raise Failure("masked drive wrote hd0")
    if c.drv == 0x604 and c.start == 18 and lines[:-1] != ["present 1540", "geometry 1540"]:
        raise Failure("explicit invalid drive probe sequence differs")
    if c.state == "mounted" and lines[:-1] != [f"present {c.drv}"]:
        raise Failure("mounted HD reached partition discovery")
    expected = NOPART if c.state == "identify" and c.start < 0 and 0 <= c.drv < 4 else c.rc
    if lines[-1] != f"result {expected}":
        raise Failure(f"medium result {c}: {lines[-1]}")
    writes = [l.split() for l in lines if l.startswith("W ")]
    if c.rc and c.state not in ("write-error", "data-read-error", "read-error") and writes:
        raise Failure(f"unexpected writes {c}")
    if c.drv == 0x600 and c.providers == 3 and c.image == "-" and len(lines) != 1:
        raise Failure("unattached loop performed HD I/O")
    if c.rc == 0 and not writes:
        raise Failure(f"missing writes {c}")
    target = "new.nhd" if c.state == "reattach" else c.image
    begin = max(0, c.start) if target != "-" else (17 if c.start < 0 else c.start)
    for _, name, address, count in writes:
        offset, size = int(address), int(count)
        if target == "-":
            correct = name == hd and begin <= offset and offset+size <= begin+c.length
        else:
            base = images[target][1] + begin*512
            correct = name == target and base <= offset and offset+size <= base+c.length*512
        if not correct:
            raise Failure(f"wrong write {name} {address} {count} for {c}")
    for name, (before, header) in images.items():
        after = (folder/name).read_bytes()
        low = header+begin*512
        high = low+c.length*512
        if after[:low] != before[:low] or after[high:] != before[high:]:
            raise Failure(f"sentinel changed {name} {c}")
        if (name != target or c.rc) and after != before:
            raise Failure(f"inactive image changed {name} {c}")
    # Digest every resulting medium for the one-time base/candidate comparison.
    digests = {}
    for name in [hd, *images]:
        with (folder/name).open("rb") as f:
            digests[name] = hashlib.file_digest(f, "sha256").hexdigest()
    if expected != c.rc:
        # Keep the original baseline for all other observations, including I/O.
        trace = "\n".join([*lines[:-1], f"result {c.rc}"]) + "\n"
    return trace, digests


def verify(config):
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp)/"format"
        compile_fixture(ROOT, exe)
        subprocess.run(["qemu-x86_64", exe, "registration"], check=True)
        for case in (Case(state="mounted", rc=-17), Case(state="range", rc=INVAL),
                     Case(state="legacy", rc=NOPART)):
            with tempfile.TemporaryDirectory(dir=tmp) as folder:
                observe(exe, case, Path(folder))
        observations = hashlib.sha256()
        for case in cases():
            with tempfile.TemporaryDirectory(dir=tmp) as folder:
                trace, media = observe(exe, case, Path(folder))
                observations.update(hashlib.sha256(trace.encode()).digest())
                for name, digest in sorted(media.items()):
                    observations.update(name.encode() + bytes.fromhex(digest))
        # Ordered I/O, return values and final file bytes observed on the PC-98 base.
        if observations.hexdigest() != "c66f6ecba9b12b83eddcbc831ba8f1392dfafe26d7f22ac77bbaf5c2cb7cc79e":
            raise Failure("PC-98 formatter trace or medium differs from baseline")


if __name__ == "__main__":
    main(lambda config: None, verify)
