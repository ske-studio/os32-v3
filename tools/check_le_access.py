#!/usr/bin/env python3
"""External LE formats: reject AST casts that increase pointee alignment."""
import argparse
import clang_ast as ast
GUARDED = ['fs/ext2_super.c','fs/ext2_inode.c','fs/ext2_dir.c','fs/ext2_file.c',
           'fs/ext2_fmt.c','fs/ext2_vfs.c','fs/iso9660.c','drivers/kcg.c','lib/utf8.c']


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
                if rel in GUARDED and ast.alignment_cast(c):
                    hits.add(f'{rel}:{c.location.line}: external LE alignment cast; use endian_le.h')
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
