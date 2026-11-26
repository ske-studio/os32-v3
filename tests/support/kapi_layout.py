"""Measure the public C definitions with the target ABI and verify Rust's view."""
import hashlib
import json
import os
import struct
from pathlib import Path
import re
import subprocess
import tempfile

from tests.support.media import Failure


def compiler(cpu, directory, source, options):
    target = "i386-none-elf" if cpu == "pc98" else "powerpc-none-elf"
    proc = subprocess.run(["clang", "-target", target, "-ffreestanding", "-Wno-ignored-attributes",
                           "-I", str(directory), "-x", "c", "-", *options],
                          input=source, capture_output=True, text=True)
    if proc.returncode:
        raise OSError(f"{target} could not measure C layout: {proc.stderr}")
    return proc.stdout


def layouts(cpu, directory):
    src = '#include "os32api.h"\n#include "os32_gui_shared.h"\n'
    ast = json.loads(compiler(cpu, directory, src, ["-fsyntax-only", "-Xclang", "-ast-dump=json"]))
    records = {node["id"]: node for node in ast["inner"] if node["kind"] == "RecordDecl"}
    fields = {}
    for node in ast["inner"]:
        if node["kind"] != "TypedefDecl" or not node.get("inner"):
            continue
        record = records.get(node["inner"][0].get("ownedTagDecl", {}).get("id"))
        if record and record.get("completeDefinition"):
            fields[node["name"]] = [field["name"] for field in record.get("inner", [])
                                    if field["kind"] == "FieldDecl" and "name" in field and
                                    not field.get("isBitfield")]
    for record in records.values():
        if record.get("completeDefinition") and record.get("name") and record.get("loc", {}).get("includedFrom"):
            name = record["tagUsed"] + " " + record["name"]
            fields[name] = [field["name"] for field in record.get("inner", [])
                            if field["kind"] == "FieldDecl" and "name" in field and not field.get("isBitfield")]
    entries = []
    expressions = []
    for name, members in fields.items():
        entries.extend((name, field) for field in ["(sizeof)", "(alignof)", *members])
        expressions.extend([f"sizeof({name})", f"__alignof__({name})"])
        expressions.extend(f"__builtin_offsetof({name}, {field})" for field in members)
    if "KernelAPI" not in fields:
        raise OSError(f"{cpu}: missing public table type")
    src += 'const unsigned int measurements[] __attribute__((section(".kapi_layout"))) = {'
    src += ", ".join(expressions) + "};\n"
    pc98 = cpu == "pc98"
    directory_var = "CROSS_DIR" if pc98 else "PPC_CROSS_DIR"
    toolchain = Path(os.environ.get(directory_var, Path.home() / ("opt/cross" if pc98 else "opt/cross-ppc")))
    prefix = str(toolchain / "bin" / ("i386-elf-" if pc98 else "powerpc-elf-"))
    flags = ["-march=i386"] if pc98 else ["-mcpu=7450", "-mbig-endian", "-msoft-float", "-fno-short-enums"]
    with tempfile.TemporaryDirectory() as tmp:
        obj, raw = Path(tmp) / "layout.o", Path(tmp) / "layout.bin"
        proc = subprocess.run([prefix + "gcc", "-m32", *flags, "-ffreestanding", "-Wno-attributes",
                               "-I", directory, "-x", "c", "-c", "-", "-o", obj],
                              input=src, text=True, capture_output=True)
        if proc.returncode:
            raise OSError(f"{cpu}: public layout probe: {proc.stderr}")
        subprocess.run([prefix + "objcopy", "--dump-section", f".kapi_layout={raw}", obj], check=True)
        values = struct.unpack(("<" if pc98 else ">") + str(len(entries)) + "I", raw.read_bytes())
    result = {}
    for (name, field), value in zip(entries, values, strict=True):
        result.setdefault(name, {})[field] = value
    return result


def rust_layout(cpu, measured):
    root = Path.cwd()
    crate = root / "sdk/rust/os32api"
    target = "i686" if cpu == "pc98" else "powerpc"
    expected = []
    for module in ("av", "install", "kapi_generated"):
        body = (crate / "src" / (module + ".rs")).read_text()
        for name, definition in re.findall(r"pub struct (\w+)\s*\{(.*?)^\}", body, re.S | re.M):
            if name not in measured:
                continue
            layout = measured[name]
            ty = f"os32api::{module}::{name}"
            expressions = [("(sizeof)", f"size_of::<{ty}>()"), ("(alignof)", f"align_of::<{ty}>()")]
            for field in re.findall(r"\bpub (\w+):", definition):
                if field in layout:
                    expressions.append((field, f"offset_of!({ty}, {field})"))
            expected.extend(f'const _: () = assert!({expr} == {layout[field]}, "{name}.{field}");'
                            for field, expr in expressions)
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        (folder / "Cargo.toml").write_text('[package]\nname="measure_sdk"\nversion="0.1.0"\nedition="2021"\n'
            '[lib]\npath="measure.rs"\n[dependencies]\nos32api={path=' + json.dumps(str(crate)) + '}\n')
        (folder / "measure.rs").write_text("#![no_std]\nuse core::mem::{align_of,offset_of,size_of};\n" +
                                           "\n".join(expected))
        spec = root / f"sdk/rust/{target}-os32-none.json"
        version = subprocess.check_output(["rustc", "-vV"])
        key = hashlib.sha256(version + b"\0" + spec.read_bytes()).hexdigest()
        cache = Path(os.environ.get("OS32_KAPI_CACHE", Path(tempfile.gettempdir()) / "kapi-targets"))
        cmd = ["cargo", "check", "--manifest-path", folder / "Cargo.toml", "-Zjson-target-spec",
               "-Zbuild-std=core,alloc,compiler_builtins", "--target", spec]
        env = dict(os.environ, CARGO_TARGET_DIR=str(cache / spec.stem / key))
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if proc.returncode:
            env["CARGO_TARGET_DIR"] = str(folder / "target")
            proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if proc.returncode and not any(msg in proc.stderr for msg in
                                       ("evaluation of constant value failed", "evaluation panicked")):
            # Retry independently of restored build products and crate downloads.
            env.update(CARGO_TARGET_DIR=str(folder / "retry-target"), CARGO_HOME=str(folder / "cargo-home"))
            proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if proc.returncode:
            if "evaluation of constant value failed" in proc.stderr or "evaluation panicked" in proc.stderr:
                raise Failure(f"{cpu} Rust layout mismatch: {proc.stderr}")
            raise OSError(f"{cpu} Rust compiler: {proc.stderr}")
