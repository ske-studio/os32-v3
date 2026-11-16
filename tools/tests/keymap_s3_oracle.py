"""Independent B§2a oracle: reviewed M0 BSD fixture, never product tables."""
from array import array
from test_input_pc98_bsd import pc98_bsd_tokens, pc98_bsd_decode
DOS = {0:27, 15:9, 28:13, 53:0, 54:18, 55:3, 56:22, 57:127,
       58:30, 59:29, 60:28, 61:31, 62:1, 63:5, 81:0, 82:0, 83:0,
       84:0, 96:0, 97:0, **{i:0 for i in range(98,108)}, 114:0}

def rows(root):
    result = []
    for line in (root/'tools/tests/input_pc98_bsd_fixture.kbd').read_text().splitlines():
        if not line or line.startswith('#'): continue
        f = pc98_bsd_tokens(line)
        sc = int(f[0]); assert sc == len(result)
        result.append(([pc98_bsd_decode(t)[0] if not pc98_bsd_decode(t)[1] else 0
                        for t in f[1:9]], f[10] == 'C'))
    assert len(result) == 128
    return result

def byte(table, sc, mods):
    if sc in DOS: return DOS[sc]
    cells, caps = table[sc]
    col = bool(mods & 1) | (bool(mods & 16) << 1) | (bool(mods & 8) << 2)
    if caps and mods & 2: col ^= 1
    return cells[col]

def differences(root):
    result = {}
    for line in (root/'tools/tests/input_s3_differences.tsv').read_text().splitlines():
        if line.startswith('#'): continue
        sc, mods, old, new = (int(v,16) for v in line.split())
        assert (sc, mods) not in result and old != new and not mods & 4
        result[sc, mods] = old, new
    assert len(result) == 304
    return result

def bsd_trace(root, data):
    delta = differences(root)
    a = array('I'); a.frombytes(data)
    i = 0
    while i < len(a):
        mods = a[i+1]
        i += 132  # scan/mods/abort/guest + 128 pressed states
        while a[i] != 0xffffffff:
            sc = a[i] >> 8
            change = delta.get((sc, mods & ~4))
            if change:
                old, new = change
                assert a[i] & 255 == old, (sc, mods, a[i], old)
                a[i] = (sc << 8) | new
            i += 1
        i += 1
        while a[i] != 0xfffffffe: i += 1
        i += 1
        while a[i] != 0xfffffffd: i += 1
        i += 1
    return a.tobytes()

def c_oracle(root):
    table = rows(root)
    values = ',\n'.join('{' + ','.join(str(byte(table,sc,m)) for m in range(32)) + '}'
                         for sc in range(128))
    return 'static const unsigned char s3_expected[128][32] = {\n'+values+'\n};\n'
