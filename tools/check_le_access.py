#!/usr/bin/env python3
"""External LE formats: reject multibyte direct access independently of alignment."""
import argparse
import clang_ast as ast
GUARDED = ['fs/ext2_super.c','fs/ext2_inode.c','fs/ext2_dir.c','fs/ext2_file.c',
           'fs/ext2_fmt.c','fs/ext2_vfs.c','fs/iso9660.c','drivers/kcg.c','lib/utf8.c']


def direct_access(c):
    # Old *(uNN *)&... rule, extended to canonical aliases and pointer casts.
    # Alignment is irrelevant to byte order. Catch split cast/dereference too.
    if c.kind != ast.K.CSTYLE_CAST_EXPR or not ast.pointer(c.type):
        return False
    pointee = c.type.get_pointee().get_canonical()
    children = list(c.get_children())
    return bool(children) and ast.pointer(children[-1].type) and \
        ast.integer(pointee) and pointee.get_size() > 1


def rules(c):
    result = []
    if direct_access(c):
        result.append('external LE multibyte direct access; use endian_le.h')
    if ast.alignment_cast(c):
        result.append('external LE alignment cast; use endian_le.h')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',default=ast.ROOT)
    root = p.parse_args().root
    hits, seen = set(), set()
    try:
        for u in ast.units(root):
            if u['src'] not in GUARDED:
                continue
            seen.add(u['src'])
            tu = ast.parse(u['src'],u['argv'],root)
            for c,_ in ast.walk(tu.cursor):
                rel = ast.relpath(c,root)
                if rel in GUARDED:
                    for rule in rules(c):
                        hits.add(f'{rel}:{c.location.line}: {rule}')
        if seen != set(GUARDED):
            raise ast.ParseError('missing guarded units: ' + str(set(GUARDED)-seen))
    except RuntimeError as e:
        hits.add('clang parse failure: ' + str(e))
    for h in sorted(hits):
        print(h)
    print(f'LE: {len(hits)} violations; {len(seen)} files')
    return bool(hits)

if __name__ == '__main__':
    raise SystemExit(main())
