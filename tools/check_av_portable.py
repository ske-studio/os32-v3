"""Check that common AV HAL sources use portable device contracts."""
from pathlib import Path
import re


def violations(root):
    for directory in ('sound/hal', 'input/hal'):
        for path in sorted((root / directory).glob('*.[ch]')):
            text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
            for line in text.splitlines():
                if re.search(r'^\s*#\s*include.*(?:drivers/|platform/|arch/)', line):
                    yield f'{path}: {line.strip()}'
                elif re.search(r'\b(?:inp|outp|inb|outb|inw|outw)\s*\(', line):
                    yield f'{path}: {line.strip()}'


if __name__ == '__main__':
    errors = list(violations(Path(__file__).resolve().parents[1]))
    if errors:
        raise SystemExit('\n'.join(errors))
