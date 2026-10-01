#!/usr/bin/env python3
"""[C5] Physical conversions, using canonical clang types and macro expansions."""
import bisect
import collections
import argparse
import pathlib
import re
import clang_ast as ast

ROOT = ast.ROOT
ROOTS = ('kernel', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib')
EXCLUDED = {'third_party', 'sqlite', 'sqlite3', 'fatfs', 'zlib', 'microtar', 'os32_lz4'}
PHYS = re.compile(r'^(?:MEM_\w*(?:BASE|ADDR)|TVRAM_\w+|VRAM_PLANE_\w+|PEGC_\w*_BASE|BIOS_WORK_\w+|KHEAP_BASE|KAPI_ADDR|V86_(?:TEST_\w+_ADDR|IPL_ADDR|REMAP_START)|DB_SHM_PTR|\w*_phys|phys|pa|paddr|pfn)$')
USER = re.compile(r'^(?:RING3_\w+|MEM_EXEC_LOAD_ADDR|MEM_APP_BAND_\w+|MEM_LEASE_\w+|uva|user_\w+)$')
OTHER_PHYS = {'addr','addr0','addr1','ebp','aligned','base','page','load_base','stack_top',
              'new_esp','u_esp','user_esp','ring3_tramp_page','guest','ivt18','dst','src'}
HOST = re.compile(r'^g_(?:invoke|stack|iostatus|databuf|sop|fsctx|fobj|namebuf|secctx)$')
CONVERSIONS = {'P2V','P2V_IO','P2V_BOOT','P2V_CONST','P2V_IO_CONST','V2P'}
# Zero-based physical input positions from the existing API declarations.
PHYSICAL_ARGS = {
    'paging_verify_identity': {0}, 'paging_set_page': {1},
    'paging_map_range': {2}, 'paging_map_phys': {1},
    'paging_load_cr3': {0}, 'paging_addrspace_map_user': {2},
    'paging_addrspace_map_user_range_phys': {3},
    'dma_chan_setup': {1}, 'dma_setup': {0}, 'arch_mmu_load_root': {0},
}
K = ast.K


def expression_nodes(c):
    """A call's result has its declared type; its inputs are not its value."""
    yield c
    if c.kind == K.CALL_EXPR:
        return
    for child in c.get_children():
        yield from expression_nodes(child)


def findings(tu, root=ROOT):
    nodes = list(ast.walk(tu.cursor))
    macros = [c for c, _ in nodes if c.kind == K.MACRO_INSTANTIATION]
    macro_files = collections.defaultdict(list)
    for m in macros:
        macro_files[str(m.location.file)].append((m.location.offset,m))
    for entries in macro_files.values():
        entries.sort(key=lambda x:x[0])
    def inside(c):
        ranges = [(str(c.location.file), c.extent.start.offset, c.extent.end.offset)]
        # Include expansions in the physical spelling file (e.g. a header).
        file, start = ast.spelling_position(c.extent.start)
        end_file, end = ast.spelling_position(c.extent.end)
        if file == end_file:
            ranges.append((file, start, end))
        result = []
        for file, start, end in ranges:
            entries = macro_files[file]
            left = bisect.bisect_left(entries, (start,))
            right = bisect.bisect_left(entries, (end,))
            result.extend(m for _, m in entries[left:right])
        return result
    definitions = {c.spelling: {t.spelling for t in c.get_tokens()}
                   for c,_ in nodes if c.kind == K.MACRO_DEFINITION}
    def macro_names(c):
        # Nested expansions in a macro replacement list are not necessarily
        # exposed as MACRO_INSTANTIATION cursors; follow their definitions too.
        names = {m.spelling for m in inside(c)}
        pending = list(names)
        while pending:
            for name in definitions.get(pending.pop(), ()):
                if name not in names:
                    names.add(name)
                    pending.append(name)
        return names

    def macro_converted(name, seen=None):
        if name in CONVERSIONS:
            return True
        seen = set() if seen is None else seen
        if name in seen:
            return False
        seen.add(name)
        return any(macro_converted(n,seen) for n in definitions.get(name,()) if n != name)
    conversion_definitions = [c for c,_ in nodes if c.kind == K.MACRO_DEFINITION
                              and c.spelling in CONVERSIONS - {'V2P'}]
    def converted(c):
        # Only the outer expression can justify integer -> pointer conversion.
        while c.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR):
            children = list(c.get_children())
            if len(children) != 1:
                break
            c = children[0]
        pointer_conversions = CONVERSIONS - {'V2P'}
        if c.kind == K.CALL_EXPR:
            return c.spelling in pointer_conversions
        # Nested alias expansions collapse their source extent to the alias.
        # Their spelling position still identifies the actual outer P2V cast.
        locations = [c.extent.start]
        # LLVM 18 can return an empty token range for cross-header aliases.
        # A single token preserves the outer cast's physical definition.
        first = ast.first_token_location(c)
        if first is not None:
            locations.append(first)
        for location in locations:
            file, offset = ast.spelling_position(location)
            if any(str(d.location.file) == file and
                   d.extent.start.offset <= offset < d.extent.end.offset
                   for d in conversion_definitions):
                return True
        # An expansion must cover the whole expression, not just a summand.
        return any(m.spelling in pointer_conversions and
                   m.extent.start.offset == c.extent.start.offset and
                   m.extent.end.offset >= c.extent.end.offset for m in inside(c))
    def raw_pointer(c):
        for x in expression_nodes(c):
            if not ast.pointer(x.type):
                continue
            converted_here = any(macro_converted(m.spelling) and
                m.extent.start.offset <= x.location.offset < m.extent.end.offset
                for m in inside(c))
            if not converted_here:
                return True
        return False
    hits = set()
    functions = set()
    ranges = []
    for c, f in nodes:
        rel = ast.relpath(c, root)
        if c.kind == K.FUNCTION_DECL and c.is_definition() and rel:
            functions.add((rel,c.spelling))
            ranges.append((rel,c.extent.start.offset,c.extent.end.offset,c.spelling))
        if not rel:
            continue
        def add(rule):
            hits.add((rel,c.location.line,f,rule))
        if c.kind == K.CSTYLE_CAST_EXPR:
            children = list(c.get_children())
            if not children:
                continue
            operand = children[-1]
            if ast.pointer(c.type) and ast.integer(operand.type) and not converted(c):
                values = list(expression_nodes(operand))
                names = [x.spelling for x in values if x.kind in (K.DECL_REF_EXPR,K.MEMBER_REF_EXPR,K.CALL_EXPR)]
                names += macro_names(c)
                # Preserve the hexadecimal-address heuristic: decimal array
                # indices and integer sentinels are not physical addresses.
                literal = any(x.kind == K.INTEGER_LITERAL and
                    ast.integer_literal_value(x) != 0 and any(
                        token.lower().startswith('0x') for token in
                        [t.spelling for t in x.get_tokens()] + list(macro_names(c)))
                    for x in values)
                if literal or any(PHYS.fullmatch(n) or n in OTHER_PHYS for n in names):
                    add('physical-cast')
            if ast.integer(c.type) and ast.pointer(operand.type):
                # HostDrv's hypercall ABI uses CR3-relative linear addresses
                # (T1-U6, hostdrvnt.c:945); retain function-scoped exceptions.
                if any(HOST.fullmatch(x.spelling) for x in expression_nodes(operand)
                       if x.kind == K.DECL_REF_EXPR):
                    add('physical-sink')
        if c.kind == K.CALL_EXPR and c.spelling == 'V2P':
            names = [x.spelling for arg in c.get_arguments() for x in expression_nodes(arg)
                     if x.kind in (K.DECL_REF_EXPR,K.MEMBER_REF_EXPR,K.CALL_EXPR)]
            names += [m.spelling for m in inside(c)]
            if any(USER.fullmatch(name) for name in names):
                add('user-v2p')
        if c.kind == K.CALL_EXPR and (c.spelling.startswith('paging_') or
                c.spelling in ('dma_chan_setup','dma_setup','arch_mmu_load_root')):
            params = list(c.referenced.get_arguments()) if c.referenced else []
            physical = PHYSICAL_ARGS.get(c.spelling, {i for i,p in enumerate(params)
                if 'phys' in p.spelling or p.spelling in ('pa','paddr','pfn')})
            for i,arg in enumerate(c.get_arguments()):
                if i not in physical:
                    continue
                for cast in expression_nodes(arg):
                    if cast.kind != K.CSTYLE_CAST_EXPR or not ast.integer(cast.type):
                        continue
                    ch = list(cast.get_children())
                    # memmap's linker-address constants are integer layout values,
                    # not explicit pointer operands at this call site.
                    layout = any(macro.spelling in ('MEM_SHM_BASE','MEM_SQLITE_STACK_BASE')
                                 for macro in inside(arg))
                    refs = [x.spelling for x in expression_nodes(ch[-1])
                            if x.kind == K.DECL_REF_EXPR] if ch else []
                    linker_layout = layout and refs and set(refs) <= {'__bss_end','__sqlite_end'}
                    if ch and ast.pointer(ch[-1].type) and raw_pointer(ch[-1]) and not linker_layout:
                        add('physical-sink')
    for c in macros:
        rel = ast.relpath(c,root)
        if not rel:
            continue
        f = next((name for r,start,end,name in ranges if r == rel and
                  start <= c.location.offset < end), '<file>')
        if c.spelling in ('P2V_CONST','P2V_IO_CONST') and f != '<file>':
            hits.add((rel,c.location.line,f,'function-const'))
        if c.spelling == 'V2P' and any(USER.fullmatch(t.spelling) for t in c.get_tokens()):
            hits.add((rel,c.location.line,f,'user-v2p'))
    return sorted(hits), functions


def scan(source):
    """Tests pass complete, valid translation units; parse errors never count as hits."""
    tu = ast.parse('p2v_probe.c', ['-std=gnu11','-ffreestanding'], text=source)
    return [(ln,f,rule) for _,ln,f,rule in findings(tu)[0]]


def audit(root):
    root = pathlib.Path(root).absolute()
    allowed, errors = {}, []
    for line in (root / 'tools/check_p2v_allow.txt').read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        parts = line.split(':',2)
        if len(parts) != 3 or not parts[2].strip():
            errors.append('invalid exception: ' + line)
            continue
        rel,func,reason = parts
        if (rel,func) in allowed:
            errors.append('duplicate exception: ' + line)
        allowed[rel,func] = reason
    functions, files = set(), set()
    try:
        for u in ast.units(root):
            if u['src'].split('/')[0] not in ROOTS or EXCLUDED.intersection(pathlib.Path(u['src']).parts):
                continue
            tu = ast.parse(u['src'],u['argv'],root)
            hits, funcs = findings(tu,root)
            functions.update(funcs)
            files.update(ast.relpath(c,root) for c,_ in ast.walk(tu.cursor))
            for rel,ln,func,rule in hits:
                if rel.split('/')[0] not in ROOTS or EXCLUDED.intersection(pathlib.Path(rel).parts):
                    continue
                if rule in ('physical-cast','physical-sink') and (rel,func) in allowed:
                    continue
                errors.append(f'{rel}:{ln}:{func}: {rule}')
    except (ast.ParseError, RuntimeError) as e:
        errors.append('clang parse failure: ' + str(e))
    for rel,func in allowed:
        if rel not in files or (func != '<file>' and (rel,func) not in functions):
            errors.append(f'stale exception: {rel}:{func}')
    return sorted(set(errors)), len(allowed)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=pathlib.Path,default=ROOT)
    errors,count = audit(p.parse_args().root)
    for e in errors:
        print(e)
    print(f'P2V: {len(errors)} violations; {count} exceptions')
    return bool(errors)

if __name__ == '__main__':
    raise SystemExit(main())
