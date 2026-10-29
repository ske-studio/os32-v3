"""Pin private HWDEV ABI while allowing GeForce mode/verification values.

The independent expectations below were captured from ca28d81b (main 4e96d02d).
Layout includes sizes, alignments, member offsets/sizes and enum values. Import
indices, names and function declarations stay fixed even after regeneration.
The full contract CRC includes mode constants/inline checks, so only its pair
consistency is pinned here. Public ABI snapshots remain in the dependency tests.
"""
import hashlib
import json
from pathlib import Path
import re
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gen_kdebug_imports import declaration

LAYOUT_CRC = 0x64142dfa
IMPORT_DECLARATIONS_SHA = '34b227461aff152720340d8087159a5a7554670259f4ea473947478752ba6d89'


def assert_hwdev_contract(root):
    generated = root / 'hwdev/generated'
    text = (generated / 'hwdev_contract.h').read_text()
    values = {name: int(re.search(r'#define ' + name + r' 0x([0-9a-f]+)U', text)[1], 16)
              for name in ('HWDEV_CONTRACT_CRC', 'HWDEV_LAYOUT_CRC')}
    imports = json.loads((generated / 'hwdev_imports.json').read_text())
    assert values['HWDEV_CONTRACT_CRC'] == imports['module_abi_gen']
    assert values['HWDEV_LAYOUT_CRC'] == zlib.crc32((generated / 'hwdev_layout.txt').read_bytes())
    entries = json.loads((root / 'hwdev/hwdev_imports.def').read_text())['imports']
    declarations = ''.join(f'{e["index"]}:{e["symbol"]}:'
                           f'{"".join(declaration(e, root).split())}\n' for e in entries)
    assert hashlib.sha256(declarations.encode()).hexdigest() == IMPORT_DECLARATIONS_SHA, \
        'private HWDEV import indices/names/types changed'
    assert imports['imports'] == [dict(name=e['symbol'], index=e['index'], kind=1, type_ver=1)
                                  for e in entries], 'private HWDEV import metadata changed'
    assert values['HWDEV_LAYOUT_CRC'] == LAYOUT_CRC, 'private HWDEV layout changed'
