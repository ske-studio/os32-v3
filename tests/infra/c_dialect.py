"""Probe the language and diagnostics selected by product compile recipes."""
from collections import defaultdict
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, make
from tests.support.product import flags, recipes, sqlite_directory
from tools.ownership import Inventory


def probe(args, source):
    return subprocess.run([*args, "-x", "c", "-"], input=source, text=True, capture_output=True)


def options(argv, source):
    result = [argv[0]]
    it = iter(argv[1:])
    for arg in it:
        if arg in ("-o", "-MT", "-MF", "-MQ"):
            next(it)
        elif arg not in (source, "-MMD", "-MD", "-MP", "-c"):
            result.append(arg)
    return tuple(result)


def verify(config):
    objects, _ = recipes(config, ("all",))
    groups = defaultdict(set)
    sqlite = {"lib/sqlite3/sqlite3.c", "build/out/sqlite-proto/os32_sqlite_vfs.c",
              "userland/diag/sqlite_standalone/sqlite_user_vfs.c",
              "userland/diag/sqlite_standalone/sqlite_standalone.c"}
    if config != "pc98":
        cache = sqlite_directory(config, Path.cwd())
        if cache:
            sqlite.add(str(cache[0] / "gen/sqlite/os32_sqlite_vfs.c"))
    for source, argv in objects.values():
        if source.endswith(".c"):
            groups[options(argv, source), source in sqlite, source == "lib/sqlite3/sqlite3.c"].add(source)
    errors = []
    inventory = Inventory(Path.cwd())
    for source, argv in objects.values():
        if not source.endswith(".c") or (source in inventory.paths and inventory.lookup(source)["kind"] == "第三者の物"):
            continue
        proc = subprocess.run([*options(argv, source), "-Werror=old-style-definition", "-fsyntax-only", source],
                              capture_output=True, text=True)
        if proc.returncode:
            if "old-style-definition" not in proc.stderr:
                raise OSError(f"{source}: {proc.stderr}")
            errors.append(f"{source}: old-style-definition")
    if not any(vendor for _, _, vendor in groups) or not any(not sql for _, sql, _ in groups):
        raise OSError("missing SQLite or internal product recipes")
    negatives = {"implicit-int": "extern c3_value;",
                 "implicit-function-declaration": "int c3_f(void){return c3_missing();}",
                 "vla": "int c3_f(int n){int a[n];return sizeof(a);}"}
    for (args, sql, vendor), sources in groups.items():
        normal = probe([*args, "-fsyntax-only"], "int c3_probe(void){return 0;}")
        if normal.returncode:
            raise OSError(normal.stderr)
        macros = probe([*args, "-dM", "-E"], "")
        if macros.returncode:
            raise OSError(macros.stderr)
        version = [l for l in macros.stdout.splitlines() if l.startswith("#define __STDC_VERSION__ ")]
        issues = []
        if version != ([] if sql else ["#define __STDC_VERSION__ 201112L"]) or "#define __STRICT_ANSI__ " in macros.stdout:
            issues.append("expected GNU89" if sql else "expected GNU11")
        if not vendor:
            for diagnostic, body in negatives.items():
                proc = probe([*args, "-fsyntax-only"], body)
                if not proc.returncode or diagnostic not in proc.stderr:
                    issues.append("missing diagnostic " + diagnostic)
        errors.extend(f"{s}: {issue}" for s in sorted(sources) for issue in issues)
    headers = sorted(Path("sdk/include/os32").glob("*.h"))
    if not headers:
        raise OSError("missing SDK headers")
    cc = next(iter(groups))[0][0]
    for h in headers:
        for standard in ("gnu89", "gnu11"):
            args = [cc, "-ffreestanding", "-Isdk/include", "-Isdk/include/os32", "-std=" + standard, "-fsyntax-only"]
            if standard == "gnu89":
                args += ["-Werror=c90-c99-compat", "-Werror=declaration-after-statement"]
            entry = "os32_kapi_shared.h" if h.name in ("os32_kapi_generated.h", "os32_ver_info.h") else h.name
            proc = probe(args, f'#include <os32/{entry}>\n')
            if proc.returncode:
                errors.append(f"{h}: {standard}: {proc.stderr.strip()}")
    if errors:
        raise Failure("\n".join(errors))


if __name__ == "__main__":
    main(lambda config: make(*flags(config), "all"), verify)
