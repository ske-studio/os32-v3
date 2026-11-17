#!/usr/bin/env python3
"""Pinned PowerPC nano inputs, build receipts and isolated cache keys."""
import argparse
import json
import os
import pathlib
import subprocess
import tarfile
import tempfile
from check_nano_inputs import require, digest, compare, sdk_inputs, Mismatch

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEDGER = ROOT / 'sdk/allocator/nano_inputs.json'
BUILDER = ROOT / 'tools/ci/build_cross_ppc.sh'
LICENSE = ROOT / 'sdk/allocator/nano.LICENSE'
RECEIPT = 'os32-nano-build-powerpc.json'
TARGET = 'powerpc-elf'
ABI = ['-mcpu=750', '-m32', '-mbig-endian', '-mhard-float',
       '-mcall-sysv', '-mno-eabi', '-mno-altivec', '-msdata=none']


def select(ledger):
    require(ledger['schema'] == 2, 'unknown ledger schema')
    selected = ledger['targets'][TARGET]
    require(selected['toolchain']['target'] == TARGET, 'ledger target differs')
    require(selected['toolchain']['abi'] == ABI, 'ledger ABI differs')
    return selected


def command(prefix, tool, *args):
    return subprocess.check_output([str(prefix / 'bin' / (TARGET + '-' + tool)),
                                    *map(str, args)], stderr=subprocess.PIPE)


def libraries(prefix):
    names = ('-print-file-name=libc.a', '-print-libgcc-file-name')
    paths = [pathlib.Path(command(prefix, 'gcc', name).decode().strip()).resolve() for name in names]
    require(all(path.is_file() for path in paths), 'GCC library resolution failed')
    return paths


def inventory(prefix, ledger):
    ledger = ledger["toolchain"]
    libc, libgcc = libraries(prefix)
    lib = libc.parent
    require(command(prefix, "gcc", "-print-multi-directory").decode().strip() == ledger["multilib"],
            "multilib differs")
    macros = command(prefix, "gcc", *ABI, "-dM", "-E", "-x", "c", "/dev/null")
    require(b"#define __BIG_ENDIAN__ 1" in macros and b"_SOFT_FLOAT" not in macros, "ABI differs")
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
    # PPC USER library search order, with a real -lc resolution.
    # Relocatable output is inspected only, never executed as a guest program.
    with tempfile.TemporaryDirectory(prefix="nano_link_") as work:
        work = pathlib.Path(work)
        c = work / "probe.c"
        symbols = list(ledger["symbols"])
        c.write_text("\n".join("extern char " + name + ";" for name in symbols)
                     + "\nvoid *probes[] = {" + ",".join("&" + name for name in symbols) + "};\n")
        command(prefix, "gcc", *ABI, "-c", c, "-o", work / "probe.o")
        command(prefix, "ld", "-m", "elf32ppc", "-r", "-L" + str(ROOT / "build/out/ppc-user/lib"),
                "-L" + str(lib), work / "probe.o", "-lc", "-o", work / "probe.elf",
                "-Map=" + str(work / "probe.map"))
        link_map = (work / "probe.map").read_text()
        for member in set(ledger["symbols"].values()):
            require(str(lib / "libc.a") + "(" + member + ")" in link_map,
                    "link target differs: " + member)
    return {"archives": archives, "members": members}


def config_prefix(config):
    invocation = next(line for line in config.read_text().splitlines() if line.startswith('  $ '))
    return next(flag.split('=', 1)[1] for flag in invocation.split() if flag.startswith('--prefix='))


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
    dry = subprocess.check_output(['bash', str(BUILDER), '--dry-run', '--prefix',
                                   str(config_prefix(config))], text=True)
    expected = next(line for line in dry.splitlines() if line.startswith('newlib configure ')).split()[2:]
    require(invocation.split()[2:] == expected, 'builder configure differs')
    for flag in ledger["configure"]:
        require(flag in invocation.split(), "configure flag missing: " + flag)
    require(not any(flag.startswith(("--disable-newlib-nano", "--enable-multilib"))
                    for flag in invocation.split()), "conflicting configure flags")
    makefile = config.with_name("Makefile").read_text()
    require("CFLAGS = " + ledger["cflags"] in makefile.splitlines(), "build CFLAGS differ")
    require("--target=" + ledger["target"] in invocation.split(), "configure target differs")
    require(ledger["patches"] == [], "patched build needs a new input ledger")
    return {"target": TARGET, "abi": ABI, "upstream": ledger["upstream"], "configure": ledger["configure"],
            "config_sha256": digest(config.read_bytes()),
            "builder_sha256": digest(BUILDER.read_bytes())}


def check(prefix, ledger, receipt=None):
    require(receipt is None or pathlib.Path(receipt).is_file(), 'explicit receipt missing')
    receipt = pathlib.Path(receipt) if receipt else prefix / RECEIPT
    inputs = ledger["toolchain"]
    expected = inputs["local"]
    if receipt.exists():
        expected = json.loads(receipt.read_text())
        require(expected["target"] == TARGET and expected["abi"] == inputs["abi"], "receipt target/ABI differs")
        require(expected["upstream"] == inputs["upstream"], "receipt upstream differs")
        require(expected["configure"] == inputs["configure"], "receipt configure differs")
        require(expected["builder_sha256"] == digest(BUILDER.read_bytes()),
                "receipt builder differs; recover using --record-built --source --tarball --config "
                "with preserved build inputs, or rebuild with tools/ci/build_cross_ppc.sh "
                "(docs/08_build.md section 8-6)")
    actual = inventory(prefix, ledger)
    try:
        compare(actual, expected)
    except Mismatch as exc:
        if not receipt.exists():
            raise Mismatch(str(exc) + "; receipt absent: pinned ledger is for the PPC reference host only; "
                           "DWARF build paths may differ. Recover using --record-built --source --tarball "
                           "--config with preserved build inputs, or rebuild with tools/ci/build_cross_ppc.sh "
                           "(docs/08_build.md section 8-6)") from exc
        raise
    return "build receipt" if receipt.exists() else "pinned local ledger"


def cache_key(ledger):
    """Key construction inputs and inventory names, excluding local hash values."""
    tc = ledger["toolchain"]
    inputs = {key: tc[key] for key in ("upstream", "configure", "gcc", "target", "cflags", "patches", "abi", "multilib")}
    inputs["builder_sha256"] = digest(BUILDER.read_bytes())
    inputs["members"] = sorted(tc["local"]["members"])
    inputs["archives"] = sorted(tc["local"]["archives"])
    return digest(json.dumps(inputs, sort_keys=True, separators=(",", ":")).encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get("PPC_CROSS_DIR", "/home/hight/opt/cross-ppc")))
    parser.add_argument("--ledger", type=pathlib.Path, default=LEDGER)
    parser.add_argument("--cache-key", action="store_true")
    parser.add_argument("--record-built", action="store_true")
    parser.add_argument("--source", type=pathlib.Path)
    parser.add_argument("--tarball", type=pathlib.Path)
    parser.add_argument("--config", type=pathlib.Path)
    parser.add_argument("--receipt", type=pathlib.Path)
    args = parser.parse_args()
    try:
        ledger = select(json.loads(args.ledger.read_text()))
        if args.cache_key:
            print(cache_key(ledger))
            return 0
        if args.record_built:
            require(all((args.source, args.tarball, args.config)), "record-built needs source/tarball/config")
            receipt = build_inputs(ledger, args.source, args.tarball, args.config)
            receipt.update(inventory(args.cross_dir, ledger))
            (args.receipt or args.cross_dir / RECEIPT).write_text(json.dumps(receipt, indent=2) + "\n")
        print("nano inputs PASS: " + check(args.cross_dir, ledger, args.receipt))
        return 0
    except (Mismatch, OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as exc:
        print("nano inputs FAIL: " + str(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
