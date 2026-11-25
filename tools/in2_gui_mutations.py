"""Exclusive-input acceptance image edits and guest observations."""
D_MUTATIONS = {
    'closed': ('explicit', 'sys/kapi/kapi_in2.c', '#include "platform_input_caps.h"',
               '#define IN2_PUBLIC_EXCLUSIVE 0\n#include "platform_input_caps.h"'),
    'background': ('explicit', 'sys/hal/input/in2_excl.c',
                   'if (!owner || owner != av_focus_owner()) return OS32_ERR_BUSY;\n    unsigned f = irq_save();\n    if (owner != acq_owner',
                   'unsigned f = irq_save();\n    if (owner != acq_owner'),
    'drop-tab': ('explicit', 'sys/hal/input/in2_excl.c', ' || sc == KEY_TAB', ''),
    'release-lost': ('explicit', 'sys/hal/input/in2_excl.c', '    in2_ring_lost(IN2_LOST_EXCL);',
                     '    if (focus || owner) in2_ring_lost(IN2_LOST_EXCL);'),
    'keep-owner': ('focus', 'sys/hal/input/in2_excl.c', 'if (acq_owner == old) transition(0, 0, 1);',
                   'if (acq_owner == old) transition(acq_owner, excl, 1);'),
    'keep-capture': ('explicit',),
    'keep-left': ('focus',),
    'keep-right': ('exit',),
}

D_FAILURES = {
    'closed': ('D-GUI explicit HELD', 'C-GUI foreground acquire rc=-10'),
    'background': ('D-GUI busy-Q BACKGROUND', 'C-GUI background acquire rc=0'),
    'drop-tab': ('D-GUI exclusive KEY 15 1', 'D-GUI RAW 15 1'),
    'release-lost': ('D-GUI exclusive UP', 'C-GUI missing release LOST_EXCL='),
    'keep-owner': ('D-GUI focus-Q FOREGROUND EXCLUSIVE', 'C-GUI foreground flags=3'),
    'keep-capture': ('D-GUI explicit HELD', 'C-GUI residual drag='),
    'keep-left': ('D-GUI focus HELD', 'C-GUI residual left='),
    'keep-right': ('D-GUI exit-P HELD', 'C-GUI residual right='),
}

PC98_MUTATIONS = {name: D_MUTATIONS[name] for name in
                  ('closed', 'background', 'drop-tab', 'release-lost', 'keep-owner')}
PC98_MUTATIONS.update({
    'no-release': ('exit', 'sys/hal/input/in2_reclaim.c', '    in2_excl_release(owner);', ''),
    'keep-capture': ('explicit', 'userland/gshell/src/excl.rs', 'input::cancel_capture(st);', ''),
})
