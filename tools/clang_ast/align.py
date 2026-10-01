"""Manual alignment audit, with the same flags and AST predicate as the LE gate."""
import argparse
from . import ROOT, units, parse, walk, relpath, alignment_cast


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('scope',choices=('kernel','user','all'),default='all',nargs='?')
    p.add_argument('--root',default=ROOT)
    args = p.parse_args()
    hits, count = set(), 0
    try:
        for u in units(args.root):
            user = u['src'].startswith(('userland/','sdk/'))
            if args.scope != 'all' and user != (args.scope == 'user'):
                continue
            tu = parse(u['src'],u['argv'],args.root)
            count += 1
            for c,_ in walk(tu.cursor):
                rel = relpath(c,args.root)
                if rel and alignment_cast(c):
                    hits.add((rel,c.location.line,c.type.spelling))
    except RuntimeError as e:
        print('clang parse failure:',e)
        return 1
    for rel,ln,typ in sorted(hits):
        print(f'{rel}:{ln}: cast increases required alignment: {typ}')
    print(f'alignment: {len(hits)} candidates / {count} parsed units / 0 parse failures')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
