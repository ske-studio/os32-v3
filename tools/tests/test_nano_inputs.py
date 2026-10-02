#!/usr/bin/env python3
"""f1a toolchain ledger regression. Log: tools/tests/nano_inputs_tdd.md
票: docs/tasks/v3/TASK_T2D_T2H.md §3-5 f1a
Relocatable i386 probes are inspected, never executed (no ILP32 runner needed).
"""
import copy
import importlib.util
import json
import os
import pathlib
import shutil
import sys
import tempfile
import io
import tarfile
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/check_nano_inputs.py"
LEDGER = ROOT / "sdk/allocator/nano_inputs.json"


def load(script):
    spec = importlib.util.spec_from_file_location("nano_check", script)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.ROOT = ROOT
    mod.LICENSE = ROOT / "sdk/allocator/nano.LICENSE"
    mod.BUILDER = ROOT / "tools/ci/build_cross.sh"
    return mod


def rejected(mod, fn, text):
    try:
        fn()
    except mod.Mismatch as exc:
        assert text in str(exc), str(exc)
        return str(exc)
    raise AssertionError("accepted mismatch: " + text)


def cases(script):
    mod = load(script)
    ledger = json.loads(LEDGER.read_text())
    tc = ledger["toolchain"]
    assert "license" in tc and "license" not in ledger["sdk_build"], "toolchain license scope"
    prefix = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
    mod.check(prefix, ledger)
    actual = mod.inventory(prefix, ledger)
    expected = copy.deepcopy(actual)
    expected["archives"]["libc.a"] = "0" * 64
    rejected(mod, lambda: mod.compare(actual, expected), "archives SHA256")
    expected = copy.deepcopy(actual)
    expected["members"]["libc_a-mallocr.o"] = "0" * 64
    rejected(mod, lambda: mod.compare(actual, expected), "members SHA256")
    bad = copy.deepcopy(ledger)
    bad["toolchain"]["symbols"]["_malloc_r"] = "libc_a-freer.o"
    rejected(mod, lambda: mod.inventory(prefix, bad), "provider differs")
    bad = copy.deepcopy(ledger)
    bad["toolchain"]["local"]["absent"] = ["libc.a"]
    rejected(mod, lambda: mod.inventory(prefix, bad), "unexpected archive")
    for key, message in (("gcc", "GCC version"), ("target", "GCC target")):
        bad = copy.deepcopy(ledger)
        bad["toolchain"][key] = "wrong"
        rejected(mod, lambda: mod.inventory(prefix, bad), message)
    original_command = mod.command
    def dlmalloc_command(prefix, tool, *args):
        data = original_command(prefix, tool, *args)
        if tool == "nm":
            data += b"libc.a:libc_a-mallocr.o:00000000 B __malloc_av_\n"
        return data
    mod.command = dlmalloc_command
    rejected(mod, lambda: mod.inventory(prefix, ledger), "dlmalloc present")
    mod.command = original_command
    # A valid shadow libc at the first -L must fail the real link-map check.
    with tempfile.TemporaryDirectory(prefix="nano_shadow_") as work:
        root = pathlib.Path(work)
        lib = root / "build/out/lib"
        lib.mkdir(parents=True)
        shutil.copyfile(prefix / "i386-elf/lib/libc.a", lib / "libc.a")
        mod.ROOT = root
        rejected(mod, lambda: mod.inventory(prefix, ledger), "link target differs")
        mod.ROOT = ROOT
    key = mod.cache_key(ledger)
    for field in ("sdk_build",):
        bad = copy.deepcopy(ledger)
        bad[field]["patches"] = ["sdk-only.patch"]
        if mod.cache_key(bad) != key:
            raise AssertionError("accepted mismatch: SDK changed cache key")
    for field in ("symbols",):
        bad = copy.deepcopy(ledger)
        bad["toolchain"][field] = {}
        if mod.cache_key(bad) != key:
            raise AssertionError("accepted mismatch: evidence changed cache key")
    for group in ("members", "archives"):
        bad = copy.deepcopy(ledger)
        bad["toolchain"]["local"][group] = dict.fromkeys(tc["local"][group], "0" * 64)
        if mod.cache_key(bad) != key:
            raise AssertionError("accepted mismatch: local hashes changed cache key")
        bad["toolchain"]["local"][group]["added"] = "0" * 64
        if mod.cache_key(bad) == key:
            raise AssertionError("accepted mismatch: local names omitted from cache key")
        bad = copy.deepcopy(ledger)
        bad["toolchain"]["local"][group] = dict(reversed(list(tc["local"][group].items())))
        if mod.cache_key(bad) != key:
            raise AssertionError("accepted mismatch: local name order changed cache key")
    for field in ("upstream", "configure", "gcc", "target", "cflags", "patches"):
        bad = copy.deepcopy(ledger)
        bad["toolchain"][field] = None
        if mod.cache_key(bad) == key:
            raise AssertionError("accepted mismatch: build input omitted from cache key")
    original_builder = mod.BUILDER
    with tempfile.TemporaryDirectory(prefix="nano_key_") as work:
        mod.BUILDER = pathlib.Path(work) / "builder.sh"
        mod.BUILDER.write_bytes(original_builder.read_bytes() + b"\n")
        if mod.cache_key(ledger) == key:
            raise AssertionError("accepted mismatch: builder omitted from cache key")
    mod.BUILDER = original_builder
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    assert "$(python3 tools/check_nano_inputs.py --cache-key)" in workflow
    assert "hashFiles('tools/ci/build_cross.sh'" not in workflow
    # Execute the real key step with a failing checker under Actions' bash -e.
    block = workflow.split("      - name: Toolchain cache key\n", 1)[1]
    block = block.split("        run: |\n", 1)[1].split("\n      - name:", 1)[0]
    block = "\n".join(line[10:] for line in block.splitlines()).replace("${{ runner.os }}", "test")
    with tempfile.TemporaryDirectory(prefix="nano_ci_") as work:
        work = pathlib.Path(work)
        stub = work / "python3"
        stub.write_text("#!/bin/sh\necho 'nano inputs FAIL: fixture'\nexit 1\n")
        stub.chmod(0o755)
        output = work / "output"
        env = dict(os.environ, PATH=str(work) + os.pathsep + os.environ["PATH"],
                   GITHUB_ENV=str(work / "env"), GITHUB_OUTPUT=str(output))
        result = subprocess.run(["bash", "-e", "-c", block], cwd=ROOT, env=env,
                                capture_output=True)
        assert result.returncode == 1 and not output.exists(), "CI swallowed checker failure"
    # Real artifact corruption with a valid archive: no compile/command error.
    with tempfile.TemporaryDirectory(prefix="nano_ledger_") as work:
        work = pathlib.Path(work)
        (work / "bin").symlink_to(prefix / "bin", target_is_directory=True)
        lib = work / "i386-elf/lib"
        lib.mkdir(parents=True)
        for name in tc["local"]["archives"]:
            shutil.copyfile(prefix / "i386-elf/lib" / name, lib / name)
        # Prefix receipt uses the current build's hashes even when CI differs.
        receipt = dict(actual, upstream=tc["upstream"], configure=tc["configure"],
                       builder_sha256=mod.digest(mod.BUILDER.read_bytes()))
        path = work / mod.RECEIPT
        path.write_text(json.dumps(receipt))
        mod.check(work, ledger)
        with (lib / "libc.a").open("ab") as archive:
            archive.write(b"\n")
        rejected(mod, lambda: mod.check(work, ledger), "archives SHA256")
        path.unlink()
        rejected(mod, lambda: mod.check(work, ledger), "receipt absent: pinned ledger is for the f1a reference host only")
        original_inventory = mod.inventory
        for message in ("provider differs", "dlmalloc present", "link target differs", "GCC version differs"):
            def failing_inventory(prefix, ledger):
                raise mod.Mismatch(message)
            mod.inventory = failing_inventory
            detail = rejected(mod, lambda: mod.check(work, ledger), message)
            if "receipt absent" in detail:
                raise AssertionError("accepted mismatch: receipt hint on inventory failure")
        mod.inventory = original_inventory
        shutil.copyfile(prefix / "i386-elf/lib/libc.a", lib / "libc.a")
        for key, message in (("builder_sha256", "builder"), ("upstream", "upstream"),
                             ("configure", "configure")):
            bad_receipt = copy.deepcopy(receipt)
            bad_receipt[key] = None
            path.write_text(json.dumps(bad_receipt))
            detail = rejected(mod, lambda: mod.check(work, ledger), "receipt " + message)
            if key == "builder_sha256" and "--record-built --source --tarball --config" not in detail:
                raise AssertionError("accepted mismatch: builder recovery omitted")
    # Build receipt creation uses a verified tarball and the actual build inputs.
    with tempfile.TemporaryDirectory(prefix="nano_source_") as work:
        work = pathlib.Path(work)
        source = work / "src"
        (source / "newlib/libc/stdlib").mkdir(parents=True)
        (source / "newlib/libc/stdlib/nano-mallocr.c").write_bytes(mod.LICENSE.read_bytes() + b"nano fixture\n")
        tarball = work / "source.tar.gz"
        with tarfile.open(tarball, "w:gz") as tar:
            data = (source / "newlib/libc/stdlib/nano-mallocr.c").read_bytes()
            info = tarfile.TarInfo("newlib-fixture/newlib/libc/stdlib/nano-mallocr.c")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        tiny = copy.deepcopy(ledger)
        tiny["toolchain"]["upstream"] = dict(version="fixture", sha256=mod.digest(tarball.read_bytes()),
                                files={"newlib/libc/stdlib/nano-mallocr.c": mod.digest(data)})
        config = work / "config.log"
        invocation = "  $ configure --target=i386-elf " + " ".join(tiny["toolchain"]["configure"]) + "\n"
        config.write_text(invocation)
        (work / "Makefile").write_text("CFLAGS = -g -O2\n")
        mod.build_inputs(tiny, source, tarball, config)
        bad = copy.deepcopy(tiny)
        bad["toolchain"]["upstream"]["sha256"] = "0" * 64
        rejected(mod, lambda: mod.build_inputs(bad, source, tarball, config), "tarball SHA256")
        bad = copy.deepcopy(tiny)
        bad["toolchain"]["upstream"]["files"]["newlib/libc/stdlib/nano-mallocr.c"] = "0" * 64
        rejected(mod, lambda: mod.build_inputs(bad, source, tarball, config), "upstream source differs")
        original_license = mod.LICENSE
        mod.LICENSE = work / "nano.LICENSE"
        mod.LICENSE.write_bytes(b"wrong notice\n")
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "nano license differs")
        mod.LICENSE = original_license
        sdk_only = copy.deepcopy(tiny)
        sdk_only["sdk_build"]["patches"] = ["adapter.patch"]
        mod.build_inputs(sdk_only, source, tarball, config)
        (source / "newlib/libc/stdlib/nano-mallocr.c").write_bytes(b"patched\n")
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "build source differs")
        (source / "newlib/libc/stdlib/nano-mallocr.c").write_bytes(data)
        config.write_text(invocation.replace("--enable-newlib-nano-malloc", ""))
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "configure flag missing")
        config.write_text(invocation.rstrip() + " --disable-newlib-nano-malloc\n")
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "conflicting configure")
        config.write_text(invocation)
        (work / "Makefile").write_text("CFLAGS = -O0\n")
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "build CFLAGS differ")
        (work / "Makefile").write_text("CFLAGS = -g -O2\n")
        config.write_text(invocation.replace("--target=i386-elf", "--target=arm-elf"))
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "configure target differs")
        config.write_text(invocation)
        tiny["toolchain"]["patches"] = ["unrecorded.patch"]
        rejected(mod, lambda: mod.build_inputs(tiny, source, tarball, config), "patched build")
    mod.sdk_inputs(ledger)
    for field, name, message in (
            ("sources", "sdk/allocator/nano_adapter.c", "SDK source differs"),
            ("upstream", "newlib/libc/stdlib/nano-mallocr.c", "SDK upstream differs")):
        bad = copy.deepcopy(ledger)
        bad["sdk_build"][field][name] = "0" * 64
        rejected(mod, lambda: mod.sdk_inputs(bad), message)
    bad = copy.deepcopy(ledger)
    bad["sdk_build"]["members"].append("untracked.o")
    rejected(mod, lambda: mod.sdk_inputs(bad), "SDK member absent")
    bad = copy.deepcopy(ledger)
    bad["sdk_build"]["patches"] = ["unrecorded.patch"]
    rejected(mod, lambda: mod.sdk_inputs(bad), "SDK source patch")
    return 55


MUTATIONS = [
    ('require(digest((ROOT / name).read_bytes()) == sha,', 'require(True,', 1, 'SDK source bypass'),
    ('require(ledger["toolchain"]["upstream"]["files"].get(name) == sha,',
     'require(True,', 1, 'SDK upstream bypass'),
    ('require(all(name in ledger["toolchain"]["local"]["members"] for name in sdk["members"]),',
     'require(True,', 1, 'SDK member bypass'),
    ('require(sdk["patches"] == [],', 'require(True,', 1, 'SDK patch bypass'),
    ('require((source / name).read_bytes() == data,', 'require(True,', 1, 'source bypass'),
    ('require(flag in invocation.split(),', 'require(True,', 1, 'build flag bypass'),
    ('require("CFLAGS = " + ledger["cflags"] in makefile.splitlines(),',
     'require(True,', 1, 'CFLAGS bypass'),
    ('require(actual[group] == expected[group],', 'require(True,', 1, 'hash bypass'),
    ('require(providers.get(symbol) == [member],', 'require(True,', 1, 'provider bypass'),
    ('require(not (lib / name).exists(),', 'require(True,', 1, 'archive-name bypass'),
    ('require(expected["builder_sha256"] == digest(BUILDER.read_bytes()),',
     'require(True,', 1, 'builder bypass'),
    ('require(expected["upstream"] == inputs["upstream"],', 'require(True,', 1, 'upstream bypass'),
    ('require(expected["configure"] == inputs["configure"],', 'require(True,', 1, 'configure bypass'),
    ('require(command(prefix, "gcc", "-dumpfullversion").decode().strip() == ledger["gcc"],',
     'require(True,', 1, 'GCC version bypass'),
    ('require(command(prefix, "gcc", "-dumpmachine").decode().strip() == ledger["target"],',
     'require(True,', 1, 'GCC target bypass'),
    ('require("__malloc_av_" not in providers,', 'require(True,', 1, 'dlmalloc bypass'),
    ('require(str(lib / "libc.a") + "(" + member + ")" in link_map,',
     'require(True,', 1, 'link map bypass'),
    ('require(not any(flag.startswith(("--disable-newlib-nano", "--enable-multilib"))',
     'require(not any(False', 1, 'conflicting flags bypass'),
    ('require("--target=" + ledger["target"] in invocation.split(),',
     'require(True,', 1, 'configure target bypass'),
    ('require(ledger["patches"] == [],', 'require(True,', 1, 'toolchain patches bypass'),
    ('require(digest(tarball.read_bytes()) == ledger["upstream"]["sha256"],',
     'require(True,', 1, 'tarball bypass'),
    ('require(digest(data) == sha,', 'require(True,', 1, 'upstream source bypass'),
    ('require(b"\\n".join(data.splitlines()[:27]) + b"\\n" == LICENSE.read_bytes(),',
     'require(True,', 1, 'license bypass'),
    ('("upstream", "configure", "gcc", "target", "cflags", "patches")',
     '("upstream", "gcc", "target", "cflags", "patches")', 1, 'cache input bypass'),
    ('sorted(tc["local"]["members"])', '[]', 1, 'cache member names bypass'),
    ('sorted(tc["local"]["archives"])', '[]', 1, 'cache archive names bypass'),
    ('sorted(tc["local"]["members"])', 'tc["local"]["members"]', 1, 'cache local hashes included'),
    ('actual = inventory(prefix, ledger)\n    try:',
     'try:\n        actual = inventory(prefix, ledger)', 1, 'inventory receipt hint regression'),
    ('receipt builder differs; recover using --record-built --source --tarball --config ',
     'receipt builder differs; ', 1, 'builder recovery omitted'),
]


def main():
    print("nano ledger: {} cases GREEN".format(cases(SCRIPT)))
    if "--mutate" in sys.argv:
        source = SCRIPT.read_text()
        red = 0
        for old, new, hits, name in MUTATIONS:
            assert source.count(old) == hits, "mutation hit count: " + name
            with tempfile.TemporaryDirectory(prefix="nano_mut_") as work:
                script = pathlib.Path(work) / "check_nano_inputs.py"
                changed = source.replace(old, new)
                compile(changed, str(script), "exec")  # syntax failure is ERROR
                script.write_text(changed)
                try:
                    cases(script)
                except AssertionError as exc:
                    assert str(exc).startswith("accepted mismatch:"), str(exc)
                    print("MUTATION RED: " + name)
                    red += 1
                else:
                    raise AssertionError("survived: " + name)
        print("nano ledger: {} runtime RED / 0 survived / 0 ERROR".format(red))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
