"""Pin private HWDEV ABI while allowing GeForce mode/verification values.

The import expectations were captured from ca28d81b (main 4e96d02d).
GF_FEAT1 A0 appends the two foundation hooks to gf_display_owner; the layout
CRC below pins that declaration plus CK's stage foundation reference and
fixed engine-stop rule flag. CW adds the lease foundation pointer, cleanup
hook and module-context declarations, and appends geforce_foundation_ops at
HWDEV index 37 (the existing indices remain fixed). C9b appends
serial_puts_polled at index 38 for the serial response, and includes the
GF1/production/foundation shared types for display callbacks and row reads.
Layer 2 adds the command hooks/channel and GF2 diagnostic types, and appends
sys_time_now at private index 39. The 2026-11-03 import ceiling removal drops
that member from module_component_policy and shifts its following export/load-span
fields. Layer 3 adds accel/VRAM types and production.accel, plus the
accel getter and gfx_present imports at indices 40/41. These changes own the pins below.
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

# Review pins, not values derived from the artifact being checked. Layer 2
# appends sys_time_now, GF2 types, command/command_get and the production chan.
# After reviewing the declaration/inventory/layout diff, print replacement pins:
#   python3 -B tools/tests/hwdev_contract_fixture.py --snapshot
REVIEWED_LAYOUT_CRC = 0xb08dcefc
REVIEWED_IMPORT_DECLARATIONS_SHA = 'b4f450fd3778df3d2c0e7e83817c1c37148d55ca87fe5f248df5a89be434afb5'
REVIEWED_INVENTORY_SHA = 'd3e458e9915ac2c8bea09007e0d9cd74a011bd3b69d3a3840ea1270c636385ae'


def import_declarations(root):
    entries = json.loads((root / 'hwdev/hwdev_imports.def').read_text())['imports']
    return ''.join(f'{e["index"]}:{e["symbol"]}:'
                   f'{"".join(declaration(e, root).split())}\n' for e in entries)


def assert_hwdev_inventory(root):
    assert hashlib.sha256((root / 'tools/hwdev_layout.json').read_bytes()).hexdigest() == REVIEWED_INVENTORY_SHA, \
        'reviewed HWDEV type/member/enum inventory changed'


def assert_hwdev_contract(root):
    generated = root / 'hwdev/generated'
    text = (generated / 'hwdev_contract.h').read_text()
    values = {name: int(re.search(r'#define ' + name + r' 0x([0-9a-f]+)U', text)[1], 16)
              for name in ('HWDEV_CONTRACT_CRC', 'HWDEV_LAYOUT_CRC')}
    imports = json.loads((generated / 'hwdev_imports.json').read_text())
    assert values['HWDEV_CONTRACT_CRC'] == imports['module_abi_gen']
    assert values['HWDEV_LAYOUT_CRC'] == zlib.crc32((generated / 'hwdev_layout.txt').read_bytes())
    entries = json.loads((root / 'hwdev/hwdev_imports.def').read_text())['imports']
    assert hashlib.sha256(import_declarations(root).encode()).hexdigest() == REVIEWED_IMPORT_DECLARATIONS_SHA, \
        'private HWDEV import indices/names/types changed'
    assert imports['imports'] == [dict(name=e['symbol'], index=e['index'], kind=1, type_ver=1)
                                  for e in entries], 'private HWDEV import metadata changed'
    assert values['HWDEV_LAYOUT_CRC'] == REVIEWED_LAYOUT_CRC, 'private HWDEV layout changed'


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Print current HWDEV review pins; review diffs before accepting them.')
    parser.add_argument('--snapshot', action='store_true', required=True)
    parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    print(f'REVIEWED_LAYOUT_CRC = 0x{zlib.crc32((root / "hwdev/generated/hwdev_layout.txt").read_bytes()):08x}')
    print(f'REVIEWED_IMPORT_DECLARATIONS_SHA = {hashlib.sha256(import_declarations(root).encode()).hexdigest()!r}')
    print(f'REVIEWED_INVENTORY_SHA = {hashlib.sha256((root / "tools/hwdev_layout.json").read_bytes()).hexdigest()!r}')
