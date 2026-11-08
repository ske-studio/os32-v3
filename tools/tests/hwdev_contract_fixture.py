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
accel getter and gfx_present imports at indices 40/41. Layer 4 appends the kernel-owned gf_display_test at index 42 and adds
Context v5, eight HAL hooks, display4, per-head mode/snapshot and script enums.
Layer 5 appends gf5_test at index 43. The reviewed e790c537 inventory
adds the F5 shared types/enums, production.feature5 and F4 logical save types
reachable through feature5.h (470 added lines; existing entries retained).
The layout pin includes these declarations, not just the new import.
FIX3 adds gf5 fan_off/poll_off/poll_source for optional-worker shutdown and diagnostics.
S03 adds gf2_chan.staging_pending in existing padding (offset 899) to retain
outstanding transfer ownership. Existing sizes and member offsets stay fixed.
IRQ B01 adds the small pre-ACK first-fault latch to gf_irq_stats.
M5 appends resource_lookup/device_map/device_unmap at indices 44-46;
only the import declaration pin changes, not the layout/inventory pins.
Guard lifetime D1-D6 adds dynamic ledger intervals, borrow nodes, view loans,
F5 resident tile slots/saved raw values, cleanup_pending and management abandon.
GF5_IMAGE_VERSION is 3 after GF D0: optional fan/volt/clock availability and
raw boot core/shader/mux snapshot declarations. The inventory/layout pins
include those additions; import indices/signatures and public KAPI are unchanged.
Guard D0 adds the private plan guard, ROM generation and live heat state,
plus ENODEV and perf pstate/bios_index (the latter occupy existing padding).
make all regenerates the contract after the reviewed inventory update.
These changes own the pins below.
OS32X v5 appends OS32Header.code_size at offset 60 and grows it to 64 bytes;
the HWDEV inventory/layout pins include that shared header change.
GF CUI fix12 adds per-opcode TMDS timing and two saved scan origins.
KAPI v81 appends ime_dict_suspend at KernelAPI offset 1040, moving the
reserved tail to 1044 and shrinking it from 168 to 164 bytes. The inventory
adds that member; refresh both pins with --snapshot after reviewing the diff.
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
# GFX_ACCEL_HAL H1 appends INDEX8 direct hooks and GFX_Cmd (32B); P0b appends enum GF6_PRIM=12.
REVIEWED_LAYOUT_CRC = 0x927d90af
REVIEWED_IMPORT_DECLARATIONS_SHA = '479579e77f4bd48360001cccbca42acc45fcb46d1ed3d64c6e6860f0f37cb156'
REVIEWED_INVENTORY_SHA = '1b5fe57d12d1adaf489c2c43f8dcd707842f99ef4181b1af65cca73ccdf65214'


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
