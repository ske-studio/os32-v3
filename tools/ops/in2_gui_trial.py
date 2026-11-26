"""Drive one cold-boot D trial using the guest serial stream on either machine."""
import re


def run_trial(s, role):
    p = 'exit-P' if role == 'exit' else role
    reader = {'focus': 'focus-Q', 'exit': 'exit'}.get(role, p)

    def state(who, phase):
        pattern = rf'D-GUI {who} {phase} front=(\d+) fg=(\d+) owner=(\d+) flags=(\d+) gen=(\d+) caller=(\d+)\r?\n'
        match = s.wait_match(pattern)
        front, focus, owner, flags, gen, caller = map(int, match.groups())
        assert caller and (caller != focus if who == 'busy-Q' else caller == focus)
        return front, focus, owner, flags, gen

    q = state('exit', 'FRONT') if role == 'exit' else None
    front = state(p, 'FRONT')
    assert front[1] and front[2:4] == (0, 0)
    if q:
        assert q[0] != front[0] and q[1] != front[1]
    button = 'right' if role == 'exit' else 'left'
    s.button(button, True)
    assert state(p, 'HELD') == front
    acquired = state(p, 'SETTLED')
    assert acquired == (*front[:2], front[1], 3, front[4] + 1)
    s.button(button, False)
    if role == 'explicit':
        assert state('busy-Q', 'BUSY') == acquired
        assert state(p, 'RESUMED') == acquired
        offset = s.offset
        for key, scan in [('a', 29), ('pause', 96), ('ctrl', 116), ('alt', 115), ('tab', 15)]:
            for down in (1, 0):
                s.key(key, bool(down))
                s.seen(f'D-GUI exclusive KEY {scan} {down}')
                if key != 'a':
                    s.seen(f'D-GUI RAW {scan} {down}')
        s.move(12, -7)
        s.seen('D-GUI exclusive MOVE')
        for down in (True, False):
            s.button('left', down)
            s.seen('D-GUI exclusive ' + ('DOWN' if down else 'UP'))
        assert not re.search(r'C-GUI KEY \d+ [01]', s.trace()[offset:s.trace().index('D-GUI exclusive UP', offset)])
    elif role == 'focus':
        q = state(reader, 'FRONT')
        assert q[0] != front[0] and q[1] != front[1]
    else:
        assert state(p, 'EXIT') == acquired
    reasons = 'EXCL' if role == 'explicit' else 'FOCUS,EXCL'
    s.seen(f'D-GUI {reader} reader={(q or front)[1]} LOST={reasons}')
    released = state(reader, 'RELEASED')
    assert released[:4] == (*(q or front)[:2], 0, 0) and released[4] > acquired[4]
    for down in (True, False):
        s.key('a', down)
        s.seen('D-GUI ordinary ' + ('MAKE' if down else 'BREAK'))
    delta = s.wait_match(r'D-GUI title delta (-?\d+) (-?\d+)\r?\n')
    s.move(*map(int, delta.groups()))
    s.seen('D-GUI stationary')
    s.button('left', True)
    s.seen('D-GUI drag started')
    s.move(8, 4)
    s.seen('D-GUI drag moved')
    s.button('left', False)
    assert state(reader, 'DONE') == released
