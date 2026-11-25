import json
import re
import sys
from pathlib import Path

PATHS = ('snd-start', 'snd-pause', 'snd-resync', 'snd-advance', 'snd-teardown',
         'snd-other', 'if0-resident', 'irq0', 'irq10')


def verify(rate, log):
    complete = 'TSC=raw cycles observation_lost=0' in log
    if not complete:
        print('未測定: TSC observation_lost')
    maximum = 0
    scale = 1 << rate['shift']
    for name in PATHS:
        rows = re.findall(r'^' + name + r' count=(\d+) worst=(0x[0-9a-fA-F]+)', log, re.M)
        if len(rows) != 2 or not any(int(n) for n, _ in rows):
            print('未測定:', name)
            complete = False
            continue
        worst = max(int(value, 16) for _, value in rows)
        us = (worst * rate['mult'] + scale - 1) // scale
        print(name, worst, 'cycles', us, 'us')
        maximum = max(maximum, us)
    print('max_us', maximum)
    return complete and maximum <= 1450


if __name__ == '__main__':
    sys.exit(0 if verify(json.loads(Path(sys.argv[1]).read_text()),
                         Path(sys.argv[2]).read_text()) else 1)
