"""Host policy read from the guest subsystem header, without fallback values."""
from pathlib import Path
import re

OSRI_COMPARE_OUT_LIMIT = 47
PROFILE_HEADER = Path(__file__).resolve().parents[1] / 'gfx/osri_profile.h'


def read_out_budget(path=PROFILE_HEADER):
    text = Path(path).read_text()
    text = re.sub(r'/\*.*?\*/|//[^\n]*',
                  lambda match: ' ' + '\n' * match[0].count('\n'), text, flags=re.S)
    definitions = re.findall(r'^\s*#\s*define\s+OSRI_DEFAULT_OUT_BUDGET\b([^\n]*)',
                             text, flags=re.M)
    if len(definitions) != 1:
        raise ValueError('expected exactly one OSRI_DEFAULT_OUT_BUDGET definition')
    value = re.fullmatch(r'\s*([1-9][0-9]*)[uU]?\s*', definitions[0])
    if value is None or not 1 <= int(value[1]) <= OSRI_COMPARE_OUT_LIMIT:
        raise ValueError('invalid OSRI_DEFAULT_OUT_BUDGET integer')
    return int(value[1])


OSRI_DEFAULT_OUT_BUDGET = read_out_budget()
