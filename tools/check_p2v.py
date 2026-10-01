#!/usr/bin/env python3
"""[C5] Physical pointer conversion audit (TASK_T1_LEDGER §3-4).

Text scanner, not a C type checker: multiline casts are supported, but aliases
and indirect calls still require review. Exceptions use file:function:reason.
This is a guard against accidental introductions, not a defense against
intentionally crafted type spellings (user decision 2026-10-01, like T0's
check-c-dialect). Known limits: two or more qualifiers after a pointer star
(e.g. * const volatile), qualifiers after an integer typedef (e.g. uptr const),
and arbitrary typedef resolution.
"""
import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROOTS = ('kernel', 'drivers', 'gfx', 'fs', 'exec', 'kapi', 'lib')
EXCLUDED = {'third_party', 'sqlite', 'sqlite3', 'fatfs', 'zlib', 'microtar', 'os32_lz4'}
# A pointer type is unambiguous without resolving typedefs. Scalar casts use
# built-in specifiers and the integer typedefs used by this tree, so (p) remains
# an operand rather than being mistaken for a type name.
POINTER_TYPE = r'(?:\w+\s+)*\w+\s*(?:\*\s*(?:(?:const|volatile)\s*)?)+'
SCALAR_WORD = r'(?:const|volatile|unsigned|signed|short|long|int|char)'
# Read direct built-in integer typedefs; resolving arbitrary aliases is out of scope.
INTEGER_TYPEDEFS = set(re.findall(
    r'\btypedef\s+(?:(?:unsigned|signed|short|long|int|char)\s+)+(\w+)\s*;',
    (ROOT / 'include/types.h').read_text()))
INTEGER_TYPEDEFS.update(('u64', 's8', 's16', 's32', 's64',
                         'uintptr_t', 'intptr_t', 'size_t'))
INTEGER_NAMES = '|'.join(re.escape(name) for name in sorted(INTEGER_TYPEDEFS))
SCALAR_TYPE = (r'(?:(?:const|volatile)\s+)*(?:' + SCALAR_WORD +
               r'(?:\s+' + SCALAR_WORD + r')*|' + INTEGER_NAMES + r')')
CAST = re.compile(r'\(\s*' + POINTER_TYPE + r'\)\s*')
ANY_CAST = re.compile(r'\(\s*(?:' + POINTER_TYPE + '|' + SCALAR_TYPE + r')\s*\)\s*')
INTEGER_CAST = re.compile(r'\(\s*(?:' + SCALAR_TYPE + r')\s*\)\s*')
PHYS = re.compile(r'\b(?:MEM_\w*(?:BASE|ADDR)|TVRAM_\w+|VRAM_PLANE_\w+|PEGC_\w*_BASE|BIOS_WORK_\w+|KHEAP_BASE|KAPI_ADDR|V86_(?:TEST_\w+_ADDR|IPL_ADDR|REMAP_START)|DB_SHM_PTR|\w*_phys|phys|pa|paddr|pfn)\b')
USER = re.compile(r'\b(?:RING3_\w+|MEM_EXEC_LOAD_ADDR|MEM_APP_BAND_\w+|MEM_LEASE_\w+|uva|user_\w+)\b')
FUNCTION = re.compile(r'^\w[^;{}\n]*?\b(\w+)\s*\([^;{}]*?\)\s*\{', re.M)
SINK = re.compile(r'\b(?:paging_\w+|dma_chan_setup|dma_setup|arch_mmu_load_root)\s*\(')


def code_only(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  source, flags=re.S)


def expression(code, start):
    """One unary operand: casts, grouping, and field/index/call suffixes."""
    i = start
    while i < len(code) and code[i].isspace():
        i += 1
    cast = ANY_CAST.match(code, i)
    if cast:
        _, end = expression(code, cast.end())
        return code[start:end], end
    if i < len(code) and code[i] in '&*+-~!':
        _, end = expression(code, i + 1)
        return code[start:end], end
    if i < len(code) and code[i] == '(':
        i = balanced_end(code, i)
    else:
        m = re.match(r'(?:0[xX][0-9a-fA-F]+[uUlL]*|\w+)', code[i:])
        i += len(m[0]) if m else 0
    while i < len(code):
        suffix = re.match(r'\s*(?:->|\.)\s*\w+', code[i:])
        if suffix:
            i += len(suffix[0])
            continue
        j = i
        while j < len(code) and code[j].isspace():
            j += 1
        if j < len(code) and code[j] in '([':
            i = balanced_end(code, j)
        else:
            break
    return code[start:i], i


def balanced_end(code, start):
    opening = code[start]
    closing = ')' if opening == '(' else ']'
    depth, i = 1, start + 1
    while i < len(code) and depth:
        depth += (code[i] == opening) - (code[i] == closing)
        i += 1
    return i


def operand(expr):
    """Remove leading casts and enclosing groups, preserving the real value."""
    expr = expr.strip()
    while expr:
        cast = ANY_CAST.match(expr)
        if cast:
            expr = expr[cast.end():].strip()
        elif expr.startswith('(') and balanced_end(expr, 0) == len(expr):
            expr = expr[1:-1].strip()
        else:
            break
    return expr


def without_call_arguments(expr):
    """Names inside calls are inputs, not the value being cast to an integer."""
    result, start = [], 0
    while True:
        call = re.search(r'\b\w+\s*\(', expr[start:])
        if not call:
            result.append(expr[start:])
            return ''.join(result)
        opening = start + call.end() - 1
        result.append(expr[start:opening + 1])
        result.append(')')
        start = balanced_end(expr, opening)


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
        expr = operand(expr)
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
        for cast in INTEGER_CAST.finditer(args):
            expr, _ = expression(args, cast.end())
            expr = without_call_arguments(operand(expr))
            if (pointer_names.intersection(re.findall(r'\b\w+\b', expr)) or
                    re.search(r'&\s*\w+|\b\w*(?:buffer|buf|table|directory|ptr)\w*\b', expr)):
                add(m.start(), 'physical-sink')
                break
    for m in INTEGER_CAST.finditer(code):
        expr, _ = expression(code, m.end())
        # HostDrv address candidates also need an audited exception: IA32's
        # hypercall ABI uses CR3-relative linear addresses (T1-U6), not V2P.
        # Status members are integers, not the HostDrv structures' addresses.
        if re.fullmatch(r'(?:&\s*)?g_(?:invoke|stack|iostatus|databuf|sop|fsctx|fobj|namebuf|secctx)', operand(expr)):
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
