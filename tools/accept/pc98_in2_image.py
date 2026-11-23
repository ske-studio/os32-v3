"""Build the PC-98 cold-boot disks without deploying to a live emulator."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from build_compare import snapshot
from mk_blank_nhd import create
from nhd_deploy import update_partition_table


def disk(tree, stage, output, work):
    image = work / output.name
    create(image, 24)
    start, length = update_partition_table(image)
    partition = work / 'partition.img'
    with partition.open('wb') as stream:
        stream.truncate(length * 512)
    subprocess.run(['mke2fs', '-q', '-F', '-t', 'ext2', '-b', '1024', '-I', '128',
                    '-O', 'none,filetype', '-d', stage, partition], check=True)
    with image.open('r+b') as dest:
        for lba, name, limit in [(0, 'boot_hdd.bin', 512), (2, 'loader_hdd.bin', 8192)]:
            data = (tree / 'boot' / name).read_bytes()
            if not 0 < len(data) <= limit:
                raise ValueError(name)
            dest.seek(512 + 512 * lba)
            dest.write(data)
        dest.seek(512 + 512 * start)
        with partition.open('rb') as src:
            shutil.copyfileobj(src, dest)
    os.replace(image, output)
    print(output, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    out = parser.parse_args().out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        work = Path(tmp)
        tree = work / 'source'
        snapshot(ROOT, 'HEAD', tree)
        shutil.copytree(ROOT / 'assets/fonts', tree / 'assets/fonts', dirs_exist_ok=True)
        env = dict(os.environ, OS32_BUILD_COMMIT='pc98-in2-d')
        def make(*goals):
            subprocess.run(['make', *goals], cwd=tree, env=env, check=True)
        make('kernel', 'boot', 'userland/shell.bin', 'userland/ppc/guiprob.bin',
             'shlib', 'fonts', 'build/out/settings.db')
        stage = work / 'root'
        for name in ['boot', 'sys/lib', 'bin', 'etc', 'data', 'tmp', 'home/user']:
            (stage / name).mkdir(parents=True, exist_ok=True)
        (stage / 'etc/system.cfg').write_text('GUI=1\n')
        for path in ['vmkernel.lz4', 'kdebug.bin', 'sqlite.mod', 'fep.mod', 'cd.mod',
                     'kcg.mod', 'fm.mod', 'pcm.mod', 'v86.mod', 'lan.mod', 'cirrus.mod',
                     'hwdev.mod', 'geforce.mod', 'trident.mod', 'usb.mod']:
            shutil.copy2(tree / 'build/out' / path, stage / 'boot')
        for src, dst in [('userland/shell.bin', 'sys'), ('userland/libos32gui.shlib', 'sys/lib'),
                         ('userland/ppc/guiprob.bin', 'bin'), ('build/out/settings.db', 'etc')]:
            shutil.copy2(tree / src, stage / dst)
        for name in ['JF-Dot-Shinonome16.ttf', 'JF-Dot-Shinonome16.g16', 'Inconsolata-Regular.ttf',
                     'JF-Dot-Shinonome16-LICENSE.txt', 'Inconsolata-OFL.txt']:
            shutil.copy2(tree / 'assets/fonts' / name, stage / 'data')
        for role in ['explicit', 'focus', 'exit']:
            env['PPC_GUI_TRIAL'] = role
            make('gshell', 'GSHELL_FEATURES=c_gui_accept')
            shell = tree / 'userland/gshell.bin'
            assert ('/bin/guiprob.bin ' + role + '\0').encode() in shell.read_bytes(), role
            shutil.copy2(shell, stage / 'bin')
            disk(tree, stage, out / (role + '.nhd'), work)


if __name__ == '__main__':
    main()
