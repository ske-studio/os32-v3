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
# rejected. A returned acceptance is an observed runtime oracle failure.
METER_MUTATIONS = (
    ('tick-check', 'abs(ticks*1000-us)*100 <= us*5', 'True', 'tick'),
    ('duration-check', "us >= 2000000", 'True', 'duration'),
)
