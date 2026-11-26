"""Read GNU ld's cross-reference map and GCC's declarations for selected objects."""
from pathlib import Path
import re
import os
import subprocess

from tests.support.product import commands


def references(config, links, folder):
    for output, original in commands(config):
        if output not in links:
            continue
        args = original.copy()
        index = args.index("-o")
        dest = folder / Path(output).name
        args[index + 1] = str(dest)
        mapfile = dest.with_suffix(".cref")
        gcc = any(x.endswith("-elf-gcc") for x in args)
        args[index:index] = (["-Wl,--cref,-Map," + str(mapfile)] if gcc else
                             ["--cref", "-Map=" + str(mapfile)])
        # Override any earlier or later map argument from the normal build.
        for i, arg in enumerate(args):
            if arg.startswith("-Wl,-Map,"):
                args[i] = "-Wl,-Map," + str(mapfile)
            elif arg.startswith("-Map="):
                args[i] = "-Map=" + str(mapfile)
        subprocess.run(args, check=True)
        lines = mapfile.read_text(errors="replace").split("Cross Reference Table\n")
        if len(lines) != 2:
            raise OSError(f"{output} missing linker cross references")
        definition = None
        for line in lines[1].splitlines():
            if not line.strip() or line.startswith("Symbol "):
                continue
            if line[0].isspace():
                if definition:
                    yield output, line.strip(), *definition
            else:
                words = line.split(None, 1)
                definition = words if len(words) == 2 else None


def declarations(cmd, aux):
    if not cmd or not any(x.endswith(".c") for x in cmd):
        return []
    args = cmd.copy()
    index = args.index("-o")
    del args[index:index+2]
    args = [x for x in args if x not in ("-c", "-MMD", "-MP")]
    subprocess.run([*args, "-fsyntax-only", "-aux-info", aux], check=True)
    result = []
    for line in aux.read_text().splitlines():
        m = re.match(r"/\* (.+?):\d+:[A-Z]([CF]) \*/ extern (.+?)(\w+) \((.*?)\);", line)
        if m:
            header, role, ret, name, args = m.groups()
            result.append((os.path.normpath(header), role, name, (ret.strip(), args)))
    return result


def symbols(obj):
    proc = subprocess.run(["nm", "-P", "-g", obj], text=True, capture_output=True)
    if proc.returncode:
        raise OSError(f"cannot inspect {obj}: {proc.stderr}")
    return {fields[0]: fields[1] for line in proc.stdout.splitlines() if len(fields := line.split()) >= 2}


def data_declarations(cmd, config):
    """Use compiler source positions to distinguish own declarations from includes."""
    import bisect
    import json
    if not cmd or not any(x.endswith(".c") for x in cmd):
        return []
    args = cmd.copy()
    out = args.index("-o")
    del args[out:out+2]
    args = [x for x in args if x not in ("-c", "-MMD", "-MP")]
    cpp = subprocess.check_output([*args, "-E"])
    positions, files = [], []
    for match in re.finditer(rb'^# \d+ "([^"]+)".*$', cpp, re.M):
        positions.append(match.end())
        files.append(os.path.normpath(match[1].decode()))
    cpu = "i386-unknown-linux-gnu" if config == "pc98" else "powerpc-none-elf"
    result = subprocess.run(["clang", "-target", cpu, "-x", "cpp-output", "-ffreestanding",
                             "-Wno-everything", "-fsyntax-only", "-Xclang", "-ast-dump=json", "-"],
                            input=cpp, capture_output=True)
    if result.returncode:
        raise OSError(f"{args[-1]}: cannot obtain declaration types: {result.stderr.decode()[-500:]}")
    records = []
    for decl in json.loads(result.stdout)["inner"]:
        if decl["kind"] not in ("VarDecl", "FunctionDecl") or decl.get("storageClass") == "static":
            continue
        location = decl["loc"]
        if "offset" not in location:
            continue
        pos = bisect.bisect_right(positions, location["offset"]) - 1
        if pos >= 0 and "mangledName" in decl:
            ty = decl["type"]
            defined = (decl.get("storageClass") != "extern" if decl["kind"] == "VarDecl" else
                       any(n["kind"] in ("CompoundStmt", "AliasAttr") for n in decl.get("inner", [])))
            signature = (decl["kind"], ty.get("desugaredQualType", ty["qualType"]))
            records.append((files[pos], decl["mangledName"], signature, defined))
    return records
