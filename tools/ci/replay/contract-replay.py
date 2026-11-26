from pathlib import Path
import json,subprocess,sys,os,re
from unittest.mock import patch
root=Path.cwd()
sys.path.insert(0,str(root/'tools'))
import gen_sqlite_imports as generator
rev='v3-pre-restructure'
mapping={r['to']:r['from'] for r in json.loads(Path(__file__).with_name('move_map.json').read_text())}
out=Path(os.environ['TMPDIR'])/'contract-replay'
normal=Path.read_text
for definition in sorted((root/'tools/prototypes').glob('*_imports.def')):
 module=definition.name.removesuffix('_imports.def')
 doc=json.loads(normal(definition))
 contracts=doc.get('contracts')
 if not contracts or 'imports' not in doc:continue
 def source(version):
  def read(p,*args,**kwargs):
   try:rel=str(p.relative_to(root))
   except ValueError:rel=''
   if version=='baseline-text' and rel in contracts:
    return subprocess.check_output(['git','show',rev+':'+mapping.get(rel,rel)]).decode()
   return normal(p,*args,**kwargs)
  return read
 values=[]
 for name in ['candidate','baseline-text']:
  d=out/module/name
  with patch.object(Path,'read_text',source(name)):
   generator.generate(d,definition=definition,module=module,contracts_only=True)
  values.append(re.search(r'MODULE_ABI_GEN (0x\w+)u',normal(d/(module+'_imports.h')))[1])
 print(module,*values,'CHANGED' if len(set(values))>1 else 'same')
