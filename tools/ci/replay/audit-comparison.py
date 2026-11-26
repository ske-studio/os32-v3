from pathlib import Path
import hashlib,json,sys
p=Path(sys.argv[1]); base=p.parent/'base'; t=p/'tmp/verified-tree'; replay=p/'tmp/replay-tree'
sys.path.insert(0,str(Path.cwd()/'tools'))
from build_compare import candidate_path
moves={Path(r['from']):Path(r['to']) for r in json.loads(Path(__file__).with_name('move_map.json').read_text())}
objects={k.with_suffix('.o'):(k,v) for k,v in moves.items() if k.suffix in ('.c','.s','.S','.asm')}
diff={Path(l.split()[1]) for l in (p/'compare-final.log').read_text().splitlines() if l.startswith('DIFF ')}
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
draft=[];audit=[]
for line in (base/'SHA256SUMS').read_text().splitlines():
 digest,name=line.split('  ',1); old=Path(name);dest=candidate_path(t,old,moves,objects)
 if dest not in diff:continue
 assert sha(replay/dest)==digest,name
 h=sha(t/dest)
 if dest.suffix in ['.py','.rs','.h'] or dest.name in ('kselftest.o','kstr_bench.elf','kstr_asm_a.o','DEBUG.PKG') or dest.name in ('isr_stub.o','kentry.o','v86_entry.o','ring3_entry.o','setjmp.o','ne2000_io.o','kstring_asm.o'):
  reason='path'
 elif dest.name in ('hwdev_module.o','hwdev_core.o','hwdev_main.o','hwdev.elf'):
  reason='hwdev'
 elif dest.suffix=='.o':
  reason='module-abi'
 else:reason='combined'
 draft.append(f'Sep-Changes: reviewed-replay {old} {h} {reason}')
 audit.append(f'{old}\t{dest}\t{reason}\t{digest}\t{h}')
assert len(audit)==len(diff)==93
(p/'sep-changes.txt').write_text('\n'.join(draft)+'\n')
(p/'reviewed-differences.tsv').write_text('baseline\tcandidate\tcause\tbaseline_and_replay_sha256\tcandidate_sha256\n'+'\n'.join(audit)+'\n')
