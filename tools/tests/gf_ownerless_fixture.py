"""Ownerless F3/display fixtures; full module initialization is tested by GF4 C3.

Keep the actual platform/backend adapter and (for wire) F3 registration. This
is an explicit partial composition, not a replacement implementation of any
F4 API. No F4 owner, state, output slot or lifecycle is installed here.
"""


def ownerless_body(source, *, bind_f3):
    begin = source.index('    /* X3/N1:')
    end = source.index('    *context->foundation_out=&gf1_module_ops;', begin)
    registration = ''
    if bind_f3:
        a = source.index('    int rc=', begin, end)
        b = source.index('    /* C-group production ctx contract:', a, end)
        registration = source[a:b]
    body = source[:begin] + registration + source[end:]
    # These output slots belong to the omitted F4/F5 composition. The fixture
    # never advertises them; the real module's Context contract is covered by C3.
    for slot in ('display_out', 'display_restore_out', 'feature5_out'):
        line = f'    *context->{slot}=0;\n'
        assert body.count(line) == 1, slot
        body = body.replace(line, '', 1)
    return body.replace('geforce_module_init', 'gf_ownerless_module_init')
