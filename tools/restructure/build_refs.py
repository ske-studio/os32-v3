"""Inventory build definitions and verify their coverage with GNU Make dry runs."""
import argparse
import csv
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ('powermac', 'kboot-core', 'kboot-link', 'kboot-shell', 'kboot-sh', 'kboot-link-sh')
TAGS = {
    'source': r'\.(?:c|asm|S|s|rs|cpp|h)\b|SOURCES?|SRCS?',
    'include-order': r'-I|INCLUDE|INC_KERNEL',
    'output': r'(?:^|\s)-o\s|BUILD_OUT|\b\w*OUT\b|\$@',
    'link-order': r'\b\w*(?:ORDER|OBJS?|OBJECTS|LINK_COMMAND)\w*\b|\$\(\w*LD\)|-Wl,|-elf-ld\b|\$\^',
    'path-expression': r'\$\((?:addprefix|addsuffix|patsubst|subst|shell|realpath|abspath|wildcard|foreach|basename|dir|notdir)\s|\$\([^()]+:[^()]+=|\)/',
}


def statements(lines):
    parts = []
    for number, line in enumerate(lines, 1):
        if not parts:
            start = number
        line = line.rstrip('\n')
        if line.endswith('\\'):
            parts.append(line[:-1].strip())
            continue
        value = ' '.join([*parts, line.strip()])
        parts.clear()
        if value and not value.startswith('#'):
            yield start, value


def raw_definitions(root):
    files = [root / 'Makefile']
    for pattern in ('build/**/*.mk', 'sdk/**/*.mk'):
        files.extend(sorted(root.glob(pattern)))
    for path in files:
        relative = path.relative_to(root).as_posix()
        yield relative, 0, 'file', ''
        for number, value in statements(path.read_text().splitlines()):
            tags = [tag for tag, pattern in TAGS.items() if re.search(pattern, value)]
            if re.match(r'^[^#=\s]+(?:\s+[^:=\s]+)*\s*:(?![=])', value):
                tags.append('dependency')
            yield relative, number, ','.join(tags) or 'definition', value


# Follow named inputs across included makefiles. Conditional and appended
# definitions are all candidates because this table spans every image.
ASSIGN = re.compile(r'^(?:export\s+)?([\w.-]+)\s*(?::=|\+=|\?=|=)\s*(.*)')
VARIABLE = re.compile(r'\$[({]([\w.-]+)(?=[:})])')
LINK = re.compile(r'(?:^|\s)(?:\$\([\w.-]*(?:LD|LINK|LINK_COMMAND)\)|(?:[\w./-]+-)?ld)(?:\s|$)|-Wl,')


def definitions(root):
    rows = list(raw_definitions(root))
    inputs = {}
    pending = []
    for _, _, _, value in rows:
        assignment = ASSIGN.match(value)
        if assignment:
            name, rhs = assignment.groups()
            inputs.setdefault(name, set()).update(VARIABLE.findall(rhs))
        elif LINK.search(value):
            pending.extend(VARIABLE.findall(value))

    linked = set()
    while pending:
        name = pending.pop()
        if name not in linked:
            linked.add(name)
            pending.extend(inputs.get(name, ()))

    for path, number, kind, value in rows:
        assignment = ASSIGN.match(value)
        if assignment and assignment[1] in linked and 'link-order' not in kind:
            kind = 'link-order' if kind == 'definition' else kind + ',link-order'
        yield path, number, kind, value


def observed(log):
    for line in log.splitlines():
        if line.startswith('MAKEFILE_LIST := '):
            yield from (p for p in line.split()[2:] if p.endswith('.mk'))


def write_table(path, columns, rows):
    with path.open('w', newline='') as out:
        writer = csv.writer(out, delimiter='\t', lineterminator='\n')
        writer.writerow(columns)
        writer.writerows(rows)


def main(out):
    refs = list(definitions(ROOT))
    indexed = {path for path, _, tag, _ in refs if tag == 'file'}
    columns = ('file', 'line', 'kind', 'make_text')
    write_table(out / 'build_refs.tsv', columns, refs)
    write_table(out / 'link_sources.tsv', columns, (r for r in refs if 'link-order' in r[2]))
    coverage, includes, links = [], [], []
    for image in ('pc98', *IMAGES):
        args = ['all'] if image == 'pc98' else ['ARCH=ppc', 'PLATFORM=powermac', 'IMAGE=' + image]
        if image != 'pc98':
            # -n skips the mkdir recipe but GNU Make still executes the
            # recursive make under flock. Ask the active configuration for its directory.
            folder = subprocess.check_output(
                ['make', '-s', *args, '--eval=p0-body-dir: ; @echo "$(KB_BODY_OUT)"',
                 'p0-body-dir'], cwd=ROOT, text=True).strip()
            if folder:
                (ROOT / folder).mkdir(parents=True, exist_ok=True)
        log = subprocess.check_output(['make', '-j1', '-Bnp', '--trace', *args], cwd=ROOT,
                                      env=dict(os.environ, LC_ALL='C'), text=True)
        files = sorted(set(observed(log)))
        if not files:
            raise ValueError(f'{image}: MAKEFILE_LIST missing')
        coverage.extend((image, path, int(path in indexed)) for path in files)
        location = None
        database = False
        for line in log.splitlines():
            if line.startswith('# Make data base'):
                database = True
            if line.startswith('# Finished Make data base'):
                database = False
                location = None
            if database:
                continue
            trace = re.match(r"^(.*?):(\d+): update target '([^']+)'", line)
            if trace:
                location = trace.groups()
            elif location:
                flags = re.findall(r'(?:^|\s)-I\s*([^\s]+)', line)
                if flags:
                    includes.append((image, *location, ' '.join(flags)))
                if re.search(r'(?:^|[/\s])(?:[\w-]+-)?ld(?:\s|$)|-Wl,', line):
                    links.append((image, *location, line))
    write_table(out / 'read_mk.tsv', ('image', 'file', 'covered'), coverage)
    cmd_cols = ('image', 'recipe_file', 'line', 'target')
    write_table(out / 'include_order.tsv', (*cmd_cols, 'directories_in_order'), includes)
    write_table(out / 'link_order.tsv', (*cmd_cols, 'expanded_command'), links)
    missing = sum(1 for _, _, covered in coverage if not covered)
    print(f'dry runs={1 + len(IMAGES)}, .mk reads={len(coverage)}, missing={missing}')
    return bool(missing)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_directory', type=Path)
    raise SystemExit(main(parser.parse_args().output_directory))
