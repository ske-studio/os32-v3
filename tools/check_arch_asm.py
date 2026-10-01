#!/usr/bin/env python3
"""CPU asm templates from clang GCC_ASM_STMT; preserve architecture exceptions."""
import argparse
import pathlib
import re
import clang_ast as ast
from clang_ast.asm import templates
K = ast.K
SCAN_DIRS = ['kernel','drivers','exec','fs','kapi','lib','net','gfx','include','arch','platform']
ALLOW_RE = re.compile(r'^arch/[^/]+/arch_[A-Za-z0-9_]+\.h$')
ALLOW_LOOKBACK = 16
MNEMONIC_RE = re.compile(r'(?:^|[;\s])(hlt|cli|sti)(?=$|[;\s])')


def findings(tu, root=ast.ROOT):
    nodes = list(ast.walk(tu.cursor))
    definitions = {c.spelling:(c,list(c.get_tokens())[1:]) for c,_ in nodes if c.kind == K.MACRO_DEFINITION}
    expanded = templates(tu)
    hits = set()
    comment_marks = {}
    def marked(rel, line):
        if rel not in comment_marks:
            path = pathlib.Path(root,rel)
            file = tu.get_file(str(path.absolute()))
            start = ast.cx.SourceLocation.from_offset(tu,file,0)
            end = ast.cx.SourceLocation.from_offset(tu,file,path.stat().st_size)
            extent = ast.cx.SourceRange.from_locations(start,end)
            comment_marks[rel] = [t.location.line + t.spelling[:t.spelling.index('ARCH-ASM-OK')].count('\n')
                for t in tu.get_tokens(extent=extent)
                if t.kind == ast.cx.TokenKind.COMMENT and 'ARCH-ASM-OK' in t.spelling]
        return any(line-ALLOW_LOOKBACK <= n <= line for n in comment_marks[rel])
    for c,_ in nodes:
        rel = ast.relpath(c,root)
        if c.kind != K.ASM_STMT or not rel or rel.split('/')[0] not in SCAN_DIRS:
            continue
        if rel.startswith('lib/sqlite3/') or ALLOW_RE.fullmatch(rel):
            continue
        if marked(rel,c.location.line):
            continue
        expansion = next((m for m,_ in nodes if m.kind == K.MACRO_INSTANTIATION
                          and m.location.offset == c.location.offset
                          and str(m.location.file) == str(c.location.file)),None)
        if expansion is not None and expansion.spelling in definitions:
            definition,_ = definitions[expansion.spelling]
            origin = ast.relpath(definition,root)
            if origin and ALLOW_RE.fullmatch(origin):
                continue
        for match in MNEMONIC_RE.finditer(expanded[c.hash]):
            hits.add((rel,c.location.line,match[1]))
    return sorted(hits)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',default=ast.ROOT)
    root = p.parse_args().root
    hits,errors = set(),[]
    try:
        for u in ast.units(root):
            if u['src'].split('/')[0] not in SCAN_DIRS or u['src'].startswith('lib/sqlite3/'):
                continue
            hits.update(findings(ast.parse(u['src'],u['argv'],root),root))
    except RuntimeError as e:
        errors.append('clang parse failure: ' + str(e))
    for rel,ln,mn in sorted(hits):
        print(f'{rel}:{ln}: inline asm `{mn}`; use include/io.h')
    for e in errors:
        print(e)
    print(f'arch asm: {len(hits)} violations; {len(errors)} parse failures')
    return bool(hits or errors)

if __name__ == '__main__':
    raise SystemExit(main())
