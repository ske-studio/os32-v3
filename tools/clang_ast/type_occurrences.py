"""Visit every written AST type with the Clang API omitted by libclang."""
import functools
import hashlib
import os
import pathlib
import subprocess
import tempfile
from . import ParseError, resource_dir


@functools.lru_cache(maxsize=1)
def visitor_binary():
    source = pathlib.Path(__file__).with_suffix('.cpp')
    llvm = pathlib.Path(resource_dir()).parents[2]
    key = hashlib.sha256(source.read_bytes() + str(llvm).encode()).hexdigest()
    directory = pathlib.Path(tempfile.gettempdir()) / ('os32-type-visitors-' + str(os.getuid()))
    directory.mkdir(mode=0o700, exist_ok=True)
    binary = directory / key
    if binary.is_file():
        return binary
    try:
        cpp = next((llvm / 'lib').glob('libclang-cpp.so*'))
        shared = next((llvm / 'lib').glob('libLLVM.so*'))
        with tempfile.TemporaryDirectory(dir=directory) as staging:
            output = pathlib.Path(staging) / 'visitor'
            result = subprocess.run(['clang++', '-std=c++17', '-O0',
                '-I' + str(llvm / 'include'), str(source), str(cpp), str(shared),
                '-Wl,-rpath,' + str(llvm / 'lib'), '-o', str(output)],
                capture_output=True, text=True, stdin=subprocess.DEVNULL)
            if result.returncode:
                raise ParseError('Clang type visitor compilation failed: ' + result.stderr)
            output.replace(binary)
    except (OSError, StopIteration) as error:
        raise ParseError('Clang type visitor toolchain unavailable: ' + str(error)) from error
    return binary


def findings(tu, root):
    root = pathlib.Path(root).absolute()
    source, args, text = tu.os32_parse_input
    result = subprocess.run([str(visitor_binary()), source, *args],
        input=text, cwd=root, capture_output=True, text=True)
    if result.returncode:
        raise ParseError('Clang type visitor parse failed: ' + result.stderr)
    hits = set()
    for record in result.stdout.splitlines():
        file, line, feature = record.split('\t')
        try:
            rel = pathlib.Path(os.path.abspath(root / file)).relative_to(root).as_posix()
        except ValueError:
            continue
        hits.add((rel, int(line), feature))
    return hits
