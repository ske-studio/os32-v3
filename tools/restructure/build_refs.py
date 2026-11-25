"""Inventory build definitions and verify their coverage with GNU Make dry runs."""
import argparse
import bisect
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


# GNU Make prints each recursive invocation's database separately. Keep its
# variables, target-specific overrides, and rules in the same namespace.
ORIGIN = re.compile(r"^# (?:makefile|override) \(from '(.+)', line (\d+)\)")
RECIPE = re.compile(r"^#  recipe to execute \(from '(.+)', line (\d+)\):")
ASSIGN = re.compile(r'^(?:(\S+):\s+)?([\w.-]+)\s*(?:::=|:=|\+=|\?=|=)\s*(.*)')
VARIABLE = re.compile(r'\$[({]([\w.-]+)(?=[:})])')
LINK = re.compile(r'(?:^|[\s/@])(?:\$\([\w.-]*(?:LD|LINK|LINK_COMMAND)\)|(?:[\w.-]+-)?ld)(?:\s|$)|-Wl,')
SOURCE = re.compile(r'\S+\.(?:c|cc|cpp|s|S|asm|rs)$')


def assignment_history(refs):
    history = {}
    for path, line, _, value in refs:
        match = re.fullmatch(r'([\w.-]+)\s*(:=|=|\+=)\s*(.*)', value)
        if match:
            name, operator, rhs = match.groups()
            history.setdefault((path, name), []).append((line, operator, rhs))
    return history


def input_origins(origin, name, rhs, variables, history):
    words = rhs.split()
    path, end = origin
    # Work backwards from Make's reported assignment. A literal or simple
    # variable append must match the suffix of the recorded value exactly.
    entries = [entry for entry in history.get((path, name), ()) if entry[0] <= end]
    if entries and entries[-1][1] == '+=':
        for line, operator, value in reversed(entries):
            if operator != '+=':
                origin = (path, line)
                break
            expanded = re.sub(r'\$\(([\w.-]+)\)', lambda m: variables.get(
                (None, m[1]), (None, m[0]))[1], value)
            suffix = expanded.split()
            if not suffix or words[-len(suffix):] != suffix:
                break
            yield from ((word, (path, line)) for word in suffix)
            del words[-len(suffix):]
    yield from ((word, origin) for word in words)


def database_sources(block, expanded_targets, history):
    variables = {}
    prerequisites = {}
    recipes = []
    target = None
    origin = recipe = None
    for line in block.splitlines():
        if origin:
            match = ASSIGN.match(line)
            if match:
                scope, name, rhs = match.groups()
                variables[scope, name] = origin, rhs
            origin = None
        if match := ORIGIN.match(line):
            origin = match[1], int(match[2])
        elif match := RECIPE.match(line):
            recipe = match[1], int(match[2])
        elif line.startswith('\t') and recipe and target:
            recipes.append((target, recipe, line.strip()))
        else:
            recipe = None
            if not line:
                target = None
            elif match := re.match(r'^([^#\s:=]+)::?\s*(.*)$', line):
                target = match[1]
                if not ASSIGN.match(line):
                    prerequisites[target] = match[2].split('|')[0].split()

    # Immediate assignments and computed variable names lose their expansion
    # history in -p. Index literal inputs by word, then join them to the
    # link rule's actual prerequisites (including the sources of its objects).
    owners = {}
    for (scope, name), (origin, rhs) in variables.items():
        if scope is None:
            for word, supplier in input_origins(origin, name, rhs, variables, history):
                if SOURCE.fullmatch(word) or word.endswith(('.a', '.o', '.ld')):
                    owners.setdefault(word, set()).add((supplier, name))

    for target, recipe, command in recipes:
        if (target, *recipe) not in expanded_targets and not LINK.search(command):
            continue
        yield recipe, target, 'recipe', command
        pending = VARIABLE.findall(command)
        visited = set()
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            for scope in (None, target):
                entry = variables.get((scope, name))
                if entry:
                    origin, rhs = entry
                    yield origin, target, 'variable', name
                    pending.extend(VARIABLE.findall(rhs))
        for position, word in enumerate(prerequisites.get(target, ()), 1):
            inputs = [word]
            if word.endswith('.o'):
                inputs.extend(src for src in prerequisites.get(word, ()) if SOURCE.fullmatch(src))
            for item in inputs:
                for origin, name in owners.get(item, ()):
                    yield origin, target, 'prerequisite-word', f'{position} {word} <- {name} {item}'


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
    refs = list(raw_definitions(ROOT))
    history = assignment_history(refs)
    indexed = {path for path, _, tag, _ in refs if tag == 'file'}
    columns = ('file', 'line', 'kind', 'make_text')
    evidence = set()
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
        targets = {(target, path, int(number)) for config, path, number, target, _ in links
                   if config == image}
        for block in re.findall(r'^# Make data base.*?^# Finished Make data base', log, re.M | re.S):
            for (path, number), target, reason, detail in database_sources(block, targets, history):
                if path in indexed:
                    evidence.add((path, number, image, target, reason, detail))
    # GNU Make reports the end line of some continued statements.
    starts = {}
    for path, number, _, _ in refs:
        starts.setdefault(path, []).append(number)
    linked = set()
    for path, number, *_ in evidence:
        lines = starts[path]
        linked.add((path, lines[bisect.bisect_right(lines, number) - 1]))
    differences = []
    sources = []
    for row in refs:
        path, number, kind, value = row
        db = (path, number) in linked
        lexical = 'link-order' in kind
        if db:
            sources.append(row)
        if lexical != db:
            differences.append((*row, int(db)))
    write_table(out / 'build_refs.tsv', columns, refs)
    write_table(out / 'link_sources.tsv', columns, sources)
    write_table(out / 'link_evidence.tsv', ('file', 'db_line', 'image', 'target', 'basis', 'word_or_recipe'), sorted(evidence))
    write_table(out / 'link_differences.tsv', (*columns, 'database_match'), differences)
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
