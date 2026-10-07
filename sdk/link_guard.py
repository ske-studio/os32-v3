#!/usr/bin/env python3
"""D35: validate the objects actually selected by ld, including archive members."""
import pathlib
import os
import shutil
import importlib.util
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


def vendor_inputs(args):
    """Resolve compiler/newlib -l inputs from the same ordered -L search as ld."""
    dirs, libraries = [], []
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == '-L':
            i += 1
            dirs.append(pathlib.Path(args[i]))
        elif arg.startswith('-L'):
            dirs.append(pathlib.Path(arg[2:]))
        elif arg == '-l':
            i += 1
            libraries.append(args[i])
        elif arg.startswith('-l'):
            libraries.append(arg[2:])
        i += 1
    resolved = set()
    for lib in libraries:
        if lib not in ('c', 'm', 'gcc'):
            continue
        for directory in dirs:
            path = directory / ('lib' + lib + '.a')
            if path.is_file():
                resolved.add(path.resolve())
                break
    return resolved


def main():
    linker, *args = sys.argv[1:]
    if not any('app.ld' in a or 'app_sys.ld' in a or 'shlib.ld' in a or 'os32.ld' in a for a in args):
        return subprocess.call([linker, *args])
    out = pathlib.Path(args[args.index('-o') + 1])
    # The exact vendor inputs are recorded; a basename alone is not an exemption.
    exempt = set()
    vendor = vendor_inputs(args)
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
            nano_vendor = set()
            if any(pathlib.Path(a).name == 'app.ld' for a in args):
                module = pathlib.Path(__file__).parent / 'allocator/check_link.py'
                if not module.exists():
                    module = pathlib.Path(__file__).parent / 'nano_check_link.py'
                spec = importlib.util.spec_from_file_location('nano_check_link', module)
                gate = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(gate)
                # Resolve the receipt from the actual selected archive, never a
                # guessed object basename. SDK staging keeps it beside the lib.
                archives = [pathlib.Path(n[5:]) for n in mapfile.read_text().splitlines()
                            if n.startswith('LOAD ') and pathlib.Path(n[5:]).name == 'libos32nano.a']
                if len(archives) > 1:
                    raise H.HeaderError('USER link has duplicate libos32nano.a inputs')
                cross = pathlib.Path(os.environ.get('CROSS_DIR') or pathlib.Path(shutil.which(linker)).resolve().parent.parent)
                nano_vendor = gate.validate_output(cross, mapfile, out, archives[0].with_suffix('.json') if archives else None)
            source = mapfile.read_text()
            selected = set(re.findall(r'(?m)^LOAD (.+)$', source))
            selected.update(re.findall(r'(?m)^([^\s]+\.a\([^\n)]+\))\s*(?:\n|\s)', source))
            for name in sorted(selected):
                match = re.fullmatch(r'(.+\.a)\((.+)\)', name)
                path = pathlib.Path(match[1] if match else name)
                if not path.exists() or (path.suffix == '.a' and not match) or (not match and path.suffix != '.o'):
                    continue
                # Compiler/newlib components are generation independent, not OS32 SDK code.
                if path.resolve() in vendor or name in nano_vendor:
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
        except (H.HeaderError, ValueError, KeyError, OSError, subprocess.CalledProcessError) as e:
            out.unlink(missing_ok=True)
            print(f'link_guard: {e}', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
