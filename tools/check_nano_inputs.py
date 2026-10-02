#!/usr/bin/env python3
"""Check pinned nano archives and link providers; record a verified CI build.

Build receipt hashes are specific to the build directory (DWARF paths). They
are integrity records, not signatures or a claim of byte-reproducible builds.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEDGER = ROOT / "sdk/allocator/nano_inputs.json"
BUILDER = ROOT / "tools/ci/build_cross.sh"
LICENSE = ROOT / "sdk/allocator/nano.LICENSE"
RECEIPT = "os32-nano-build.json"


class Mismatch(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Mismatch(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def command(prefix, tool, *args):
    return subprocess.check_output([str(prefix / "bin" / ("i386-elf-" + tool)),
                                    *map(str, args)], stderr=subprocess.PIPE)


def inventory(prefix, ledger):
    ledger = ledger["toolchain"]
    lib = prefix / "i386-elf/lib"
    archives = {name: digest((lib / name).read_bytes())
                for name in ledger["local"]["archives"]}
    members = {name: digest(command(prefix, "ar", "p", lib / "libc.a", name))
               for name in ledger["local"]["members"]}
    for name in ledger["local"]["absent"]:
        require(not (lib / name).exists(), "unexpected archive: " + name)
    require(command(prefix, "gcc", "-dumpfullversion").decode().strip() == ledger["gcc"],
            "GCC version differs")
    require(command(prefix, "gcc", "-dumpmachine").decode().strip() == ledger["target"],
            "GCC target differs")
    syms = command(prefix, "nm", "-A", lib / "libc.a").decode()
    providers = {}
    for line in syms.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in ("T", "B", "D", "C"):
            member = fields[0].rsplit(":", 2)[-2]
            providers.setdefault(fields[2], []).append(member)
    for symbol, member in ledger["symbols"].items():
        require(providers.get(symbol) == [member], "provider differs: " + symbol)
    require("__malloc_av_" not in providers, "dlmalloc present")
    # Same -L search order as PROGRAM_LDFLAGS, with a real -lc resolution.
    # Relocatable output is inspected only, never executed as a guest program.
    with tempfile.TemporaryDirectory(prefix="nano_link_") as work:
        work = pathlib.Path(work)
        c = work / "probe.c"
        symbols = list(ledger["symbols"])
        c.write_text("\n".join("extern char " + name + ";" for name in symbols)
                     + "\nvoid *probes[] = {" + ",".join("&" + name for name in symbols) + "};\n")
        command(prefix, "gcc", "-c", c, "-o", work / "probe.o")
        command(prefix, "ld", "-m", "elf_i386", "-r", "-L" + str(ROOT / "build/out/lib"),
                "-L" + str(lib), work / "probe.o", "-lc", "-o", work / "probe.elf",
                "-Map=" + str(work / "probe.map"))
        link_map = (work / "probe.map").read_text()
        for member in set(ledger["symbols"].values()):
            require(str(lib / "libc.a") + "(" + member + ")" in link_map,
                    "link target differs: " + member)
    return {"archives": archives, "members": members}


def compare(actual, expected):
    for group in ("archives", "members"):
        require(actual[group] == expected[group], group + " SHA256 differs from ledger")


def build_inputs(ledger, source, tarball, config):
    ledger = ledger["toolchain"]
    require(digest(tarball.read_bytes()) == ledger["upstream"]["sha256"], "tarball SHA256 differs")
    with tarfile.open(tarball) as tar:
        for name, sha in ledger["upstream"]["files"].items():
            data = tar.extractfile("newlib-" + ledger["upstream"]["version"] + "/" + name).read()
            if name == "newlib/libc/stdlib/nano-mallocr.c":
                require(b"\n".join(data.splitlines()[:27]) + b"\n" == LICENSE.read_bytes(),
                        "nano license differs")
            require(digest(data) == sha, "upstream source differs: " + name)
            require((source / name).read_bytes() == data, "build source differs: " + name)
    invocation = next(line for line in config.read_text().splitlines() if line.startswith("  $ "))
    for flag in ledger["configure"]:
        require(flag in invocation.split(), "configure flag missing: " + flag)
    require(not any(flag.startswith(("--disable-newlib-nano", "--enable-multilib"))
                    for flag in invocation.split()), "conflicting configure flags")
    makefile = config.with_name("Makefile").read_text()
    require("CFLAGS = " + ledger["cflags"] in makefile.splitlines(), "build CFLAGS differ")
    require("--target=" + ledger["target"] in invocation.split(), "configure target differs")
    require(ledger["patches"] == [], "patched build needs a new input ledger")
    return {"upstream": ledger["upstream"], "configure": ledger["configure"],
            "config_sha256": digest(config.read_bytes()),
            "builder_sha256": digest(BUILDER.read_bytes())}


def check(prefix, ledger):
    receipt = prefix / RECEIPT
    inputs = ledger["toolchain"]
    expected = inputs["local"]
    if receipt.exists():
        expected = json.loads(receipt.read_text())
        require(expected["upstream"] == inputs["upstream"], "receipt upstream differs")
        require(expected["configure"] == inputs["configure"], "receipt configure differs")
        require(expected["builder_sha256"] == digest(BUILDER.read_bytes()),
                "receipt builder differs; recover using --record-built --source --tarball --config "
                "with preserved build inputs, or rebuild with tools/ci/build_cross.sh "
                "(docs/08_build.md section 8-5)")
    actual = inventory(prefix, ledger)
    try:
        compare(actual, expected)
    except Mismatch as exc:
        if not receipt.exists():
            raise Mismatch(str(exc) + "; receipt absent: pinned ledger is for the f1a reference host only; "
                           "DWARF build paths may differ. Recover using --record-built --source --tarball "
                           "--config with preserved build inputs, or rebuild with tools/ci/build_cross.sh "
                           "(docs/08_build.md section 8-5)") from exc
        raise
    return "build receipt" if receipt.exists() else "pinned local ledger"


def cache_key(ledger):
    """Key construction inputs and inventory names, excluding local hash values."""
    tc = ledger["toolchain"]
    inputs = {key: tc[key] for key in ("upstream", "configure", "gcc", "target", "cflags", "patches")}
    inputs["builder_sha256"] = digest(BUILDER.read_bytes())
    inputs["members"] = sorted(tc["local"]["members"])
    inputs["archives"] = sorted(tc["local"]["archives"])
    return digest(json.dumps(inputs, sort_keys=True, separators=(",", ":")).encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross")))
    parser.add_argument("--ledger", type=pathlib.Path, default=LEDGER)
    parser.add_argument("--cache-key", action="store_true")
    parser.add_argument("--record-built", action="store_true")
    parser.add_argument("--source", type=pathlib.Path)
    parser.add_argument("--tarball", type=pathlib.Path)
    parser.add_argument("--config", type=pathlib.Path)
    args = parser.parse_args()
    try:
        ledger = json.loads(args.ledger.read_text())
        require(ledger["schema"] == 2, "unknown ledger schema")
        if args.cache_key:
            print(cache_key(ledger))
            return 0
        if args.record_built:
            require(all((args.source, args.tarball, args.config)), "record-built needs source/tarball/config")
            receipt = build_inputs(ledger, args.source, args.tarball, args.config)
            receipt.update(inventory(args.cross_dir, ledger))
            (args.cross_dir / RECEIPT).write_text(json.dumps(receipt, indent=2) + "\n")
        print("nano inputs PASS: " + check(args.cross_dir, ledger))
        return 0
    except (Mismatch, OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as exc:
        print("nano inputs FAIL: " + str(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
