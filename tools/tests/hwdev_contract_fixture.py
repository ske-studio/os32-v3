"""Check the current private HWDEV pair without pinning GeForce mode values.

Public ABI snapshots remain in the six dependency tests. Private declaration
fingerprints follow regeneration; actual mismatches are rejected by the loader
cases in test_hwdev_image (core contract/layout and manifest contract/layout).
"""
import json
import re
import zlib


def assert_hwdev_contract(root):
    generated = root / 'hwdev/generated'
    text = (generated / 'hwdev_contract.h').read_text()
    values = {name: int(re.search(r'#define ' + name + r' 0x([0-9a-f]+)U', text)[1], 16)
              for name in ('HWDEV_CONTRACT_CRC', 'HWDEV_LAYOUT_CRC')}
    imports = json.loads((generated / 'hwdev_imports.json').read_text())
    assert values['HWDEV_CONTRACT_CRC'] == imports['module_abi_gen']
    assert values['HWDEV_LAYOUT_CRC'] == zlib.crc32((generated / 'hwdev_layout.txt').read_bytes())
