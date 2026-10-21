"""KMOD validation, independent wire fixtures and real GNU11 ILP32 C.
記録: tools/tests/module_image_tdd.md
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

import host32

ROOT = Path(__file__).resolve().parents[2]
TARGET_SRC = ROOT / "kernel/module_image.c"
HARNESS = ROOT / "tools/tests/module_image_host.c"
FLAGS = ["-m32", "-march=i386", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-Werror=vla", "-ffreestanding", "-fno-pie", "-fno-stack-protector"]


def build_validator(directory, source=None):
    exe = Path(directory) / "module-validator"
    subprocess.run(["gcc", *FLAGS, "-nostdlib", "-static", "-no-pie",
                    "-I" + str(ROOT / "include"), "-I" + str(ROOT / "kernel"),
                    str(HARNESS), str(source or TARGET_SRC), "-o", str(exe)], check=True)
    return exe


def validate(exe, image, *, kapi=75, layout=3, abi=0x12345678, pair=0xabcdef01,
             ops=4, img_valid=1, imports=(), exports=(), module_id=1, runner=None, relocate_base=None):
    contract = bytearray(struct.pack("<11I", kapi, layout, abi, pair, ops, img_valid,
                                     len(imports), len(image), module_id,
                                     int(relocate_base is not None), relocate_base or 0))
    for entry in imports:
        contract.extend(struct.pack("<IHH", *entry))
    slots = [(0, 0, 0)] * 20  # KDEBUG v4: appended core getter
    for slot, name_hash, version in exports:
        slots[slot - 1] = (name_hash, version, 0)
    for entry in slots:
        contract.extend(struct.pack("<IHH", *entry))
    return host32.run([str(exe)], input=bytes(contract) + image, capture_output=True, runner=runner)


def fnv(name):
    value = 2166136261
    for ch in name.encode():
        value = ((value ^ ch) * 16777619) & 0xffffffff
    return value


POLICY = {"imports": [(fnv("kprintf"), 1, 1)], "exports": [(5, fnv("crt_diag_bootlog"), 1)]}


def crc(image):
    struct.pack_into("<I", image, 128, 0)
    struct.pack_into("<I", image, 128, zlib.crc32(image))
    return image


def word(image, index, value):
    struct.pack_into("<I", image, index * 4, value)
    return image


def fixture():
    # Independent schema numbers deliberately do not import the producer.
    names = b"kprintf\0crt_diag_bootlog\0"
    h = [0x444f4d4b, 1, 144, 4160, 75, 3, 0x12345678, 1, 2, 1,
         4096, 4096, 64, 0, 24, 24, 8, 32, 32, 64, 32,
         144, 2, 152, 1, 164, 1, 176, len(names), 0, 0,
         0xabcdef01, 0, 4, 0, 0]
    image = bytearray(struct.pack("<36I", *h) + bytes(4160 - 144))
    struct.pack_into("<2I", image, 144, 1, 5)  # Unaligned, exactly adjacent writes.
    struct.pack_into("<IHHI", image, 152, fnv("kprintf"), 1, 1, 32)
    struct.pack_into("<IHHI", image, 164, fnv("crt_diag_bootlog"), 5, 1, 10)
    image[176:176 + len(names)] = names
    struct.pack_into("<I", image, 4097, 4096)  # one-past is allowed
    struct.pack_into("<I", image, 4101, 24)
    return crc(image)


def cases():
    result = []

    def add(group, name, change, expected=3, policy=None, checksum=True):
        image = fixture()
        changed = change(image)
        if changed is not None:
            image = changed
        if checksum and len(image) >= 132:
            crc(image)
        result.append((group, name, image, expected, dict(POLICY, **(policy or {}))))

    add("future-version", "future", lambda b: word(b, 1, 2))
    add("id-policy", "known-image-wrong-policy", lambda b: None, policy={"module_id": 2})
    add("id-policy", "unknown-policy", lambda b: None, policy={"module_id": 0})
    add("id-policy", "wrong-image-id", lambda b: word(b, 7, 2))
    add("id-policy", "unknown-matching-id", lambda b: word(b, 7, 99),
        policy={"module_id": 99})
    add("id-policy", "wrong-phase", lambda b: word(b, 8, 1))
    add("id-policy", "missing-pair-flag", lambda b: word(b, 9, 0))
    add("id-policy", "unknown-flag", lambda b: word(b, 9, 3))
    add("id-policy", "stop-not-allowed", lambda b: word(b, 30, 10))
    add("header-size", "short-header-field", lambda b: word(b, 2, 140))
    add("image-size", "truncated", lambda b: b[:-1])
    add("image-size", "declared-size", lambda b: word(b, 3, 4159))
    add("image-size", "trailing", lambda b: b + b"x")
    for i in (21, 23, 25, 27):
        add("table-range", f"table-{i}-low", lambda b, i=i: word(b, i, 143))
        add("table-range", f"table-{i}-wrap", lambda b, i=i: word(b, i, 0xfffffffc))
    add("table-range", "empty-table-below-header",
        lambda b: word(word(b, 22, 0), 21, 143))
    for i, limit in ((22, 16384), (24, 1024), (26, 64)):
        add("table-range", f"count-{i}", lambda b, i=i, limit=limit: word(b, i, limit + 1))
    add("table-range", "names-wrap", lambda b: word(b, 28, 0xffffffff))
    add("table-overlap", "tables-overlap", lambda b: word(b, 23, 148))
    add("table-overlap", "names-overlap", lambda b: word(b, 27, 170))
    for off in (1, 2, 3, 4):
        add("reloc-overlap", f"reloc-overlap-{off}", lambda b, off=off: word(b, 37, off), 11)
    def overlap_with_valid_values(b):
        b[4096:4112] = bytes(16)
        word(b, 37, 2)
    add("reloc-overlap", "overlap-with-valid-values", overlap_with_valid_values, 11)
    for off in (29, 30, 31, 32, 33, 34, 35):
        add("reloc-overlap", f"reloc-import-{off}", lambda b, off=off: word(b, 37, off), 11)
    for off in (61, 64, 4096, 0xffffffff):
        add("reloc-range", f"reloc-outside-{off}", lambda b, off=off: word(b, 37, off), 11)
    add("reloc-range", "reloc-value", lambda b: struct.pack_into("<I", b, 4097, 4097), 11)
    for off in (0, 24, 31, 33, 62, 64, 0xfffffffc):
        add("import-slot", f"import-slot-{off}", lambda b, off=off: word(b, 40, off), 12)
    for slot in (0, 17, 19, 20, 21, 65535):  # 19/20 = no fixture contract, 21 = past ops_slots
        add("export-slot", f"export-slot-{slot}", lambda b, slot=slot: struct.pack_into("<H", b, 168, slot))
    add("export-slot", "export-outside-text", lambda b: word(b, 43, 24))
    def duplicate_export(b):
        word(b, 26, 2)
        word(b, 27, 188)
        names = b"kprintf\0crt_diag_bootlog\0crt_diag_bootlog\0"
        word(b, 28, len(names))
        b[176:188] = b[164:176]
        b[188:188 + len(names)] = names
    add("export-slot", "export-duplicate", duplicate_export)
    add("name-hash", "import-name", lambda b: b.__setitem__(176, ord("x")), 12)
    add("name-hash", "export-name", lambda b: b.__setitem__(184, ord("x")))
    add("name-hash", "trusted-import", lambda b: None, 12,
        {"imports": [(fnv("wrong"), 1, 1)]})
    add("name-hash", "trusted-export", lambda b: None, 3,
        {"exports": [(5, fnv("wrong"), 1)]})
    add("crc", "crc", lambda b: b.__setitem__(4000, 1), 9, checksum=False)
    add("pair", "pair", lambda b: word(b, 31, 0), 4)
    add("img-valid", "img-valid", lambda b: None, 5, {"img_valid": 0})
    for i in (4, 5, 6, 33):
        add("abi-ops", f"abi-{i}", lambda b, i=i: word(b, i, 0), 6)
    add("abi-ops", "old-ops-v1", lambda b: word(b, 33, 1), 6)
    add("abi-ops", "caller-ops-mismatch", lambda b: None, 6, {"ops": 1})
    add("abi-ops", "old-ops-matching-caller", lambda b: word(b, 33, 1), 6, {"ops": 1})
    add("abi-ops", "import-count", lambda b: None, 6, {"imports": []})
    # Additional boundary and metadata variants, beyond the 14 required groups.
    for i in (0, 7, 8, 9, 30, 34, 35):
        add("header-extra", f"header-{i}", lambda b, i=i: word(b, i, 0xffffffff))
    for i, val in ((10, 4095), (10, 266240), (11, 0), (11, 4095), (12, 0),
                   (13, 0xffffffff), (14, 0xffffffff), (15, 23), (17, 31),
                   (19, 63), (20, 0xffffffff), (29, 24)):
        add("section-extra", f"section-{i}-{val}", lambda b, i=i, val=val: word(b, i, val))
    for at, value in ((156, 0), (156, 2), (158, 0), (158, 6), (158, 2)):
        add("import-extra", f"import-kind-ver-{at}-{value}",
            lambda b, at=at, value=value: struct.pack_into("<H", b, at, value), 12)
    def duplicate_import(b):
        word(b, 24, 2)
        word(b, 25, 176)
        word(b, 27, 188)
        names = b"kprintf\0kprintf\0crt_diag_bootlog\0"
        word(b, 28, len(names))
        b[176:188] = b[164:176]
        b[164:176] = b[152:164]
        b[188:188 + len(names)] = names
    add("import-extra", "duplicate-import", duplicate_import, 12,
        {"imports": POLICY["imports"] * 2})
    def tiny_payload(b):
        del b[4097:]
        for index, value in ((3, 4097), (12, 1), (14, 1), (15, 1), (16, 0),
                             (17, 1), (18, 0), (19, 1), (20, 0), (22, 1),
                             (24, 0), (26, 0), (28, 0)):
            word(b, index, value)
        word(b, 36, 0)
    add("reloc-range", "payload-smaller-than-word", tiny_payload, 11,
        {"imports": [], "exports": []})
    add("names-extra", "missing-nul", lambda b: word(b, 28, 23))
    add("names-extra", "extra-nul", lambda b: word(b, 28, 26))
    def oversized_image(b):
        b.extend(bytes(262145 - len(b)))
        for index, value in ((3, len(b)), (10, 262144), (12, len(b) - 4096),
                             (19, len(b) - 4096), (20, 0)):
            word(b, index, value)
    add("image-limit", "image-one-byte-over-limit", oversized_image)
    for size in (0, 1, 127, 143):
        add("header-extra", f"short-{size}", lambda b, size=size: b[:size], checksum=False)
    return result


def run_suite(exe, runner, verbose=True):
    positives = [fixture()]
    unsorted = fixture()
    struct.pack_into("<2I", unsorted, 144, 5, 1)
    positives.append(crc(unsorted))
    tail = fixture()
    word(tail, 37, 60)
    word(tail, (4096 + 60) // 4, 0)
    positives.append(crc(tail))
    for image in positives:
        run = validate(exe, image, runner=runner, **POLICY)
        if run.returncode or run.stdout != b"GREEN\n":
            return False, "normal/unaligned/unsorted/last-word", run
    # Maximum legal relocation count; no hidden small scratch-array limit.
    large = bytearray(fixture()[:144])
    payload_offset = 69632
    large.extend(bytes(payload_offset + 65536 - len(large)))
    for index, value in ((3, len(large)), (10, 65536), (11, payload_offset),
                         (12, 65536), (14, 4), (15, 4), (16, 65532),
                         (17, 65536), (18, 0), (19, 65536), (20, 0),
                         (22, 16384), (23, 65680), (24, 0), (25, 65680),
                         (26, 0), (27, 65680), (28, 0)):
        word(large, index, value)
    struct.pack_into("<16384I", large, 144, *range(0, 65536, 4))
    run = validate(exe, crc(large), runner=runner)
    if run.returncode or run.stdout != b"GREEN\n":
        return False, "maximum-reloc-count", run
    groups = set()
    for group, name, image, expected, policy in cases():
        run = validate(exe, image, runner=runner, **policy)
        if run.returncode != 1 or run.stdout != f"RED {expected:02}\n".encode():
            return False, name, run
        groups.add(group)
    if verbose:
        print(f"GREEN normal (unaligned, unsorted, one-past, last-word), runner={runner}")
        for group in sorted(groups):
            print(f"RED {group}")
    return True, "", None


MUTATIONS = [
    ("future", "h[MI_FORMAT] != MODULE_FORMAT_VERSION", "0"),
    ("header-size", "h[MI_HEADER_SIZE] != MODULE_HEADER_SIZE", "0"),
    ("image-size", "h[MI_IMAGE_SIZE] != file_size", "0"),
    ("table-range", "off < MODULE_HEADER_SIZE || off > h[MI_PAYLOAD_OFF]", "off > h[MI_PAYLOAD_OFF]"),
    ("table-overlap", "if (table_off[i] < end)", "if (table_off[i] < end && 0)"),
    ("reloc-overlap", "if (overlap(off, 4, prev, 4))", "if (off == prev)"),
    ("reloc-range", "!within(off, 4, h[MI_PAYLOAD_SIZE])", "!within(off, 1, h[MI_PAYLOAD_SIZE])"),
    ("import-slot", "slot % 4 || slot <", "slot <"),
    ("export-slot", "!in_text(get32(rec + 8), h)", "0"),
    ("name-hash", "hash == expected", "(hash == expected || 1)"),
    ("crc", "crc32_core_final(crc) != h[MI_IMAGE_CRC]", "crc32_core_final(crc) != h[MI_IMAGE_CRC] && 0"),
    ("pair", "h[MI_PAIR_CRC] != policy->pair_image_crc", "0"),
    ("img-valid", "!policy->img_valid", "0"),
    ("abi-ops", "h[MI_OPS_VERSION] != policy->ops_version", "0"),
    ("policy-ops", "h[MI_OPS_VERSION] != component->ops_version", "0"),
    ("policy-id", "h[MI_ID] != component->module_id", "0"),
    ("image-limit", "file_size > component->image_max", "0"),
    ("body-limit", "h[MI_MEM_SIZE] > component->mem_max", "0"),
]


def main():
    runners = os.environ.get("HOST32_RUNNERS", "native qemu").split()
    with tempfile.TemporaryDirectory(prefix="module-image-") as td:
        exe = build_validator(td)
        for runner in runners:
            ok, name, run = run_suite(exe, runner)
            if not ok:
                raise AssertionError((name, run.returncode, run.stdout, run.stderr))
        cross = Path(os.environ.get("CROSS_DIR", "/home/hight/opt/cross")) / "bin/i386-elf-gcc"
        subprocess.run([str(cross), *FLAGS, "-I" + str(ROOT / "include"), "-c",
                        str(TARGET_SRC), "-o", str(Path(td) / "module_image.o")], check=True)
        print("TARGET GNU11 -Werror GREEN")
        if "--mutate" in sys.argv:
            source = TARGET_SRC.read_text()
            for label, old, new in MUTATIONS:
                assert source.count(old) == 1, label
                changed = Path(td) / "module_image.c"
                # Preserve the source's relative include in an isolated flat copy.
                changed.write_text(source.replace(old, new).replace(
                    '"../lib/crc32_core.inc"', '"' + str(ROOT / "lib/crc32_core.inc") + '"'))
                mutant = build_validator(td, changed)
                ok, name, run = run_suite(mutant, runners[0], verbose=False)
                if ok or run.returncode not in (0, 1):
                    raise AssertionError((label, "survived or invalid detection", name, run))
                print(f"MUTATE {label} RED ({name})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
