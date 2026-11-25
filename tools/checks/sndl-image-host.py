"""Execute both shipped images at their linked addresses in an ILP32 runner."""
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='sndl-image-host', tier=1, needs=['make:all'], budget=3,
             covers=['tools/tests/sndl_image_host.c', 'tools/tests/host32.py',
                     'userland/tests/sndl_test.c', 'sdk/sndl/stub.c', 'sdk/include/os32/*.h',
                     'userland/lib/snd/**', 'sdk/mkos32x.py', 'sdk/link/shlib.ld',
                     'tools/accept/snd2/verify_soft.py', 'tools/accept/snd2/verify_wave.py',
                     'tools/accept/snd2/soft_tone.cpp', 'lib/os32_opna/upstream/**',
                     'build/os32snd.mk', 'sdk/os32x_hdr.py'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §7 x86 アプリからの読み込み'])


def run(ctx):
    import sys
    root = Path(ctx.root)
    sys.path.insert(0, str(root / 'sdk'))
    import os32x_hdr as h
    sys.path.insert(0, str(root / 'tools/tests'))
    import host32

    sound = (root / 'userland/libos32snd.shlib').read_bytes()
    app = root / 'userland/tests/sndl_test.bin'
    header = h.parse_header(app.read_bytes())
    assert header['flags'] == h.OS32X_FLAG_SND | h.OS32X_FLAG_RING3
    assert header['shlib_protocol'] == h.OS32_SHLIB_PROTOCOL
    assert h.parse_header(sound)['load_addr'] == 0x80080000
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        blob = work / 'sound.bin'
        asm = work / 'images.S'
        asm.write_text(f'.section .rodata\n.balign 4\n.global sound_blob, app_blob\n'
                       f'sound_blob: .incbin "{blob}"\n.balign 4\napp_blob: .incbin "{app}"\n'
                       '.section .note.GNU-stack,"",@progbits\n')
        sys.path.insert(0, str(root / 'tools/accept/snd2'))
        import verify_soft
        import struct
        for mode in ['offline', 'soft'] + (['bad-version'] if ctx.mutate else []):
            bad_version = mode == 'bad-version'
            raw = bytearray(sound)
            if bad_version:
                offset = h.parse_header(raw)['header_size'] + 4
                raw[offset:offset + 4] = (2).to_bytes(4, 'little')
            blob.write_bytes(raw)
            exe = work / 'guest'
            subprocess.run(['gcc', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
                            '-fno-stack-protector', '-static', '-no-pie', '-nostdlib',
                            '-Wall', '-Wextra', '-Werror', '-O2', '-I', str(root / 'sdk/include/os32'),
                            *(['-DTEST_SOFT'] if mode == 'soft' else []),
                            str(root / 'tools/tests/sndl_image_host.c'), str(asm), '-o', str(exe)],
                           check=True)
            env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
            result = host32.run([str(exe)], env=env, timeout=30, stdout=subprocess.PIPE)
            if mode == 'soft' and result.returncode == 0:
                verify_soft.verify(struct.iter_unpack('<hh', result.stdout))
            if result.returncode != int(bad_version):
                return 1
            if bad_version:
                print('RED (runtime): incompatible sound table')
    return 0
