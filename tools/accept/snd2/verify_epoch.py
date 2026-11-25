import sys
from pathlib import Path

from verify_wave import capture_paths

if len(sys.argv) < 3:
    sys.exit("usage: verify_epoch.py baseline.json continued.json [...]")
fresh = capture_paths(map(Path, sys.argv[1:]))
marks = [(i,(r-10000)*256) for i,(_,r) in enumerate(fresh) if r]
assert len(marks) >= 2 and all(0 <= frame < 1323000 for _,frame in marks)
assert all(j-i == 256 and g-f == 256 for (i,f),(j,g) in zip(marks,marks[1:]))
print('continued audio through application frame', marks[-1][1])
