import json, re, sys
r = json.load(open(sys.argv[1]))
log = open(sys.argv[2]).read()
assert 'TSC=raw cycles observation_lost=0' in log
us = []
for name in ('snd-start','snd-pause','snd-resync','snd-advance','snd-teardown',
             'snd-other','if0-resident','irq0','irq10'):
    rows = re.findall(r'^'+name+r' count=(\d+) worst=(0x[0-9a-fA-F]+)', log, re.M)
    assert len(rows) == 2 and sum(int(n) for n,_ in rows) > 0, name
    worst = max(int(v,16) for _,v in rows)
    scale = 1 << r['shift']
    us.append((worst*r['mult']+scale-1)//scale)
    print(name, worst, 'cycles', us[-1], 'us')
print('max_us', max(us))
assert max(us) <= 1450, 'over quarter-period'
