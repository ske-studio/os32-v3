#!/usr/bin/env python3
"""Compile actual gshell types; decode their memory independently in Python.
--mutate always runs the normal control first, then builds two source mutants.
--i686-elf PATH also extracts and checks the target descriptor (no live reads).
"""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
GSHELL = ROOT / "userland/gshell"
sys.path.insert(0, str(ROOT / "tools/np21w_mcp"))
import gui_desc
sys.path.insert(0, str(ROOT / "tools/tests"))
import os32api_host

EXPECTED = dict(front=3, windows=[
    dict(id=458753, owner=2, rect=[-12, 30, 240, 160], title="back", visible=True, minimized=False, z=0),
    dict(id=589828, owner=3, rect=[80, -5, 320, 200], title="front", visible=True, minimized=False, z=1),
    dict(id=131078, owner=4, rect=[4, 5, 64, 48], title="hidden", visible=False, minimized=False, z=2),
    dict(id=196617, owner=5, rect=[9, 10, 120, 90], title="mini", visible=False, minimized=True, z=4),
], slots=[dict(n=0, owner=3), dict(n=2, owner=2)], trim_sent=[True, False, True, False])


def build_fixture(gshell, out, rlib):
    src = gshell / "src"
    text = (src / "lib.rs").read_text().replace("#![no_std]", "#![allow(dead_code)]")
    text = text.replace('#[no_mangle]', '').replace('pub extern "C" fn main(', 'pub extern "C" fn guest_main(')
    text = re.sub(r'^mod (\w+);', lambda m: f'#[path="{src / (m[1] + ".rs")}"] mod {m[1]};', text, flags=re.M)
    text += f'\n#[path="{gshell / "host/mocks.rs"}"] mod mocks;\n'
    text += f'#[path="{gshell / "host/dbgdesc_fixture.rs"}"] mod dbgdesc_fixture;\n'
    root = out / "root.rs"
    root.write_text(text)
    exe = out / "fixture"
    subprocess.run(["rustc", "--edition=2021", "--test", "--crate-name=gshell",
                    "-C", "symbol-mangling-version=v0", str(root),
                    "--extern", f"os32api={rlib}", "-L", str(rlib.parent),
                    "-o", str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe), "--exact", "dbgdesc_fixture::dbgdesc_fixture", "--test-threads=1"],
                   check=True, env={**os.environ, "GSHELL_FIXTURE_DIR": str(out)})
    symbols = gui_desc.elf_symbols(exe)
    blobs = {key: (out / (key + ".bin")).read_bytes() for key in ("gui", "sent", "desc")}
    def read(address, size):
        for key, (start, _) in symbols.items():
            offset = address - start
            if 0 <= offset and offset + size <= len(blobs[key]):
                return blobs[key][offset:offset + size]
        raise AssertionError(f"out of fixture bounds: {address:#x}+{size}")
    return exe, symbols, blobs, read


def roundtrip(exe, symbols, blobs, read):
    actual = gui_desc.read_state(read, exe)
    assert actual == EXPECTED, (actual, EXPECTED)
    hooked = gui_desc.wm_state(SimpleNamespace(mem=read, gshell_elf=exe))
    assert hooked["front"] == EXPECTED["front"]
    assert hooked["windows"] == EXPECTED["windows"]
    assert hooked["slots"] == [dict(item, slot=item["n"], used=True) for item in EXPECTED["slots"]]
    return gui_desc.parse_descriptor(blobs["desc"], symbols)


def rejection(call, reason):
    try:
        call()
    except gui_desc.DescriptorError as exc:
        assert reason in str(exc), (reason, str(exc))
    else:
        raise AssertionError(f"accepted invalid image: {reason}")


def negatives(symbols, blobs, read, d):
    original = blobs["desc"]
    for index, value, reason in ((0, 0, "magic"), (1, 99, "version"),
                                 (2, len(gui_desc.FIELDS) + 1, "field count"),
                                 (3, symbols["gui"][1] + 1, "symbol size"),
                                 (27, symbols["sent"][1] + 1, "symbol size"),
                                 (15, d["win_size"], "outside object")):
        bad = bytearray(original)
        struct.pack_into("<I", bad, index * 4, value)
        blobs["desc"] = bytes(bad)
        rejection(lambda: gui_desc.read_snapshot(read, symbols), reason)
    blobs["desc"] = original
    rejection(lambda: gui_desc.read_snapshot(lambda a, n: b"", symbols), "short")
    original_gui = blobs["gui"]
    bad = bytearray(original_gui)
    start = d["gui_inner"] + d["z_count"]
    bad[start:start + d["usize_size"]] = (d["win_count"] + 1).to_bytes(d["usize_size"], "little")
    blobs["gui"] = bytes(bad)
    rejection(lambda: gui_desc.read_snapshot(read, symbols), "z_count")
    blobs["gui"] = original_gui
    # 窓なし・全非表示でも、未使用 z 項の owner へフォーカスを与えない。
    bad = bytearray(original_gui)
    for i in (1, 4):
        bad[d["gui_inner"] + d["windows"] + i * d["win_size"] + d["visible"]] = 0
    blobs["gui"] = bytes(bad)
    assert gui_desc.read_snapshot(read, symbols)["front"] == 0
    blobs["gui"] = original_gui
    for output, reason in (("", "found 0"),
                            ("10 04 d _RNv6gshell2wm3GUI\n20 04 d _RNvX6gshell2wm3GUI\n30 7c R GSHELL_DBG_DESC\n", "found 2")):
        with patch.object(gui_desc.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output, "")):
            rejection(lambda: gui_desc.elf_symbols("unused"), reason)
    print("GREEN: full expected JSON, invalid magic/version/count/sizes/offset, short read, z_count, no front, missing/ambiguous symbols")


def target_descriptor(elf):
    symbols = gui_desc.elf_symbols(elf)
    data = elf.read_bytes()
    assert data[:6] == b"\x7fELF\x01\x01", "target must be ELF32 little endian"
    assert struct.unpack_from("<H", data, 18)[0] == 3, "target must be i386"
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    strings = sections[shstrndx]
    names = data[strings[4]:strings[4] + strings[5]]
    addr, size = symbols["desc"]
    hits = [s for s in sections if s[1] == 1 and s[3] <= addr and addr + size <= s[3] + s[5]]
    assert len(hits) == 1
    section = hits[0]
    name = names[section[0]:].split(b"\0", 1)[0].decode()
    assert name == ".data", name  # app_sys.ld は .rodata* を .data へ統合。
    offset = section[4] + addr - section[3]
    d = gui_desc.parse_descriptor(data[offset:offset + size], symbols)
    assert d["usize_size"] == 4
    assert d["win_count"] == 16 and d["slot_count"] == 4 and d["sent_count"] == 4
    assert d["app_id_min"] == 2
    print("i686 descriptor:", json.dumps(d, sort_keys=True))
    print("i686 symbols:", json.dumps(symbols, sort_keys=True), "section=" + name)
    return d


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mutate", action="store_true")
    parser.add_argument("--i686-elf", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gui-desc-") as temp:
        out = Path(temp)
        rlib = os32api_host.build(out)
        normal = out / "normal"
        normal.mkdir()
        fixture = build_fixture(GSHELL, normal, rlib)
        d = roundtrip(*fixture)
        assert d["usize_size"] == 8, "host fixture is intentionally 64 bit"
        print("host descriptor:", json.dumps(d, sort_keys=True))
        print("expected JSON:", json.dumps(EXPECTED, sort_keys=True))
        negatives(*fixture[1:], d)
        if args.i686_elf:
            target_descriptor(args.i686_elf)
        if args.mutate:
            for name, old, new in (
                ("shift-owner-offset", "offset_of!(Win, owner) as u32", "offset_of!(Win, owner) as u32 + 1"),
                ("add-field-without-version", "pub const WORDS: usize = 31;", "pub const WORDS: usize = 32;"),
            ):
                gshell = out / name / "gshell"
                shutil.copytree(GSHELL / "src", gshell / "src")
                shutil.copytree(GSHELL / "host", gshell / "host")
                desc = gshell / "src/dbgdesc.rs"
                text = desc.read_text()
                assert text.count(old) == 1
                text = text.replace(old, new)
                if name == "add-field-without-version":
                    text = text.replace("\n];", "\n    0,\n];")
                desc.write_text(text)
                mutant = out / name / "dump"
                mutant.mkdir()
                fixture = build_fixture(gshell, mutant, rlib)
                try:
                    roundtrip(*fixture)
                except (AssertionError, gui_desc.DescriptorError) as exc:
                    print(f"RED: {name}: {exc}")
                else:
                    raise AssertionError(f"SURVIVED: {name}")
    print("gui descriptor tests PASS")


if __name__ == "__main__":
    main()
