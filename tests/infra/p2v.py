"""Inspect casts and physical arguments in the selected kernel and boot C units."""
from pathlib import Path
from bisect import bisect_right
import json
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, make
from tests.support.product import flags, recipes
from tests.infra.c_dialect import options

# Parameter positions in paging.h and the machine MMU interface.
PHYSICAL = {"paging_set_page": 1, "paging_map_range": 2, "paging_map_phys": 1,
            "paging_v86_map_range": 2, "paging_load_cr3": 0, "arch_mmu_load_root": 0,
            "paging_addrspace_map_user": 2, "paging_addrspace_map_user_range_phys": 3,
            "dma_split_addr": 0, "dma_check_args": 1, "dma_chan_setup": 1}
# Identity accesses before the PC-98 enables paging (C5).
BOOT = {
    ("wr32_low", b"*(volatileu32*)addr=v;"),
    ("boot_main", b"file_base=(u8*)LOAD_BUF;"),
    ("boot_main", b"rc=vk32_boot(file_base,(u32)file_size,(u8*)VK32_LOAD_MIN,&img_crc);"),
}


PHYS_MACROS = {"LOAD_BUF", "VK32_LOAD_MIN", "TVRAM_CHAR_BASE", "TVRAM_ATTR_BASE",
               "TVRAM_CG_WINDOW", "GVRAM_PLANE_B", "GVRAM_PLANE_R", "GVRAM_PLANE_G",
               "GVRAM_PLANE_I", "MEM_PHYS_MMIO_TOP"}
USER = {"MEM_EXEC_LOAD_ADDR", "MEM_LEASE_BASE", "MEM_APP_BAND_BASE", "MEM_APP_STACK_TOP"}


def names(node):
    result = set()
    if node.get("referencedDecl"):
        result.add(node["referencedDecl"]["name"])
    if node.get("kind") == "MemberExpr":
        result.add(node["name"])
    for child in node.get("inner", []):
        result.update(names(child))
    return result


def observe(source, argv, config, errors, observed):
    # Let GCC select product headers and #if arms, leaving active macro
    # definitions for Clang to retain spelling and expansion coordinates.
    cpp = subprocess.run([*options(argv, source), "-E", "-fdirectives-only", source], capture_output=True)
    if cpp.returncode:
        raise OSError(cpp.stderr.decode())
    data = cpp.stdout
    target = "i386-unknown-linux-gnu" if config == "pc98" else "powerpc-none-elf"
    ast = subprocess.run(["clang", "-target", target, "-ffreestanding", "-Wno-everything",
                          "-x", "c", "-", "-fsyntax-only", "-Xclang", "-ast-dump=json"],
                         input=data, capture_output=True)
    if ast.returncode:
        raise OSError(f"{source}: {ast.stderr.decode()[:800]}")
    offsets, files = [], []
    for m in re.finditer(rb'^# ([0-9]+) "([^"]+)".*\n', data, re.M):
        offsets.append(m.end())
        files.append((Path(m[2].decode()), int(m[1])))

    def visit(node, function="", sink=False):
        kind = node.get("kind")
        if kind == "FunctionDecl":
            function = node.get("name")
        children = node.get("inner", [])
        if kind == "CallExpr" and children:
            callee = names(children[0])
            for name in callee & PHYSICAL.keys():
                visit(children[1 + PHYSICAL[name]], function, True)
        cast = node.get("castKind")
        if kind in ("CStyleCastExpr", "ImplicitCastExpr", "CallExpr") and (
                cast in ("IntegralToPointer", "PointerToIntegral") or
                kind == "CallExpr" and children and "V2P" in names(children[0])):
            begin, end = (node["range"][key] for key in ("begin", "end"))
            start = begin.get("expansionLoc", begin)["offset"]
            finish = end.get("expansionLoc", end)
            idx = bisect_right(offsets, start) - 1
            path, line = files[idx]
            line += data.count(b"\n", offsets[idx], start)
            spelling = data[start:finish["offset"] + finish["tokLen"]]
            words = set(re.findall(r'\b[A-Za-z_]\w*\b', spelling.decode(errors="replace")))
            refs = names(node)
            issue = None
            if kind == "CallExpr" and words & USER:
                issue = "V2P on USER or lease address"
            elif cast == "PointerToIntegral" and (sink or function == "pm_v2p_ram"):
                issue = "raw physical cast"
            elif cast == "IntegralToPointer" and (
                    words & PHYS_MACROS or refs & {"pd_phys", "pt_phys", "paging_kernel_pd_phys", "phys_addr", "P2V", "P2V_IO"}
                    or function in ("layout_read", "pm_p2v_ram") and "pa" in refs
                    or path == Path("boot/boot_main.c") and "addr" in refs):
                issue = "raw physical cast"
            if path == Path("boot/boot_main.c"):
                statement = data[data.rfind(b"\n", 0, start) + 1:data.find(b"\n", start)]
                site = (function, b"".join(statement.split()))
                if site in BOOT and site not in observed:
                    observed.add(site)
                    issue = None
            if path == Path("sys/include/memmap.h") and function in ("P2V", "P2V_IO", "V2P"):
                issue = None
            if words & {"P2V_CONST", "P2V_IO_CONST"} and "expansionLoc" in begin and begin == end:
                issue = None
            if path.parent == Path("sys/platform/powermac") and path.match("platform_*.h") and function in ("pm_p2v_ram", "pm_v2p_ram"):
                issue = None
            if issue:
                errors.add(f"{path}:{line} {function}: {issue}")
        if kind == "BinaryOperator" and node.get("opcode") == "=" and names(children[0]) & {"page_directory"}:
            visit(children[1], function, True)
        # Physical sink arguments include direct casts and ORed PTE flags.
        # Arithmetic deriving a region from a linker symbol is not a direct cast.
        propagate = sink and (kind in ("ParenExpr", "ImplicitCastExpr", "CStyleCastExpr") or
                              kind == "BinaryOperator" and node.get("opcode") == "|")
        for child in children:
            visit(child, function, propagate)

    visit(json.loads(ast.stdout))


def verify(config):
    objects, _ = recipes(config, ("kernel", "boot") if config == "pc98" else ("kernel",))
    errors, observed = set(), set()
    units = {(s, tuple(a)) for s, a in objects.values() if s.endswith(".c") and s != "lib/sqlite3/sqlite3.c"}
    if not units:
        raise OSError("empty product C selection")
    for source, argv in sorted(units):
        observe(source, argv, config, errors, observed)
    if config == "pc98" and observed != BOOT:
        errors.add(f"stale boot exceptions: {sorted(BOOT - observed)}")
    if errors:
        raise Failure("\n".join(sorted(errors)))


if __name__ == "__main__":
    main(lambda config: make(*flags(config), "kernel", *(["boot"] if config == "pc98" else [])), verify)
