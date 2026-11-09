"""Negative-image contracts. H3 must register real fault images explicitly.

A contract requires both a preceding progress marker and a dedicated stop symbol;
startup failures and deadlines never constitute a runtime RED.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Negative:
    fatal: str
    reached: str
    stop: str


NEGATIVES = {
    'fixture-fatal': Negative('FATAL fixture', 'KBOOT OF-OFF calls=1',
                              'fixture_stop'),
}

# Mutate the meter's product lines, then exercise it with a guest that must be
# rejected. Each row names the fixture and the exact outcome the mutant must
# produce: 'ACCEPT' (the invalid guest is accepted) or the *different* rejection
# reason of the next guard in depth. An unchanged reason, a timeout or any other
# failure is a test error, never a RED.
METER_MUTATIONS = (
    ('tick-check', 'abs(ticks*1000-us)*100 <= us*5', 'True', 'tick', 'ACCEPT'),
    ('duration-check', "us >= 2000000", 'True', 'duration', 'ACCEPT'),
    ('tick-rate', 'abs(tick_delta*hz-elapsed*1000)*100 <= elapsed*1000*5+2*hz*100',
     'True', 'slow-tick', 'ACCEPT'),
    ('ready-stop', "require(not halted or bool(msr & 0x8000), 'stopped after READY')",
     "require(True, 'stopped after READY')", 'stop', 'interrupts disabled after READY'),
    ('ee-window', "require(elapsed < hz, 'interrupts disabled after READY')",
     "if elapsed >= hz: break", 'ee-off', 'QMP tick rate'),
)
