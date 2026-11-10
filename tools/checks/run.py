#!/usr/bin/env python3
"""Declaration-only check selection and execution; docs/design/tools/checks/run.md."""
from __future__ import annotations

import argparse
import ast
from bisect import bisect_left
from collections.abc import Mapping
from dataclasses import asdict, dataclass, replace
import fnmatch
from functools import lru_cache
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BASE_REFS = ("main", "origin/main", "feat/gui", "origin/feat/gui")
IGNORE = ("docs/hw", "docs/hw/**", ".claude/**")
# Build/toolchain inputs and infrastructure used across check recipes.
FULL = ("Makefile", "build/*.mk",
    '.gitmodules',
    'requirements.txt',
    'tools/check_tree_unchanged.py',
    'tools/fep_compiler.py',
    'tools/gen_sdk_manifest.py',
    'tools/mkd88.py',
    'tools/mkfat12.py',
    'tools/jobs.sh',
    'tools/gfinit_source.py',
    'tools/gfinit_rom_check.py',
    'tools/emu_agent/tasks/regress.txt',
    'tools/screenshot.py',
    'tools/net_l0_test.py',
    'tools/net_l3_test.py',
    'tools/sync_docs_to_win.sh',
)
DOCS = ("**/*.md", "docs/.orphans-allow", "docs/images/**", "docs/logs/**")
# No automated check exists for these repository metadata, saved observations,
# sample assets and standalone operational/conversion tools. Keep names explicit
# so adding a different file still requires a coverage decision.
NOTEST = (
    # PowerPC port (docs/tasks/ppc/): no ppc toolchain or QEMU on the CI runner, so
    # its build and tests stay outside `make check` until the kernel body builds.
    ('arch/ppc/**', ()),
    ('platform/powermac/**', ()),
    ('tools/ppc/**', ()),
    ('build/ppc.mk', ()),
    ('lib/microtar/LICENSE', ()),
    ('lib/microtar/README.OS32', ()),
    ('lib/zlib/README.OS32', ()),
    ('.gitattributes', ()),
    ('.gitignore', ()),
    ('.ignore', ()),
    ('.mcp.json', ()),
    ('LICENSE', ()),
    ('docs/archive/v21/v86v2/data/bda_hddboot.json', ()),
    ('docs/archive/v21/v86v2/data/trace_boot.json', ()),
    ('docs/archive/v21/v86v2/data/trace_write.json', ()),
    ('docs/tasks/portability/data/kstr_bench_2026-09-17_run1.txt', ()),
    ('docs/tasks/portability/data/kstr_bench_2026-09-17_run2.txt', ()),
    ('docs/tasks/portability/data/kstr_bench_2026-09-17_run3.txt', ()),
    ('sample/Gemini_Generated_Image_2d9f9k2d9f9k2d9f.jpg', ()),
    ('sample/Gemini_Generated_Image_2sdhsx2sdhsx2sdh.jpg', ()),
    ('sample/Gemini_Generated_Image_dm5n0hdm5n0hdm5n.jpg', ()),
    ('sample/README.OS32', ()),
    ('sample/dog.vbz', ()),
    ('sample/dragon.vbz', ()),
    ('sample/dragon8.vbz', ()),
    ('sample/dragon_hb.vbz', ()),
    ('sample/runner.vbz', ()),
    ('sample/runner8.vbz', ()),
    ('sample/runner_hb.vbz', ()),
    ('tools/audit_cast_align.sh', ()),
    ('tools/create_fat12_d88.sh', ()),
    ('tools/emu_agent/mcp_server.py', ()),
    ('tools/emu_agent/tasks/v86_dos.txt', ()),
    ('tools/gen_unicode.py', ()),
    ('tools/img2mgx.py', ()),
    ('tools/img2vbz.py', ()),
    ('tools/kstr_bench_report.py', ()),
    ('tools/lz4_compress.py', ()),
    ('tools/move_docs.py', ()),
    ('tools/np21w_restart.py', ()),
    ('tools/os32_server.py', ()),
    ('tools/review_local.py', ()),
    ('tools/split_c_functions.py', ()),
    ('tools/sync_hwdocs.sh', ()),
    ("userland/tests/bench_scale2x/main.c", ()),
    ("userland/cmds/**", tuple("userland/cmds/" + n + ".c" for n in
        ("cfg", "hclip", "hdate", "lpr", "stat", "tar", "wget"))),
    ("docs/manpages/*.1", ("docs/manpages/hsync.1",)),
    ("userland/lib/md/**", ()), ("userland/lib/tilemap/**", ()),
) + tuple(("userland/lib/" + p + ".c", ()) for p in (
    "asset/asset_cache", "asset/asset_core", "asset/asset_loader", "db/libos32db",
    "ecs/ecs_component", "ecs/ecs_core", "ecs/ecs_query", "ecs/ecs_system",
    "filer/filer_core", "filer/filer_draw", "input/input_bind", "input/input_config",
    "input/input_core", "input/input_ctx", "input/input_query", "math/atan2",
    "math/fix16", "math/lerp", "math/random", "math/recip", "math/sqrt", "math/trig",
    "math/vec2", "mgx/mgx_blit", "mgx/mgx_decode", "save/save_core", "save/save_meta",
    "snd/libos32snd", "ui/libos32ui_core", "ui/microui"))
SHARED_LAYERS = frozenset(("include", "kapi", "mm", "exec", "lib", "platform"))
OUTSIDE = frozenset(("arm-compile", "gfinit-fixture-host", "e10c-lifecycle",
    "host-agent", "host-lib-host", "net-link-host", "lan-bridge-host"))

KAPI_GENERATED = (
    "include/kapi_call_generated.h",
    "sdk/include/os32/os32_kapi_generated.h", "sdk/include/os32/os32_kapi_slots.h",
    "kapi/kapi_generated.c", "exec/exec_kapi_init.inc",
    "sdk/include/os32/os32_generations.h", "sdk/os32_generations.py",
    "sdk/rust/os32api/src/generations.rs", "sdk/include/os32/os32_unit_stamp.h",
    "sdk/crt/generations.inc", "sdk/link/generations.ld",
    "sdk/link/app.ld", "sdk/link/app_sys.ld", "sdk/link/shlib.ld", "build/os32.ld",
    "sdk/rust/os32api/src/kapi_generated.rs",
)


BUILD_CONFIG_GLOBS = ("tools/build_profile.py", "**/Makefile", "build/**/*.mk", "**/Cargo.lock",
                      "**/.cargo/config", "**/.cargo/config.toml", "**/Cargo.toml",
                      "**/rust-toolchain", "**/rust-toolchain.toml", "**/*-target.json",
                      "sdk/rust/i686-os32-none.json")

@lru_cache(maxsize=4096)
def glob_re(pat):
    """`**` はディレクトリを跨ぐ、`*` `?` は跨がない。`[...]` はそのまま。"""
    out, i = "", 0
    while i < len(pat):
        c = pat[i]
        if pat.startswith("**/", i):
            out += "(?:.*/)?"
            i += 3
        elif pat.startswith("**", i):
            out += ".*"
            i += 2
        elif c == "*":
            out += "[^/]*"
            i += 1
        elif c == "?":
            out += "[^/]"
            i += 1
        elif c == "[":
            j = pat.index("]", i)
            out += pat[i:j + 1]
            i = j + 1
        else:
            out += re.escape(c)
            i += 1
    return re.compile(out + r"\Z")

def norm(p):
    return os.path.normpath(p).replace(os.sep, "/")

PY_STR = re.compile(r"""(?:[rbfRBF]{0,2})(['"])([^'"\n]{1,200})\1""")
PY_JOIN = re.compile(
    r"""(?:(?:['"][\w.+-][\w./+-]*['"])\s*(?:/|,)\s*)+['"][\w.+-][\w./+-]*['"]""")
C_INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
RS_PATH = re.compile(r'#\[path\s*=\s*"([^"]+)"\]')
PY_IMPORT = re.compile(r"^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w., ]+))",
                       re.M)
TOML_PATH = re.compile(r'path\s*=\s*"([^"]+)"')

SCAN_ROOTS = ("tools/", "userland/gshell/host/")   # ここの下は中身まで辿る
# C の実装は場所を問わず #include "..." を多段に辿る。実装の .c が取り込む
# .inc (コードの断片、hsync.c → hsync_protect.inc など) を表から落とさないため
# (代行レビュー P2-1、2026-09-26)。
C_EXTS = (".c", ".h", ".inc")
# "/" を含まない裸のファイル名でも、この拡張子なら ROOT 相対の候補にする
# (check_kapi_version.py の README.md、check_manifests.py の CLAUDE.md — P2-2)。
BARE_EXTS = (".md", ".yaml", ".yml", ".json", ".tsv", ".c", ".h", ".inc")


def git(root, *args):
    proc = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    if proc.returncode:
        raise ValueError("git " + " ".join(args) + ": " + proc.stderr.strip())
    return proc.stdout.splitlines()


def tracked(root):
    return set(git(root, "ls-files")) | set(git(root, "ls-files", "--others", "--exclude-standard"))


def tracked_under(d, files):
    d = d.rstrip("/") + "/"
    i = bisect_left(files, d)
    found = []
    while i < len(files) and files[i].startswith(d):
        found.append(files[i])
        i += 1
    return found


class Extractor:
    """1 つの検査の入力を静的に拾う。

    C の `#include "x.h"` は、取り込む側の場所 → ROOT → **-I の探索先** の順に
    探す。探索先はその検査の recipe の `-I<dir>` と、辿った試験スクリプトの
    文字列 (`"-Iinclude"`、`"-I" + str(ROOT / p) for p in ("include", "fs")` の
    "include" "fs" のような、追跡されている .h を持つディレクトリ名) から集める。
    探索先が出そろってから C を読むため、C の走査は後回しの列 (pending) に積む。
    どの探索先で見つかるかは -I の順で決まるが、順は追わずに**当たったもの全部**
    を入力に数える (多めに数える = 変異を回す側に倒れる)。"""

    def __init__(self, root, snapshot=None):
        self.root = Path(root)
        self.tracked = tracked(self.root) if snapshot is None else snapshot
        self.header_dirs = {os.path.dirname(f) for f in self.tracked
                            if f.endswith((".h", ".inc")) and "/" in f}
        self.seen = set()
        self.found = set()
        self.inc_dirs = set()
        self.pending = []
        # One sorted snapshot per extraction, never shared between roots or
        # fixture edits. Directory lookups visit only their matching range.
        self._tracked = sorted(self.tracked)

    def add(self, rel):
        rel = norm(rel)
        if rel.startswith("../") or not rel in self.tracked:
            return
        self.found.add(rel)
        if rel == "tools/checks/run.py":
            return
        if rel in self.seen:
            return
        self.seen.add(rel)
        if rel.endswith(C_EXTS):
            self.pending.append(rel)
        elif rel.startswith(SCAN_ROOTS) or rel.endswith("/Cargo.toml") \
                or "/host_tests/" in rel:
            self.scan(rel)

    def add_inc_dir(self, d):
        d = norm(d) if d not in ("", ".") else ""
        if d.startswith("../") or os.path.isabs(d):
            return
        if d == "" or d in self.header_dirs:
            self.inc_dirs.add(d)

    def drain(self):
        """後回しにした C を、集まった -I の探索先で読む。"""
        while self.pending:
            self.scan(self.pending.pop())

    def add_dir(self, rel):
        for f in tracked_under(norm(rel), self._tracked):
            self.add(f)

    def resolve(self, cand, base_dir):
        """ROOT 相対・ファイルの場所相対の順に、追跡されているものを探す。"""
        for base in ("", base_dir):
            rel = norm(os.path.join(base, cand))
            if rel in self.tracked:
                return ("f", rel)
            if tracked_under(rel, self._tracked) and rel not in (".", ""):
                return ("d", rel)
        return None

    def scan(self, rel):
        path = os.path.join(self.root, rel)
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            return
        d = os.path.dirname(rel)
        if rel.endswith(".py"):
            # CHECK paths are declarations, not recipe inputs. Seed their
            # explicit .c/.py entries in extract(), as the old selector did.
            lines = text.splitlines(keepends=True)
            for node in ast.parse(text).body:
                if isinstance(node, (ast.Assign, ast.AnnAssign)) and any(
                        isinstance(t, ast.Name) and t.id == "CHECK" for t in
                        (node.targets if isinstance(node, ast.Assign) else [node.target])):
                    lines[node.lineno - 1:node.end_lineno] = ["\n"] * (node.end_lineno - node.lineno + 1)
            text = "".join(lines)
            self.scan_py(text, d)
        elif rel.endswith(C_EXTS):
            # "..." はまず取り込む側の場所から探し (C の規則)、無ければ ROOT と
            # -I の探索先 (self.inc_dirs) で当たったものを全部数える。
            # <...> は追わない (システムのヘッダ)。循環は add() の seen で止まる。
            for inc in C_INC.findall(text):
                own = norm(os.path.join(d, inc))
                if own in self.tracked:
                    self.add(own)
                    continue
                for base in sorted(self.inc_dirs | {""}):
                    rel2 = norm(os.path.join(base, inc))
                    if rel2 in self.tracked:
                        self.add(rel2)
        elif rel.endswith(".rs"):
            for p in RS_PATH.findall(text):
                self.add(os.path.join(d, p))
        elif rel.endswith("Cargo.toml"):
            self.add_crate(d)
            for p in TOML_PATH.findall(text):
                self.add_crate(norm(os.path.join(d, p)))

    def add_crate(self, d):
        if ("crate", d) in self.seen:
            return
        self.seen.add(("crate", d))
        for f in tracked_under(d, self._tracked):
            if f.endswith((".rs", ".toml")):
                self.add(f)

    def scan_py(self, text, d):
        cands = set()
        if re.search(r"""['"]-I""", text):
            # -I の探索先: "-I<dir>" の文字列と、.h を持つディレクトリ名の文字列
            for _, s in PY_STR.findall(text):
                s = s.strip()
                if s.startswith("-I"):
                    self.add_inc_dir(s[2:])
                elif " " not in s and not s.startswith("/"):
                    self.add_inc_dir(s)
        for m in PY_JOIN.finditer(text):
            parts = re.findall(r"""['"]([^'"]+)['"]""", m.group(0))
            cands.add("/".join(parts))
        for _, s in PY_STR.findall(text):
            if " " in s:
                continue
            if "/" in s or s.endswith(BARE_EXTS):
                cands.add(s)
        for c in cands:
            c = c.strip()
            if not c or c.startswith("/") or "*" in c or "$" in c:
                continue
            r = self.resolve(c, d)
            if not r:
                continue
            if r[0] == "f":
                self.add(r[1])
            elif r[1].startswith("tools/tests/") and r[1] != "tools/tests":
                self.add_dir(r[1])          # hostshim などの差し替えディレクトリ
        # Inline C snippets used by compiler probes have the same include search
        # roots as the script's compiler invocation.
        for inc in C_INC.findall(text):
            for base in sorted(self.inc_dirs | {"", d}):
                rel = norm(os.path.join(base, inc))
                if rel in self.tracked:
                    self.add(rel)
        for a, b in PY_IMPORT.findall(text):
            for name in ([a] if a else [x.strip() for x in b.split(",")]):
                name = name.split(" as ")[0].strip()
                if not name:
                    continue
                mod = name.replace(".", "/")
                for base in (d, "tools", "tools/tests", ""):
                    r = norm(os.path.join(base, mod + ".py"))
                    if r in self.tracked:
                        self.add(r)
                        break



@lru_cache(maxsize=512)
def compiler_headers(root, sources, inc_dirs):
    """Compiler dependency scan, supplemented by static includes for inactive branches.

    -MG keeps generated/missing includes visible; only repository files count.
    The conservative static closure is retained when a host compiler cannot
    preprocess a target-only translation unit (asm/ABI/vendor configuration).
    """
    found = set()
    for source in sources:
        cmd = ["gcc", "-MM", "-MG", "-I."] + ["-I" + d for d in inc_dirs] + [source]
        proc = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if proc.returncode:
            continue
        deps = proc.stdout.replace("\\\n", " ").split(":", 1)[-1]
        for dep in shlex.split(deps):
            rel = norm(os.path.relpath(dep, root) if os.path.isabs(dep) else dep)
            if not os.path.isabs(rel) and not rel.startswith("../") \
                    and os.path.isfile(os.path.join(root, rel)):
                found.add(rel)
    return frozenset(found)

PATH_AUDIT_ALLOW = {
    ("check-type-contract", "tools/tests/test_type_contract.py"):
        "recipe inventory and H5 artifacts use private TemporaryDirectory roots only",
    ("check-type-contract", "tools/build_compare.py"):
        "H5 calls compare on private fixture roots; snapshot/build main is never invoked",
    ("check-ppc-selection-host", "tools/build_compare.py"):
        "host test imports scanners only; snapshot/build entry point is never called",
    ("check-target-profile-host", "tools/gen_memmap.py"):
        "unsupported target is rejected before reading layout artifacts",
    "tools/tests/test_build_selection.py": "make fixture has a private build directory",
    "tools/tests/test_ppc_selection.py": "private query/dry-run fixture; compiler is never executed",
    ("check-shell-service-host", "tools/tests/test_shell_service.py"):
        "build configuration is copied only for private query and host mutations",

    ("check-build-compare-host", "tools/build_compare.py"):
        "comparison unit tests use private fixture roots; main is never invoked",
    ("check-src-layout-host", "tools/build_compare.py"):
        "layout tests use private snapshot/object fixtures; main is never invoked",
    "tools/tests/test_build_compare.py": "all artifacts are created inside TemporaryDirectory",
    "tools/tests/test_build_profile.py": "make fixture lives inside TemporaryDirectory",
    "tools/tests/test_target_toolchain.py": "only copied config is queried; no products are read",

    "tools/tests/" + "test_check_times.py": "timing fixtures use only private build/out logs",
    "tools/tests/" + "test_preflight.py": "preflight fixtures create private build artifacts and tool stubs",
    "tools/tests/" + "host32.py": "cache contains only freshly compiled host fixtures",
    "tools/tests/" + "test_np21w_symbols.py":
        "identity fixtures link private ELF/maps under TemporaryDirectory; no repository artifacts",
    ("check-np21w-symbols-host", "tools/np21w_mcp/" + "symbols" + ".py"):
        "identity tests inject discovery roots and synthetic guest RAM with private ELF/maps",
    ("check-gui-gate-host", "tools/np21w_mcp/" + "symbols" + ".py"):
        "offline GUI fixtures inject symbol factories; production map discovery is unused",
    ("check-key-inject-host", "tools/np21w_mcp/" + "symbols" + ".py"):
        "key encoding tests never resolve kernel symbols or discover production maps",
    # Limit helper exceptions to these offline targets: other callers may read images.
    ("check-np21w-ctl-host", "tools/" + "nhd_profile" + ".py"):
        "FakeOps/FakeHttp replace image and process I/O; profile paths are only names",
    ("check-np21w-gui-host", "tools/" + "gui_gate" + ".py"):
        "mock HTTP contract tests never call main or use its screenshot output directory",
    ("check-np21w-gui-host", "tools/np21w_mcp/" + "server" + ".py"):
        "MCP tests inject GUI observations/symbols; default build paths are never read",
    ("check-gui-gate-host", "tools/" + "gui_gate" + ".py"):
        "fake guest replaces HTTP/time/screenshots; main's output directory is unused",
    ("check-key-inject-host", "tools/" + "gui_gate" + ".py"):
        "key expansion and mocked key sends only; main's output directory is unused",
    ("check-access-host", "tools/" + "h3_park_resume" + ".py"):
        "identity observer uses synthetic memory; main's ELF/map defaults are unused",
}
ARTIFACT_DIRS = ("build/out", "build/sdk", "build/nhd", "images")
PATH_AUDIT_ALLOW.update({
    "tools/checks/run.py": "runner audits path names, without reading build payloads",
    "tools/checks/test_run.py": "runner fixtures use private repository roots",
})


@lru_cache(maxsize=512)
def test_path_refs(text):
    """Return (line, path pattern, dynamic) for literal/assembled Python paths.

    Follow simple aliases, str/Path, joinpath/os.path.join, +, /, %, format and
    f-strings. Unknown components are kept as '*' so a loop-built generated
    filename is checked even though it does not exist as a literal in the test.
    Literal loop choices remain separate paths so individual registrations suffice.
    This deliberately over-approximates writes and fixture paths too.
    """
    tree = ast.parse(text)
    bindings = {}
    for node in ast.walk(tree):
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            for target in targets:
                if isinstance(target, ast.Name) and node.value is not None:
                    bindings.setdefault(target.id, []).append(node.value)

    parents = {child: node for node in ast.walk(tree) for child in ast.iter_child_nodes(node)}

    def loop_choice(node):
        """Only the enclosing loop body can supply finite choices for a name.

        Stop at lexical scopes, and let an inner loop (even an unknown one)
        shadow an outer loop. A loop's iterable and else suite are outside
        the finite binding we can prove here.
        """
        child = node
        for_scope = (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.ClassDef)
        comp_scope = (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)
        while child in parents:
            parent = parents[child]
            if isinstance(parent, for_scope):
                break
            if isinstance(parent, (ast.For, ast.AsyncFor)):
                names = {n.id for n in ast.walk(parent.target) if isinstance(n, ast.Name)}
                if node.id in names:
                    return parent.iter if child in parent.body and isinstance(parent.target, ast.Name) else None
            if isinstance(parent, comp_scope):
                # Iterables see earlier generators; result/filters see their
                # own binding too. Comprehension targets never escape.
                generators = parent.generators
                if isinstance(child, ast.comprehension):
                    index = generators.index(child)
                    if node not in ast.walk(child.iter):
                        index += 1
                    generators = generators[:index]
                for generator in reversed(generators):
                    names = {n.id for n in ast.walk(generator.target) if isinstance(n, ast.Name)}
                    if node.id in names:
                        return generator.iter if isinstance(generator.target, ast.Name) else None
                break
            child = parent
        return None

    def combine(vals, separator):
        paths, dynamic = {""}, False
        for index, (parts, dyn) in enumerate(vals):
            paths = {a + (separator if index else "") + b for a in paths for b in parts}
            dynamic |= dyn
        return paths, dynamic

    def value(node, seen=frozenset()):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return {node.value}, False
        if isinstance(node, ast.Name):
            if node.id == "ROOT":
                return {""}, False
            choice = loop_choice(node)
            if isinstance(choice, (ast.Tuple, ast.List, ast.Set)) and node.id not in bindings and node.id not in seen:
                vals = [value(n, seen | {node.id}) for n in choice.elts]
                return {p for parts, _ in vals for p in parts}, any(d for _, d in vals)
            choices = bindings.get(node.id, [])
            if node.id not in seen and len(choices) == 1:
                choice = choices[0]
                return value(choice, seen | {node.id})
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Div, ast.Mod)):
            left = value(node.left, seen)
            right = value(node.right, seen)
            if isinstance(node.op, ast.Mod):
                return {re.sub(r"%[-+0-9.]*[sdr]", "*", p) for p in left[0]}, True
            paths, dynamic = combine([left, right], "/" if isinstance(node.op, ast.Div) else "")
            return paths, dynamic or isinstance(node.op, ast.Add)
        if isinstance(node, ast.JoinedStr):
            paths, _ = combine([value(n.value if isinstance(n, ast.FormattedValue) else n, seen)
                                for n in node.values], "")
            return paths, True
        if isinstance(node, ast.Call):
            name = node.func.id if isinstance(node.func, ast.Name) else getattr(node.func, "attr", "")
            if name == "str" and node.args:
                return value(node.args[0], seen)
            if name in ("Path", "join", "joinpath") and node.args:
                args = list(node.args)
                if name == "joinpath":
                    args.insert(0, node.func.value)
                return combine([value(n, seen) for n in args], "/")
            if name == "format" and isinstance(node.func, ast.Attribute):
                templates, _ = value(node.func.value, seen)
                return {re.sub(r"\{[^}]*\}", "*", p) for p in templates}, True
        return {"*"}, True

    refs = set()
    for node in ast.walk(tree):
        if not isinstance(node, (ast.Constant, ast.BinOp, ast.JoinedStr, ast.Call)):
            continue
        paths, dynamic = value(node)
        for path in paths:
            path = path.removeprefix("/").removeprefix("./")
            if "/" not in path and path != "build":
                continue
            if not path or any(c.isspace() for c in path) or path.startswith("*"):
                continue
            refs.add((node.lineno, path, dynamic))
    return sorted(refs)

def script_path_refs(text):
    """Conservatively audit literal paths in non-Python test scripts too."""
    refs = set()
    for line, text_line in enumerate(text.splitlines(), 1):
        if text_line.lstrip().startswith("#"):
            continue
        for path in re.findall(r"[\w./*?${}+-]+", text_line):
            path = path.removeprefix("./")
            if "/" in path and not path.startswith(("$", "*", "/")):
                dynamic = any(c in path for c in "$*?{")
                path = re.sub(r"\$\{[^}]*\}|\$[A-Za-z_][A-Za-z_0-9]*", "*", path)
                refs.add((line, path, dynamic))
    return sorted(refs)

@dataclass(frozen=True)
class Check:
    name: str
    path: str
    covers: tuple[str, ...]
    budget: float
    tier: int = 1
    needs: tuple[str, ...] = ()
    mutate_tier: int = 2
    weight: float | None = None

    @property
    def seconds(self):
        return self.budget if self.weight is None else self.weight


@dataclass(frozen=True)
class Plan:
    tier: int
    mode: str
    hit: frozenset[str]
    unmatched: frozenset[str]
    full_hits: frozenset[str]
    stage: tuple[str, ...]
    mutations: tuple[str, ...]
    needs: tuple[str, ...]
    files: tuple[str, ...]


@dataclass(frozen=True)
class Context:
    root: Path
    mutate: bool
    runners: tuple[str, ...]
    files: tuple[str, ...] | None
    tmp: Path
    make_vars: Mapping[str, str]


def matches(path, patterns):
    return any(glob_re(p).match(path) for p in patterns)


def ordered(checks, names=None):
    return tuple(sorted(checks if names is None else names,
                        key=lambda n: (-checks[n].seconds, n)))


def shards(checks, count):
    """LPT over undirected check-dependency components; never split a need."""
    if count < 1:
        raise ValueError("shard count must be positive")
    closure(checks, checks)  # Reject unknown prerequisites even for empty shards.
    neighbors = {n: set() for n in checks}
    for name, check in checks.items():
        for need in check.needs:
            if need.startswith("check:"):
                dependency = need[6:]
                neighbors[name].add(dependency)
                neighbors[dependency].add(name)
    groups, remaining = [], set(checks)
    while remaining:
        pending, group = [min(remaining)], set()
        while pending:
            name = pending.pop()
            if name in group:
                continue
            group.add(name)
            pending.extend(neighbors[name] - group)
        remaining.difference_update(group)
        groups.append(tuple(sorted(group)))
    result, loads = [set() for _ in range(count)], [0.0] * count
    for group in sorted(groups, key=lambda g: (-sum(checks[n].seconds for n in g), g)):
        index = min(range(count), key=lambda i: (loads[i], i))
        result[index].update(group)
        loads[index] += sum(checks[n].seconds for n in group)
    return tuple(ordered(checks, names) for names in result)


def declaration_paths(root):
    paths = set((root / "tools/checks").rglob("*.py"))
    paths.update((root / "tools/tests").glob("test_*.py"))
    return sorted(paths)


def indexed_discover(root):
    """Reuse declarations only while every candidate and the parser are unchanged.

    Include undeclared candidates, additions, removals, and ctime (not just mtime).
    Publication is atomic; concurrent readers never see an incomplete index.
    An unwritable or damaged cache only costs a fresh discovery.
    """
    candidates = declaration_paths(root)
    paths = sorted(set(candidates) | {Path(__file__).resolve()} |
                   ({root / "tools/checks/weights.json"} if (root / "tools/checks/weights.json").exists() else set()))
    signature = [[str(root.resolve()), sys.version]]
    for path in paths:
        stamp = path.stat()
        signature.append([str(path), stamp.st_mtime_ns, stamp.st_ctime_ns,
                          stamp.st_size, stamp.st_ino])
    cache = root / "build/out/check-index.json"
    try:
        data = json.loads(cache.read_text())
        if data["signature"] == signature:
            return {row["name"]: Check(**dict(row, covers=tuple(row["covers"]),
                                             needs=tuple(row["needs"]))) for row in data["checks"]}
    except (OSError, ValueError, TypeError, KeyError):
        pass
    checks = discover(root)
    # Do not publish a discovery made across an edit. The next call retries.
    after = []
    for path in paths:
        stamp = path.stat()
        after.append([str(path), stamp.st_mtime_ns, stamp.st_ctime_ns,
                      stamp.st_size, stamp.st_ino])
    if signature[1:] == after and declaration_paths(root) == candidates:
        pending = None
        try:
            cache.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(mode="w", dir=cache.parent,
                                             prefix="check-index-", delete=False) as stream:
                pending = Path(stream.name)
                json.dump(dict(signature=signature, checks=[asdict(c) for c in checks.values()]), stream)
            pending.replace(cache)
        except OSError:
            pass
        finally:
            if pending is not None:
                pending.unlink(missing_ok=True)
    return checks


def emit_make(checks, names):
    selected = closure(checks, names)
    values = dict(
        CHECK_NAMES=" ".join("check-" + n for n in ordered(checks)),
        CHECK_SHARD_TARGETS=" ".join("check-" + n for n in names),
        CHECK_EDGES=" ".join("check-time-check-" + c.name + ":check-time-check-" + n[6:]
                             for name in ordered(checks) for c in (checks[name],)
                             for n in c.needs if n.startswith("check:")),
        CHECK_ALL_NEEDS=" ".join(make_needs(checks, selected)))
    # Names and needs have already been restricted to make-safe characters.
    # Nested eval preserves assignment boundaries after $(shell) folds newlines.
    return " ".join("$(eval " + k + " := " + v + ")" for k, v in values.items())


def discover(root: Path) -> dict[str, Check]:
    checks = {}
    paths = declaration_paths(root)
    for path in sorted(paths):
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        declarations = [n.value for n in tree.body
                        if isinstance(n, (ast.Assign, ast.AnnAssign))
                        and any(isinstance(t, ast.Name) and t.id == "CHECK" for t in
                                (n.targets if isinstance(n, ast.Assign) else [n.target]))]
        if not declarations:
            continue
        rel = path.relative_to(root).as_posix()
        def reject(reason):
            raise ValueError(rel + ": " + reason)
        if len(declarations) != 1:
            reject("expected one CHECK")
        node = declarations[0]
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "dict" and not node.args
                and all(k.arg is not None for k in node.keywords)):
            reject("CHECK must be a literal dict(...)")
        try:
            data = {k.arg: ast.literal_eval(k.value) for k in node.keywords}
        except (ValueError, TypeError):
            reject("CHECK values must be literal")
        if len(data) != len(node.keywords):
            reject("duplicate CHECK field")
        allowed = {"name", "covers", "budget", "tier", "needs", "mutate_tier"}
        if set(data) - allowed or not {"name", "covers", "budget"} <= data.keys():
            reject("invalid CHECK fields")
        name = data["name"]
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", name):
            reject("invalid name")
        for field, default in (("covers", None), ("needs", [])):
            value = data.get(field, default)
            if not isinstance(value, list) or not all(isinstance(s, str) and s for s in value):
                reject(field + " must be list[str]")
        for field, default, minimum in (("tier", 1, 0), ("mutate_tier", 2, 1)):
            value = data.get(field, default)
            if type(value) is not int or not minimum <= value <= 4:
                reject("invalid " + field)
        if type(data["budget"]) not in (int, float) or not math.isfinite(data["budget"]) or data["budget"] < 0:
            reject("budget must be finite and nonnegative")
        for need in data.get("needs", []):
            if not re.fullmatch(r"(?:make|check):[\w./+-]+", need):
                reject("needs must use make:/check:")
        for pattern in data["covers"]:
            try:
                glob_re(pattern)
            except (ValueError, re.error):
                reject("invalid covers glob: " + pattern)
        if not any(isinstance(n, ast.FunctionDef) and n.name == "run" for n in tree.body):
            reject("missing run(ctx)")
        if name in OUTSIDE:
            continue
        if name in checks:
            reject("duplicate name: " + name)
        checks[name] = Check(name, rel, tuple(data["covers"]), float(data["budget"]),
                             data.get("tier", 1), tuple(data.get("needs", [])), data.get("mutate_tier", 2))
    weights = root / "tools/checks/weights.json"
    if weights.exists():
        for name, seconds in json.loads(weights.read_text())["seconds"].items():
            if type(seconds) not in (int, float) or not math.isfinite(seconds) or seconds < 0:
                raise ValueError("invalid check weight: " + name)
            if name in checks:
                checks[name] = replace(checks[name], weight=float(seconds), budget=2 * seconds)
    return checks


def extract(root, check, snapshot=None, *, compiler=False):
    ex = Extractor(root, snapshot)
    ex.add(check.path)
    for source in check.covers:
        if source.endswith((".c", ".py")) and not any(c in source for c in "*?["):
            ex.add(source)
    ex.drain()
    if compiler:
        ex.found.update(compiler_headers(str(root), tuple(sorted(f for f in ex.found if f.endswith(".c"))),
                                        tuple(sorted(ex.inc_dirs))))
    return frozenset(ex.found)


def inputs(root: Path, check: Check) -> frozenset[str]:
    compiler_headers.cache_clear()
    return extract(root, check, compiler=True)


def closure(checks, names):
    selected = set(names)
    pending = list(names)
    while pending:
        for need in checks[pending.pop()].needs:
            if not need.startswith("check:"):
                continue
            name = need[6:]
            if name not in checks:
                raise ValueError("unknown check prerequisite: " + name)
            if name not in selected:
                selected.add(name)
                pending.append(name)
    return ordered(checks, selected)


def make_needs(checks, stage):
    return tuple(sorted({need[5:] for name in stage for need in checks[name].needs
                         if need.startswith("make:")}))


def build_content(text, path):
    """Drop only unambiguous build comments; retain shell/define/continued text."""
    make = Path(path).name == "Makefile" or path.endswith(".mk")
    toml = path.endswith((".toml", ".lock")) or Path(path).name == "config"
    if make and ".RECIPEPREFIX" in text or toml and ("'''" in text or '\"\"\"' in text):
        return None
    content, continued, defines = [], False, 0
    for line in text.splitlines():
        stripped = line.lstrip()
        define = make and re.match(r"(?:(?:override|export|private|unexport)\s+)*define\b", stripped)
        endef = make and re.match(r"endef\b", stripped)
        protected = make and (line.startswith("\t") or continued or defines or define
                              or ";" in line or "$(eval" in line)
        continued = line.endswith("\\")
        if define:
            defines += 1
        if endef:
            defines = max(0, defines - 1)
        if protected or continued:
            content.append(line)
            continue
        if make or toml:
            quote, escaped = None, False
            i = 0
            while i < len(line):
                char = line[i]
                # make keeps hashes inside variable/function references.
                if make and char == "$" and i + 1 < len(line):
                    opening = line[i + 1]
                    if opening == "$":
                        i += 2
                        escaped = False
                        continue
                    if opening in "({":
                        closing = ")" if opening == "(" else "}"
                        depth, end = 1, i + 2
                        while end < len(line) and depth:
                            depth += (line[end] == opening) - (line[end] == closing)
                            end += 1
                        if depth:
                            break  # malformed/ambiguous: keep the original line
                        i, escaped = end, False
                        continue
                    i += 2  # single-character variable reference, including $#
                    escaped = False
                    continue
                if char == "#" and not escaped and quote is None:
                    line = line[:i]
                    break
                if toml and char in "\"'" and not escaped:
                    quote = None if quote == char else char if quote is None else quote
                escaped = char == "\\" and not escaped and (not toml or quote != "'")
                i += 1
            if quote is not None:
                return None
        if line.strip():
            content.append(line)
    return tuple(content)


SOURCE_REF = r"\$\([A-Z_][A-Z_0-9]*\)"
SOURCE_WORD = rf"(?:{SOURCE_REF}|[A-Za-z0-9_./+-])+"
SOURCE_TOKEN = rf"(?:\$\(wildcard [A-Za-z0-9_./*? \t-]+\)|{SOURCE_WORD})"
SOURCE_PATH = re.compile(rf"(?:{SOURCE_REF}/)?[A-Za-z0-9_][A-Za-z0-9_./+-]*\.(?:c|o)\Z")


def source_list_content(content):
    """Keep assignment names/operators and every non-source operand unchanged."""
    signature, lists, defines = [], [], 0
    lines = iter(content)
    for line in lines:
        parts = [line]
        while parts[-1].endswith("\\"):
            following = next(lines, None)
            if following is None:
                return None, set()
            parts.append(following)
        statement = "\n".join(parts)
        define = re.match(r"(?:(?:override|export|private|unexport)\s+)*define\b", line.lstrip())
        protected = line.startswith("\t") or defines or define
        if define:
            defines += 1
        if re.match(r"endef\b", line.lstrip()):
            defines = max(0, defines - 1)
        logical = " ".join(p[:-1] if p.endswith("\\") else p for p in parts)
        assignment = None if protected else re.fullmatch(
            r"[ ]*(C_KERNEL|[A-Z][A-Z_0-9]*_(?:SRC|SOURCES|OBJ|OBJS))"
            r"[ \t]*(:=|\+=|\?=|=)[ \t]*(.*)", logical)
        if assignment is None:
            signature.append(statement)
            continue
        name, operator, value = assignment.groups()
        # Only addprefix's file-list argument is a list here. Other functions
        # (including wildcard) remain opaque, identical operands on both sides.
        wrapper = re.fullmatch(rf"(\$\(addprefix[ \t]+{SOURCE_WORD}/,)[ \t]*(.*?)\)", value)
        prefix, value = (wrapper[1], wrapper[2]) if wrapper else ("", value)
        value = value.strip()
        if value and not re.fullmatch(rf"{SOURCE_TOKEN}(?:[ \t]+{SOURCE_TOKEN})*", value):
            signature.append(statement)
            continue
        tokens = re.findall(SOURCE_TOKEN, value)
        paths = {t for t in tokens if SOURCE_PATH.fullmatch(t)}
        references = tuple(sorted({ref for t in paths for ref in re.findall(SOURCE_REF, t)}))
        signature.append((name, operator, prefix, references, tuple(t for t in tokens if t not in paths)))
        lists.append((prefix, tuple(tokens)))
    return tuple(signature), lists


def routed_build_inputs(root, base, path):
    """None means full; otherwise return source inputs changed by registration."""
    if base is None:
        return None
    try:
        ancestor = git(root, "merge-base", base, "HEAD")[0]
        before = "\n".join(git(root, "show", ancestor + ":" + path))
        after = (root / path).read_text()
    except (ValueError, OSError, IndexError):
        return None
    old = build_content(before, path)
    new = build_content(after, path)
    if old is None or new is None:
        return None
    if old == new:
        return frozenset()
    if matches(path, ("build/arch/*.mk", "build/sources/*.mk", "build/features/*.mk", "build/platform/*.mk", "build/select.mk")):
        return None  # Selection changes always require the complete configuration route.
    if matches(path, ("build/**/*.mk",)):
        old_shape, old_lists = source_list_content(old)
        new_shape, new_lists = source_list_content(new)
        if old_shape is not None and old_shape == new_shape:
            sources = set()
            for (prefix, old_tokens), (_, new_tokens) in zip(old_lists, new_lists):
                common = set(old_tokens) & set(new_tokens)
                if tuple(t for t in old_tokens if t in common) != tuple(t for t in new_tokens if t in common):
                    return None  # Existing operands must retain their link order.
                changed_paths = set(old_tokens) ^ set(new_tokens)
                if not changed_paths:
                    continue
                if prefix or any("$" in t for t in changed_paths):
                    return None  # Cannot resolve the implementation without make.
                sources.update(changed_paths)
            return frozenset(t[:-2] + ".c" if t.endswith(".o") else t for t in sources)
    return None


def routed_build_safe(root, base, path):
    return routed_build_inputs(root, base, path) is not None


def undeclared(files, patterns):
    return {f for f in files if not matches(f, IGNORE + DOCS + FULL + BUILD_CONFIG_GLOBS + KAPI_GENERATED)
            and not any(matches(f, (p,)) and not matches(f, ex) for p, ex in NOTEST)
            and not any(matches(f, pats) for pats in patterns)}


def plan(root: Path, checks: Mapping[str, Check], files: tuple[str, ...],
         *, base: str | None = None, tier_floor: int = 0) -> Plan:
    compiler_headers.cache_clear()
    changed = tuple(f for f in files if not matches(f, IGNORE))
    dependency_scan = any(not matches(f, DOCS) for f in changed)
    snapshot = tracked(root) if dependency_scan else None
    patterns = {n: c.covers + (c.path,) + (tuple(extract(root, c, snapshot, compiler=True))
                         if dependency_scan else ()) for n, c in checks.items()}
    def uncovered(f):
        return bool(undeclared((f,), patterns.values()))

    hit, unmatched, full_hits = set(), set(), set()
    # Conversion retains the old artifact_readers as make:all consumers,
    # including readers whose covers contain no build output paths.
    artifact_targets = {n for n, c in checks.items() if "make:all" in c.needs or any(
        p.startswith(("build/out/", "build/sdk/")) for p in c.covers)}
    generated = KAPI_GENERATED + ("sdk/gen_kapi.py", "sdk/kapi_abi_x86.py", "sdk/target_profile.py", "sdk/kapi_rust_gen.py")
    for f in changed:
        hit.update(n for n, pats in patterns.items() if matches(f, pats))
        if matches(f, ("build/**/*.mk",)):
            sources = routed_build_inputs(root, base, f)
            if sources is None:
                full_hits.add(f)
            else:
                hit.update(artifact_targets)
                hit.update(n for n, c in checks.items() if any("**" in p for p in c.covers))
                hit.update(n for n, pats in patterns.items() if any(matches(s, pats) for s in sources))
                unmatched.update(s for s in sources if uncovered(s))
            continue
        if f == "sdk/kapi.json":
            hit.update(artifact_targets)
            hit.update(n for n, pats in patterns.items()
                       if "kapi" in n or any(matches(dep, pats) for dep in generated))
            continue
        if matches(f, FULL + BUILD_CONFIG_GLOBS):
            full_hits.add(f)
        elif uncovered(f):
            unmatched.add(f)
    docs_only = bool(changed) and all(matches(f, DOCS) for f in changed)
    notest_only = bool(changed) and all(any(matches(f, (p,)) and not matches(f, ex)
                                          for p, ex in NOTEST) for f in changed)
    build_changes = {f for f in changed if matches(f, FULL + BUILD_CONFIG_GLOBS)
                     and not routed_build_safe(root, base, f)}
    forced = bool(build_changes) or any(f in KAPI_GENERATED or f in
        ("sdk/kapi.json", "sdk/gen_kapi.py", "sdk/kapi_abi_x86.py", "sdk/target_profile.py", "sdk/kapi_rust_gen.py", "tools/checks/run.py") for f in changed)
    if not changed:
        tier, mode, selected = 4, "fast", set(checks)
    elif forced:
        tier, mode, selected = 4, "full", set(checks)
    elif notest_only and not hit:
        tier, mode, selected = 0, "sel", set()
    elif docs_only:
        tier, mode = 0, "docs"
        selected = {n for n in hit if checks[n].tier != 4}
    else:
        parents = [Path(f).parent.parts for f in changed]
        common = os.path.commonpath([str(Path(f).parent) for f in changed])
        shared = any(Path(f).parts[0] in SHARED_LAYERS for f in changed)
        if unmatched or shared or len(Path(common).parts) < 2:
            tier = 3
        elif len(changed) == 1 and not changed[0].endswith(".h") and len(parents[0]) >= 2:
            tier = 1
        else:
            tier = 2
        tier = max(tier, tier_floor)
        mode = "full" if tier == 4 else "sel"
        selected = set(checks) if tier == 4 else {n for n in hit if checks[n].tier != 4}
    stage = closure(checks, selected)
    if mode == "fast" or tier == 1:
        mutations = ()
    elif mode == "docs" or tier == 4:
        mutations = stage
    else:
        mutations = tuple(n for n in stage if n in hit and checks[n].mutate_tier <= tier)
    return Plan(tier, mode, frozenset(hit), frozenset(unmatched), frozenset(full_hits),
                stage, mutations, make_needs(checks, stage), tuple(files))


def default_base(root=ROOT):
    head = git(root, "rev-parse", "HEAD")[0]
    branch = git(root, "rev-parse", "--abbrev-ref", "HEAD")[0]
    if branch in {r.removeprefix("origin/") for r in BASE_REFS}:
        proc = subprocess.run(["git", "-C", str(root), "rev-parse", "--verify", "-q", "HEAD~1"],
                              capture_output=True, text=True)
        return proc.stdout.strip() if proc.returncode == 0 else head
    for ref in BASE_REFS:
        proc = subprocess.run(["git", "-C", str(root), "merge-base", ref, "HEAD"],
                              capture_output=True, text=True)
        if proc.returncode == 0 and proc.stdout.strip():
            return proc.stdout.strip()
    return head


def changed_files(root, base):
    return tuple(sorted(set(git(root, "diff", "--name-only", "--no-renames", base + "...HEAD")) |
                        set(git(root, "diff", "--name-only", "--no-renames", "HEAD")) |
                        set(git(root, "ls-files", "--others", "--exclude-standard"))))


def emit_shell(result: Plan) -> str:
    summary = "check-changed: tier %d, %s, %d checks, %d mutations" % (
        result.tier, result.mode, len(result.stage), len(result.mutations))
    if result.unmatched:
        summary += "; unmatched: " + " ".join(sorted(result.unmatched))
    if result.tier == 0 and result.mode == "sel" and not result.stage:
        summary += "; notest only, no matching check"
    values = dict(CC_MODE=result.mode, CC_STAGE1=" ".join("check-" + n for n in result.stage),
                  CC_MUT1=" ".join("check-" + n for n in result.mutations), CC_SUMMARY=summary,
                  CC_TIER=str(result.tier), CC_NEEDS=" ".join(result.needs),
                  CC_FILES_JSON=json.dumps(result.files, ensure_ascii=False))
    return "\n".join(k + "=" + shlex.quote(v) for k, v in values.items()) + "\n"


def lint(root, checks):
    errors = []
    snapshot = tracked(root)
    generated = set(KAPI_GENERATED) | {f for f in snapshot if "_generated." in Path(f).name
                or "generations." in Path(f).name or "/generated/" in f}
    coverage = []
    for name, check in checks.items():
        derived = extract(root, check, snapshot)
        coverage.append(check.covers + (check.path,) + tuple(derived))
        patterns = check.covers + (check.path,) + tuple(f for f in derived if f.endswith(".inc"))
        prepared = make_needs(checks, closure(checks, (name,)))
        for pattern in check.covers:
            if not any(matches(f, (pattern,)) for f in snapshot) and not any(
                    pattern.startswith(d + "/") for d in ARTIFACT_DIRS) and not any(
                    matches(output, (pattern,)) for output in KAPI_GENERATED):
                errors.append(name + ": missing input " + pattern)
        if any(p.startswith(d + "/") for p in check.covers for d in ARTIFACT_DIRS) \
                and not prepared:
            errors.append(name + ": artifact covers require make needs")
        for source in sorted(derived):
            if not (matches(source, patterns) or source.endswith(".h")
                    or source not in {"build/kernel.mk", "build/libs.mk", "build/programs.mk"}
                    and matches(source, FULL + BUILD_CONFIG_GLOBS) or matches(source, IGNORE)):
                errors.append(name + ": uncovered input " + source)
            if source in PATH_AUDIT_ALLOW or ("check-" + name, source) in PATH_AUDIT_ALLOW:
                continue
            if not (source.startswith("tools/") and source.endswith(".py") or source.startswith("tools/tests/")):
                continue
            data = (root / source).read_bytes()
            if not source.endswith((".py", ".sh", ".bash", ".pl", ".rb", ".awk")) and not data.startswith(b"#!"):
                continue
            refs = (test_path_refs if source.endswith(".py") else script_path_refs)(data.decode("utf-8"))
            missing = set()
            for line, path, dynamic in refs:
                artifact = any(path == d or path.startswith(d + "/") or
                               path == "build" and d.startswith("build/") for d in ARTIFACT_DIRS)
                if artifact and not prepared:
                    errors.append(f"{name}: {source}:{line}: artifact {path} requires make needs")
                if dynamic and any(path.startswith(d + "/") for d in ARTIFACT_DIRS) and not matches(path, patterns):
                    errors.append(f"{name}: uncovered dynamic artifact {path}")
                if not dynamic:
                    continue
                for output in sorted(generated):
                    if not fnmatch.fnmatchcase(output, path):
                        continue
                    required = [output]
                    if output in KAPI_GENERATED:
                        required.extend(("sdk/kapi_rust_gen.py",) if output.endswith("/kapi_generated.rs") else ("sdk/gen_kapi.py", "sdk/kapi_abi_x86.py", "sdk/target_profile.py"))
                    for dep in required:
                        if not matches(dep, patterns) and dep not in missing:
                            errors.append(f"{name}: uncovered generated input {dep}")
                            missing.add(dep)
    for path in sorted(undeclared(snapshot, coverage)):
        errors.append("対象の宣言が無い: " + path)
    closure(checks, checks)
    for error in errors:
        print(error, file=sys.stderr)
    return int(bool(errors))


def run_one(check: Check, ctx: Context) -> int:
    spec = importlib.util.spec_from_file_location("_os32_check_" + check.name.replace("-", "_"), ctx.root / check.path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    # Legacy scripts import helpers beside themselves; list/select/lint never import.
    saved_path, saved_cwd = sys.path[:], Path.cwd()
    sys.path[:0] = [str((ctx.root / check.path).parent), str(ctx.root / "tools/tests"), str(ctx.root / "tools")]
    started = time.monotonic()
    try:
        os.chdir(ctx.root)
        spec.loader.exec_module(module)
        rc = module.run(ctx)
        if type(rc) is not int:
            raise ValueError(check.name + ": run(ctx) must return int")
        return rc
    finally:
        os.chdir(saved_cwd)
        sys.path[:] = saved_path
        sys.modules.pop(spec.name, None)
        elapsed = time.monotonic() - started
        if not ctx.mutate and elapsed > check.budget:
            print(f"check-{check.name}: budget {check.budget:g}s exceeded ({elapsed:.2f}s)", file=sys.stderr)


def dialect_variants_needed(files=None):
    if files is None:
        files = changed_files(ROOT, default_base(ROOT))
    return any(matches(f, ("Makefile", "build/**/*.mk", "tools/check_c_dialect.py",
               "tools/clang_ast/**", "tools/tests/test_c_dialect.py", "tools/checks/run.py",
               "sdk/example/hello/Makefile", "tools/tests/mutpar.py")) for f in files)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--list", action="store_true")
    action.add_argument("--make", action="store_true", help="emit all make registration variables")
    action.add_argument("--select", action="store_true")
    action.add_argument("--run", nargs="+")
    action.add_argument("--lint", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--needs", action="store_true", help="with --list: selected make prerequisites")
    parser.add_argument("--shard", help="zero-based index/count, with --list or --make")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--base")
    source.add_argument("--files", nargs="*")
    parser.add_argument("--tier", type=int, choices=range(5), default=0)
    args = parser.parse_args(argv)
    try:
        checks = indexed_discover(ROOT) if args.list or args.make or args.run else discover(ROOT)
        names = ordered(checks)
        if args.shard is not None:
            if not (args.list or args.make) or not re.fullmatch(r"[0-9]+/[1-9][0-9]*", args.shard):
                raise ValueError("--shard requires --list or --make and index/count")
            index, count = map(int, args.shard.split("/"))
            if index >= count:
                raise ValueError("shard index must be less than count")
            names = shards(checks, count)[index]
        if args.needs and (not args.list or args.json):
            raise ValueError("--needs requires --list without --json")
        if args.make:
            print(emit_make(checks, names))
            return 0
        if args.list:
            if args.needs:
                print(" ".join(make_needs(checks, closure(checks, names))))
            elif args.json:
                print(json.dumps([dict(asdict(checks[n]), target="check-" + n) for n in names]))
            else:
                print("\n".join("check-" + n for n in names), end="\n" if names else "")
            return 0
        if args.lint:
            return lint(ROOT, checks)
        if args.select:
            base = None if args.files is not None else args.base or default_base(ROOT)
            files = tuple(args.files) if args.files is not None else changed_files(ROOT, base)
            result = plan(ROOT, checks, files, base=base, tier_floor=args.tier)
            if result.unmatched:
                raise ValueError("対象の宣言が無い: " + " ".join(sorted(result.unmatched)))
            print(f"check-changed: {result.mode}, tier {result.tier}, {len(result.stage)} checks", file=sys.stderr)
            print(emit_shell(result), end="")
            return 0
        names = {n.removeprefix("check-") for n in args.run}
        if names - checks.keys():
            raise ValueError("unknown check: " + " ".join(sorted(names - checks.keys())))
        child = os.environ.get("CHECK_TIME_CHILD") == "1"
        stage = ordered(checks, names) if child else closure(checks, names)
        needs = () if child or os.environ.get("CHECK_NO_NEEDS") == "1" else make_needs(checks, stage)
        # all を先に単独で組む (build/sdk.mk の check-prepare と同じ順)。
        for group in ((("all",) if "all" in needs else ()), tuple(n for n in needs if n != "all")):
            if group:
                rc = subprocess.run(["make", *group], cwd=ROOT, close_fds=False).returncode
                if rc:
                    return rc
        files = None
        if args.files is not None:
            files = tuple(args.files)
        elif "CC_FILES_JSON" in os.environ:
            parsed = json.loads(os.environ["CC_FILES_JSON"])
            if not isinstance(parsed, list) or not all(isinstance(f, str) for f in parsed):
                raise ValueError("CC_FILES_JSON must be a list of paths")
            files = tuple(parsed)
        if os.environ.get("CC_TIER") == "4":
            files = None
        runners = tuple(os.environ.get("HOST32_RUNNERS", "native qemu").split())
        make_vars = dict(os.environ)
        targets = os.environ.get("MUTATE_TARGETS", "").split()
        with tempfile.TemporaryDirectory(prefix="check-run-") as tmp:
            for name in stage:
                mutate = os.environ.get("MUTATE", "1") == "1" or name in targets or "check-" + name in targets
                variables = dict(make_vars, MUT="--mutate" if mutate else "", MUTS="--mutants" if mutate else "")
                ctx = Context(ROOT, mutate, runners, files, Path(tmp), variables)
                rc = run_one(checks[name], ctx)
                if rc:
                    return rc
        return 0
    except (ValueError, TypeError, OSError, SyntaxError) as error:
        print("checks: " + str(error), file=sys.stderr)
        return 1 if args.lint else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
