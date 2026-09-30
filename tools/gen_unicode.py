import re, struct

with open('lib/unicode_jis_table.h', 'r') as f:
    content = f.read()

entries = []
for match in re.finditer(r'\{0x([0-9A-Fa-f]{4}),0x([0-9A-Fa-f]{4})\}', content):
    uni, jis = int(match.group(1), 16), int(match.group(2), 16)
    entries.append((uni, jis))

# table size is 7063. We need a fast lookup, but the original code mapped 7063 elements directly in an array.
# The array in C is just 7063 * 4 bytes = 28252 bytes!
# WAIT! The array is 7063 elements! NOT 65536!
