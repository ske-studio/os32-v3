"""Resolve the stage roster against the linked kernel and module images."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

from tests.support.media import Failure, make

FIXTURE = Path("tests/support/fixtures/gfx2_final")
IMAGES = (("kernel", "build/out/kernel"), ("kernel", "build/out/r1/kernel"),
          ("geforce", "build/out/geforce/geforce"))


def readelf(*args):
    return subprocess.check_output(["readelf", "-W", *args], text=True)


def input_sections(mapfile):
    body = mapfile.read_text().partition("Linker script and memory map")[2]
    contributions = []
    section = None
    for line in body.splitlines():
        if match := re.match(r"^(\.\S+)", line):
            section = match[1]
        match = re.fullmatch(r"\s*(?:[.\w-]+\s+)?(0x[\da-fA-F]+)\s+(0x[\da-fA-F]+)\s+(\S+\.o)", line)
        if section and match:
            address, size = (int(v, 16) for v in match.groups()[:2])
            contributions.append((section, address, address+size, Path(match[3])))
    if not contributions:
        raise Failure(f"unreadable input sections {mapfile}")
    return contributions


def validate(elf, mapfile, expected):
    if not expected:
        raise Failure("empty provider roster")
    sections = dict(re.findall(r"^\s*\[\s*(\d+)\]\s+(\.\S+)", readelf("-S", elf), re.M))
    spans = input_sections(mapfile)
    # Identify a placeholder object by its other weak HAL providers. This also
    # recognizes a strong definition placed inside the same linked input.
    roster = {line.split()[-1] for line in (FIXTURE / "providers.txt").read_text().splitlines()
              if line.startswith(("I1 ", "I2 ", "I3 "))}
    placeholder = {}
    seen = set()
    for line in readelf("-s", elf).splitlines():
        fields = line.split()
        if len(fields) != 8 or fields[-1] not in expected:
            continue
        _, value, _, kind, binding, _, index, name = fields
        if binding != "GLOBAL" or kind != expected[name] or index not in sections:
            raise Failure(f"provider binding/type {name}")
        address = int(value, 16)
        owners = [obj for section, lo, hi, obj in spans if section == sections[index]
                  and lo <= address < hi]
        if len(owners) != 1:
            raise Failure(f"missing input section {name}")
        obj = owners[0]
        if obj not in placeholder:
            symbols = (line.split() for line in readelf("-s", obj).splitlines())
            placeholder[obj] = any(len(s) == 8 and s[4] == "WEAK" and s[6] != "UND"
                                   and s[7] in roster for s in symbols)
        if placeholder[obj]:
            raise Failure(f"placeholder provider {name}")
        seen.add(name)
    if missing := expected.keys() - seen:
        raise Failure("undefined providers " + " ".join(sorted(missing)))


def probe_reader():
    # The global definition has the correct ELF type but comes from the stub.
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        obj, elf, linkmap = (root / n for n in ("stub.o", "probe.elf", "probe.map"))
        subprocess.run(["as", "--32", "-o", obj], input=".text\n.globl test_provider\n"
                       ".type test_provider,@function\ntest_provider: ret\n"
                       ".weak gfx2_active\n.type gfx2_active,@function\ngfx2_active: ret\n", text=True, check=True)
        subprocess.run(["ld", "-m", "elf_i386", "-e", "test_provider", "-Map", linkmap,
                        "-o", elf, obj], check=True)
        try:
            validate(elf, linkmap, {"test_provider": "FUNC"})
        except Failure as exc:
            if str(exc) == "placeholder provider test_provider":
                return
            raise
        raise Failure("reader accepted strong placeholder")


def verify(stage):
    rows = [line.split() for line in (FIXTURE / "providers.txt").read_text().splitlines()
            if line.startswith(stage + " ")]
    if stage == "I1":
        probe_reader()
    # The runner supplies a fresh output root for the control and each fault.
    out = Path(os.environ["OS32_VERIFY_OUT"]).resolve().relative_to(Path.cwd())
    images = [(owner, str(out / Path(prefix).relative_to("build/out")))
              for owner, prefix in IMAGES]
    make("BUILD_OUT=" + str(out), *(prefix + ".elf" for _, prefix in images))
    for owner, prefix in images:
        roster = {name: "OBJECT" if image.endswith("-data") else "FUNC"
                  for _, image, name in rows if image.split("-")[0] == owner}
        validate(Path(prefix+".elf"), Path(prefix+".map"), roster)
