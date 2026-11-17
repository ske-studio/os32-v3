#!/bin/sh
# Fixed NetBSD input; verifies downloads before replacing the reference directory.
set -eu
manifest=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/netbsd_wscons_sources.json
python3 - "$manifest" "${1:-$HOME/os32-tmp/ref/netbsd-wscons}" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

manifest = json.loads(Path(sys.argv[1]).read_text())
dest = Path(sys.argv[2]).expanduser()
with tempfile.TemporaryDirectory() as tmp:
    staging = Path(tmp)
    for name, digest in manifest['files'].items():
        target = staging/name
        target.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/NetBSD/src/{manifest['commit']}/{name}"
        subprocess.run(['curl', '--fail', '--location', '--max-time', '60',
                        '--output', str(target), url], check=True)
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise SystemExit('SHA256 mismatch: '+name)
    for name in manifest['files']:
        target = dest/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((staging/name).read_bytes())
    (dest/'SOURCES.md').write_text('# NetBSD wscons/HID sources\n'
        'github.com/NetBSD/src commit '+manifest['commit']+'\n\n'+''.join(
            digest+'  '+name+'\n' for name, digest in manifest['files'].items()))
PY
