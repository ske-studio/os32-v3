"""Freestanding WAV parser and runtime mutation checks."""
CHECK = dict(name='pcm-wav-host', tier=1, covers=[
    'userland/tests/pcm_wav.h', 'userland/tests/pcm_play.c',
    'tools/tests/pcm_wav_host.c', 'tools/tests/test_pcm_wav.py'], needs=[], budget=3)

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('w->format != 1', '0', 'format'),
    ('w->channels != 2', '0', 'channels'),
    ('w->rate != 44100', '0', 'rate'),
    ('w->align != 4', '0', 'alignment'),
    ('w->bits != 16', '0', 'bits'),
    ('padded > end - pos', 'padded > end - pos - 1', 'chunk bounds'),
    ('len + (len & 1u)', 'len', 'odd padding'),
    ('(len & 3u)', '(len & 0u)', 'partial frame'),
]

def main(mutate):
    with tempfile.TemporaryDirectory(prefix='pcm-wav-') as tmp:
        tree = Path(tmp)
        for name in ('userland/tests/pcm_wav.h', 'tools/tests/pcm_wav_host.c'):
            dest = tree / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, dest)
        header = tree / 'userland/tests/pcm_wav.h'
        original = header.read_text()
        def run():
            exe = tree / 'test'
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', str(tree / 'tools/tests/pcm_wav_host.c'),
                            '-o', str(exe)], check=True)
            return subprocess.run(['qemu-i386', str(exe)], timeout=10).returncode
        if run(): return 1
        if mutate:
            for anchor, replacement, label in MUTATIONS:
                if original.count(anchor) != 1: raise RuntimeError('nonunique anchor: ' + anchor)
                header.write_text(original.replace(anchor, replacement))
                if run() == 0:
                    print('MUTANT SURVIVED: ' + label, flush=True)
                    return 1
                print('MUTANT runtime RED: ' + label, flush=True)
            header.write_text(original)
    return 0

def run(ctx): return main(ctx.mutate)
if __name__ == '__main__': sys.exit(main('--mutate' in sys.argv))
