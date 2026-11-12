#!/usr/bin/env python3
"""USB_DESIGN §2: keep common USB code behind its platform contract."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = ('usb_', 'ohci_', 'ehci_', 'hid_', 'umass_', 'scsi_')
SHELLS = {'usb_module.c', 'usb_resident.c', 'usb_early_quiesce.c'}  # early_quiesce: resident, no usbp_* (1E)
ALLOWED = {'usb_platform.h', 'endian_dma.h', 'endian_le.h', 'lib/kstring.h'}


def uncomment(text):
    # Retain string literals for include parsing, but remove comments (including
    # continued // comments after preprocessing line splices).
    text = re.sub(r'\\\r?\n', '', text)
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ' ' if m[0].startswith(('/', '/*')) else m[0],
                  text, flags=re.S)


def check(root):
    errors = []
    directory = root / 'drivers/usb'
    if not directory.exists():
        return errors
    for path in sorted(directory.rglob('*')):
        if path.suffix not in ('.c', '.h'):
            continue
        source = uncomment(path.read_text())
        rel = path.relative_to(root).as_posix()
        if (path.suffix == '.h' or
                (path.suffix == '.c' and path.name.startswith(PREFIXES)
                 and path.name not in SHELLS)):
            for match in re.finditer(r'^\s*#\s*include\b([^\n]*)', source, re.M):
                operand = match[1].strip()
                header = re.fullmatch(r'["<]([^">]+)[">]', operand)
                name = header[1] if header else ''
                local = (name.startswith('drivers/usb/') and name.endswith('.h')
                         and '..' not in name.split('/') and '//' not in name)
                if name not in ALLOWED and not local:
                    errors.append(f'{rel}: forbidden include {operand}')
        # USB hardware words are opaque, including in platform shells/headers.
        # Strings/comments mentioning .raw are documentation, not accesses.
        code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', ' ', source)
        if re.search(r'(?:\.|->)\s*raw\b', code):
            errors.append(f'{rel}: direct raw access (use endian_dma.h)')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    errors = check(args.root)
    for error in errors:
        print(error)
    if not errors:
        print('USB portable: OK')
    return int(bool(errors))


if __name__ == '__main__':
    raise SystemExit(main())
