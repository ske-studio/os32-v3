"""List static caller files and their first evidence line for each requested tool."""
import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCOPES = ['Makefile', ':(glob)build/**/*.mk', 'tools', '.github/workflows',
          '.claude/skills', 'docs', ':!docs/archive', ':!docs/hw']


def callers(root, targets):
    tracked = subprocess.check_output(['git', 'ls-files', '-z', '--', *SCOPES], cwd=root)
    result = {target: {} for target in targets}
    patterns = {}
    for target in result:
        path = Path(target)
        if not (root / path).is_file():
            raise ValueError(f'unknown tool: {target}')
        # Accept full paths, or a local filename in the caller's directory.
        for name in (target, path.name):
            script = re.escape(name)
            launcher = r"(?:\bpython[23]?|\bsys\.executable|\$\(PYTHON\)|\bbash|\bsh)"
            separators = r"""['",\s]+"""
            options = r"(?:-[\w-]+" + separators + ")*"
            command = launcher + separators + options + r"(?:\./)?" + script
            direct = r"(?:^\s*[@+-]?|\brun:\s+)(?:\./)?" + script
            module = re.escape(path.stem if name == path.name else str(path.with_suffix('')).replace('/', '.'))
            imported = r"^\s*(?:from\s+[.\w]*\bimport\s+|(?:from|import)\s+)" + module
            patterns[target, name] = re.compile('(?:' + command + '|' + direct + r")(?=$|[\s,'\x22\x60;)])|" + imported + r"(?:\s|,|$)")

    for raw in tracked.split(b'\0'):
        if not raw:
            continue
        caller = raw.decode()
        source = root / caller
        if source.suffix not in {'.py', '.sh', '.mk', '.md', '.yml', '.yaml', '.ps1'} and caller != 'Makefile':
            continue
        contents = source.read_text(errors='replace')
        for (target, name), pattern in patterns.items():
            if caller == target or caller in result[target] or Path(target).stem not in contents:
                continue
            if name != target and source.parent != (root / target).parent:
                # Python tools also import modules from tools/ via sys.path.
                if source.suffix != '.py' or Path(target).parent != Path('tools'):
                    continue
            for line_no, line in enumerate(contents.splitlines(), 1):
                if line.lstrip().startswith('#'):
                    continue
                make_input = (caller == 'Makefile' or source.suffix == '.mk') and name == target and (
                    re.match(r'^[^#\s][^:=]*:(?!=)', line) and
                    name in line.partition(':')[2].partition('#')[0].split())
                if make_input or pattern.search(line):
                    result[target][caller] = line_no
                    break
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='+', help='repository-relative tool paths')
    args = parser.parse_args()
    for path, hits in callers(ROOT, args.paths).items():
        print(path, len(hits), *(f'{p}:{n}' for p, n in hits.items()), sep='\t')


if __name__ == '__main__':
    main()
