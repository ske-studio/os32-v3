"""Expected private hwdev fingerprints after GF_DRIVER P4/P5."""

HWDEV_CONTRACT_CRC = 0x770af7ec
HWDEV_LAYOUT_CRC = 0x1eae31dc


def assert_hwdev_contract(root):
    text = (root / 'hwdev/generated/hwdev_contract.h').read_text()
    for name, value in (('HWDEV_CONTRACT_CRC', HWDEV_CONTRACT_CRC),
                        ('HWDEV_LAYOUT_CRC', HWDEV_LAYOUT_CRC)):
        assert f'#define {name} 0x{value:08x}U' in text, name
