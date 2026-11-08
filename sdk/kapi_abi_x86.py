"""x86 cdecl fixed argument slots; no generated-file formatting lives here."""
import re
from target_profile import require

_SCALARS = {'char', 'signed char', 'unsigned char', 'short', 'short int',
            'unsigned short', 'int', 'unsigned', 'unsigned int', 'long',
            'long int', 'unsigned long', 'u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'os_time_t'}


def fixed_arg_bytes(args, target='x86-pc98'):
    require(target, 'layout')
    count = 0
    for declaration in args:
        if declaration == '...':
            continue
        if '*' not in declaration:
            kind = re.sub(r'\s+[A-Za-z_][A-Za-z_0-9]*$', '', declaration).strip()
            if kind not in _SCALARS:
                raise ValueError('unsupported x86 by-value argument: ' + declaration)
        count += 1
    return 4 * count
