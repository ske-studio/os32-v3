#!/usr/bin/env python3
"""[C5] Physical pointer conversion audit (TASK_T1_LEDGER §3-4).

Text scanner, not a C type checker: multiline casts are supported, but aliases
and indirect calls still require review. Exceptions use file:function:reason.
"""
import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROOTS = ('kernel', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib')
EXCLUDED = {'third_party', 'sqlite', 'sqlite3', 'fatfs', 'zlib', 'microtar', 'os32_lz4'}
CAST = re.compile(r'\(\s*(?:(?:const|volatile)\s+)*(?:struct\s+)?\w+\s*\*\s*\)\s*')
PHYS = re.compile(r'\b(?:MEM_\w*(?:BASE|ADDR)|TVRAM_\w+|VRAM_PLANE_\w+|PEGC_\w*_BASE|BIOS_WORK_\w+|KHEAP_BASE|KAPI_ADDR|V86_(?:TEST_\w+_ADDR|IPL_ADDR|REMAP_START)|DB_SHM_PTR|\w*_phys|phys|pa|paddr|pfn)\b')
USER = re.compile(r'\b(?:RING3_\w+|MEM_EXEC_LOAD_ADDR|MEM_APP_BAND_\w+|MEM_LEASE_\w+|uva|user_\w+)\b')
FUNCTION = re.compile(r'^\w[^;{}\n]*?\b(\w+)\s*\([^;{}]*?\)\s*\{', re.M)
SINK = re.compile(r'\b(?:paging_\w+|dma_chan_setup|dma_setup|arch_mmu_load_root)\s*\(')


def code_only(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  source, flags=re.S)


def expression(code, start):
    """One cast operand, including field/index suffixes or parentheses."""
    i = start
    if i < len(code) and code[i] == '(':
        depth = 1
        i += 1
        while i < len(code) and depth:
            depth += (code[i] == '(') - (code[i] == ')')
            i += 1
    else:
        m = re.match(r'(?:&\s*)?(?:0[xX][0-9a-fA-F]+[uUlL]*|\w+)(?:(?:->|\.)\w+|\[[^\]]*\])*', code[i:])
        i += len(m[0]) if m else 0
    return code[start:i], i


def functions(code):
    result = []
    for m in FUNCTION.finditer(code):
        depth, end = 1, m.end()
        while end < len(code) and depth:
            depth += (code[end] == '{') - (code[end] == '}')
            end += 1
        result.append((m.start(), end, m[1]))
    return result


def context(spans, pos):
    return next((name for start, end, name in spans if start <= pos < end), '<file>')


def scan(source):
    code = code_only(source)
    spans = functions(code)
    found = []
    pointer_names = set(re.findall(r"\b\w+\s*\*+\s*(\w+)", code))
    def add(pos, rule):
        found.append((code.count('\n', 0, pos) + 1, context(spans, pos), rule))
    for m in CAST.finditer(code):
        expr, _ = expression(code, m.end())
        if re.match(r'(?:P2V(?:_IO|_BOOT)?(?:_CONST)?|V2P)\s*\(', expr):
            continue
        if PHYS.search(expr) or re.match(r'(?:addr[01]?|ebp|aligned|base|page|load_base|stack_top|new_esp|u_esp|user_esp|ring3_tramp_page|guest|ivt18|dst|src)\b', expr) or re.search(r'\b0[xX](?!0\b)[0-9a-fA-F]+', expr):
            add(m.start(), 'physical-cast')
    for m in re.finditer(r'\bV2P\s*\(', code):
        expr, _ = expression(code, m.end() - 1)
        if USER.search(expr):
            add(m.start(), 'user-v2p')
    for m in re.finditer(r'\bP2V(?:_IO)?_CONST\s*\(', code):
        start = code.rfind('\n', 0, m.start()) + 1
        if context(spans, m.start()) != '<file>' and not code[start:].lstrip().startswith('#define'):
            add(m.start(), 'function-const')
    for m in SINK.finditer(code):
        args, _ = expression(code, m.end() - 1)
        # Integer casts directly passed into physical interfaces. Nested V2P
        # already states intent; pointer arithmetic elsewhere is not a sink.
        direct = re.findall(r'\(\s*u32\s*\)\s*(?:&\s*)?(\w+)', args)
        if any(name in pointer_names for name in direct) or re.search(
                r'\(\s*u32\s*\)\s*(?:&\s*\w+|\w*(?:buffer|buf|table|directory|ptr)\w*\b)', args):
            add(m.start(), 'physical-sink')
    for m in re.finditer(r'\(\s*u32\s*\)\s*(?:&\s*)?g_(?:invoke|stack|iostatus|databuf|sop|fsctx|fobj|namebuf|secctx)\b', code):
        add(m.start(), 'physical-sink')
    return found


def audit(root):
    allowed = {}
    errors = []
    path = root / 'tools/check_p2v_allow.txt'
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        parts = line.split(':', 2)
        if len(parts) != 3 or not parts[2].strip():
            errors.append('invalid exception: ' + line)
            continue
        rel, func, _ = parts
        if (rel, func) in allowed:
            errors.append('duplicate exception: ' + line)
        allowed[rel, func] = parts[2]
    sources = {}
    for directory in ROOTS:
        for p in sorted((root / directory).rglob('*')):
            if p.suffix not in ('.c', '.h') or EXCLUDED.intersection(p.parts):
                continue
            rel = p.relative_to(root).as_posix()
            sources[rel] = p.read_text()
            for ln, func, rule in scan(sources[rel]):
                if rule in ('physical-cast', 'physical-sink') and (rel, func) in allowed:
                    continue
                errors.append(f'{rel}:{ln}:{func}: {rule}')
    for rel, func in allowed:
        if rel not in sources or (func != '<file>' and
                func not in {n for _, _, n in functions(code_only(sources[rel]))}):
            errors.append(f'stale exception: {rel}:{func}')
    return errors, len(allowed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    errors, count = audit(args.root)
    for error in errors:
        print(error)
    print(f'P2V: {len(errors)} violations; {count} exceptions')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
