#!/usr/bin/env python3
"""D35: validate the objects actually selected by ld, including archive members."""
import pathlib
import re
import subprocess
import sys
import tempfile
import os32x_hdr as H


def check_note(elf):
    s = elf.section('.os32_generations')
    expected = __import__('struct').pack('<4I', H.OS32X_HDR_VERSION, H.OS32_KAPI_ABI_GENERATION,
                                        H.OS32_MEMORY_LAYOUT_GENERATION, H.OS32_SHLIB_PROTOCOL)
    if not s or not s['size'] or s['size'] % len(expected):
        raise H.HeaderError(f'{elf.path}: missing per-unit generations; rebuild')
    body = elf.section_bytes(s)
    if any(body[i:i + len(expected)] != expected for i in range(0, len(body), len(expected))):
        raise H.HeaderError(f'{elf.path}: incompatible per-unit generations; rebuild')


def main():
    linker, *args = sys.argv[1:]
    if not any('app.ld' in a or 'app_sys.ld' in a or 'shlib.ld' in a or 'os32.ld' in a for a in args):
        return subprocess.call([linker, *args])
    out = pathlib.Path(args[args.index('-o') + 1])
    # The exact vendor inputs are recorded; a basename alone is not an exemption.
    exempt = set()
    with tempfile.TemporaryDirectory(prefix='os32-link-') as tmp:
        requested = next((a.split('=', 1)[1] for a in args if a.startswith('-Map=')), None)
        if '-Map' in args:
            requested = args[args.index('-Map') + 1]
        mapfile = pathlib.Path(requested) if requested else pathlib.Path(tmp) / 'link.map'
        map_args = [] if requested else ['-Map=' + str(mapfile)]
        r = subprocess.run([linker, *args, *map_args], text=True, capture_output=True)
        sys.stderr.write(r.stderr)
        if r.returncode:
            return r.returncode
        try:
            source = mapfile.read_text()
            selected = set(re.findall(r'(?m)^LOAD (.+)$', source))
            selected.update(re.findall(r'(?m)^([^\s]+\.a\([^\n)]+\))\s*(?:\n|\s)', source))
            for name in sorted(selected):
                match = re.fullmatch(r'(.+\.a)\((.+)\)', name)
                path = pathlib.Path(match[1] if match else name)
                if not path.exists() or (path.suffix == '.a' and not match) or (not match and path.suffix != '.o'):
                    continue
                # Compiler/newlib components are generation independent, not OS32 SDK code.
                vendor_root = pathlib.Path(__import__('os').environ.get('CROSS_DIR', '/home/hight/opt/cross')).resolve()
                if path.resolve().is_relative_to(vendor_root) and path.name in ('libc.a', 'libm.a', 'libgcc.a'):
                    exempt.add(str(path.resolve()))
                    continue
                if match:
                    member = pathlib.Path(tmp) / 'member.o'
                    member.write_bytes(subprocess.check_output(['i386-elf-ar', 'p', str(path), match[2]]))
                    check_note(H.Elf32(member))
                else:
                    check_note(H.Elf32(path))
            check_note(H.Elf32(out))
            # Evidence for wrapping and the build manifest, including used/exempt inputs.
            out.with_suffix('.inputs.json').write_text(__import__('json').dumps(
                {'selected': sorted(selected), 'vendor': sorted(exempt),
                 'elf_sha256': __import__('hashlib').sha256(out.read_bytes()).hexdigest(),
                 'generations': [H.OS32X_HDR_VERSION, H.OS32_KAPI_ABI_GENERATION, H.OS32_MEMORY_LAYOUT_GENERATION, H.OS32_SHLIB_PROTOCOL]}, indent=2) + '\n')
        except (H.HeaderError, subprocess.CalledProcessError) as e:
            out.unlink(missing_ok=True)
            print(f'link_guard: {e}', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
