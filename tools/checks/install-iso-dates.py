"""Installation media must be identical when only input timestamps change."""
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='install-iso-dates', tier=1, needs=[], budget=5,
             covers=['build/image.mk'], basis=['docs/POLICY_CI.md G11'])


def run(ctx):
    recipe = (ctx.root / 'build/image.mk').read_text()
    env = os.environ.copy()
    for name in ('SOURCE_DATE_EPOCH', 'MAKEFLAGS', 'MFLAGS'):
        env.pop(name, None)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        packages = root / 'packages'
        packages.mkdir()
        pkg = packages / 'SAMPLE.PKG'
        pkg.write_bytes(b'OS32')
        for preserve_mtime in ([False, True] if ctx.mutate else [False]):
            option = '--set_all_file_dates "=$(INSTALL_ISO_EPOCH)"'
            assert recipe.count(option) == 1
            (root / 'Makefile').write_text(recipe.replace(option, '') if preserve_mtime else recipe)
            images = []
            for stamp in (946684900, 946685000):
                os.utime(pkg, (stamp, stamp))
                subprocess.run(['make', '-o', 'packages', 'iso'], cwd=root, env=env, check=True)
                image, = (root / 'images').glob('*.iso')
                images.append(image.read_bytes())
            if preserve_mtime:
                assert images[0] != images[1]
                print('RED: preserving input mtime changed the installation ISO')
            else:
                assert images[0] == images[1]
    return 0
