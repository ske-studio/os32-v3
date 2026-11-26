import sys,shlex,json,os,re
from pathlib import Path
p=Path(sys.argv[1]); t=p/'tmp/replay-tree'
def replace(f,a,b):
 s=f.read_bytes(); assert a in s,(f,a); f.write_bytes(s.replace(a,b))
for name,a,b,*_ in [l.split() for l in (p/'module-abi.log').read_text().splitlines() if 'CHANGED' in l]+[['hwdev','0xc176cbe2','0x866b203e']]:
 d=t/('build/out/'+name if name!='hwdev' else 'sys/dev/hwdev/generated')
 replace(d/(name+('_imports.h' if name!='hwdev' else '_contract.h')),a.encode(),b.encode())
 replace(d/(name+'_imports.json'),str(int(a,16)).encode(),str(int(b,16)).encode())
replace(t/'sys/platform/pc98/kdebug/kselftest/memmap.inc',b'"memmap: low memory matches sys/include/memmap.h"',b'"memmap: low memory matches include/memmap.h"')
def commands(log):
 return [shlex.split(l) for l in log.read_text().splitlines() if l.startswith('nasm ') and ' -o ' in l and ' -f elf32 ' in l]
base=commands(p.parent/'p2-0/first-build.log')
aliases={}
for args in commands(p/'rebuild-final.log'):
 dest=args[args.index('-o')+1]
 source=args[args.index('-o')-1]
 key=Path(dest).name
 matches={a[a.index('-o')-1] for a in base if Path(a[a.index('-o')+1]).name==key and ('/r1/' in a[a.index('-o')+1])==('/r1/' in dest)}
 if len(matches)!=1:continue
 legacy=matches.pop()
 if source==legacy:continue
 aliases[source]=legacy
 output=t/(dest.replace('kstr_asm_raw.o','kstr_asm_a.o'))
 output.unlink(missing_ok=True)
print('nasm spellings',aliases)
wrapper=p/'tmp/nasm-replay'
wrapper.write_text('#!/usr/bin/python3\nimport os,sys,shutil,pathlib\npaths='+repr(aliases)+'''\nargs=sys.argv[1:]
for i,a in enumerate(args):
 if a in paths:
  path=pathlib.Path(paths[a]);path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(a,path);args[i]=str(path)
os.execvp("nasm",["nasm",*args])
''')
wrapper.chmod(0o755)
