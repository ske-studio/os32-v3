"""P0-1: tracked paths, check_layers owners and expanded kernel source sets."""
import argparse
import os
from collections import defaultdict
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_layers import owner

SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.s', '.asm', '.rs'}
# Capture leaf assignments, before select.mk combines them or applies link order.
LEAF = re.compile(r'^(C_COMMON_\w+|(?:C|GAS)_ARCH(?:_\w+)?|ASM_ARCH|'
                  r'C_PLATFORM(?:_\w+)?|(?:C|ASM)_FEATURES|SQLITE_HARD_C|'
                  r'PC98_USB_BODY_SOURCES|DIAGNOSTIC_(?!FLAG_)\w+)\s*([:+?]?=)\s*(.*)$')


def instrument(path, directory, labels):
    result = []
    guards = []
    for line in path.read_text().replace('\\\n', ' ').splitlines():
        if line.startswith(('ifeq ', 'ifneq ', 'ifdef ', 'ifndef ')):
            guard = re.fullmatch(r'ifeq \(\$\(FEATURE_(\w+)\),1\)', line)
            guards.append(guard[1].lower() if guard else '')
        elif line.startswith('else') and guards[-1]:
            guards[-1] = 'no-' + guards[-1]
        elif line == 'endif':
            guards.pop()
        # Redirect only instrumented includes. Other build inputs stay in the worktree.
        result.append(re.sub(r'^include build/(features|platform|arch|sources)/',
                             rf'include {directory}/\1/', line))
        match = LEAF.match(line)
        if not match:
            continue
        variable, operator, value = match.groups()
        if variable in {'C_ARCH_NEW', 'C_COMMON_EXEC_HOOKS'} or '$(call image_owned,' in value:
            continue
        feature = ''
        if path.parent.name == 'features':
            feature = path.stem.removesuffix('-static')
        elif variable.endswith('_FEATURES'):
            # Inline feature implementations in select.mk.
            feature = next((guard for guard in reversed(guards) if guard), '')
        else:
            group = re.fullmatch(r'(?:C_COMMON|C_PLATFORM|C_ARCH|GAS_ARCH)_(\w+)', variable)
            if group and group[1] in {'CLI', 'EXEC', 'IDE', 'LINK', 'LOOP', 'USB', 'USERSHELL'}:
                feature = group[1].lower()
        label = (path.relative_to(ROOT).as_posix(), variable, feature)
        key = labels.setdefault(label, f'P0_SET_{len(labels)}')
        result.append(f'{key} {operator} {value}')
    target = directory / path.relative_to(ROOT / 'build')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('\n'.join(result) + '\n')


def make_output(args):
    # A parent check can carry -j, command-line feature flags or an alternate locale.
    env = {k: os.environ[k] for k in ('PATH', 'HOME', 'TMPDIR', 'CROSS_DIR') if k in os.environ}
    return subprocess.check_output(['make', '-srR', '--no-print-directory', *args],
                                  cwd=ROOT, env=dict(env, LC_ALL='C'), text=True)


def collect_links(directory, image_name, images, groups, features):
    """Resolve regular and static pattern rules using make's dry-run file database."""
    out = directory / image_name
    pc98 = image_name == 'pc98'
    query = directory / 'links.mk'
    query.write_text("""include build/config.mk
include build/targets/$(ARCH)-$(PLATFORM).mk
$(info P0_VFS $(KB_VFS))
p0_pairs = $(KERNEL_PAIRED_MODULES) $(KB_ROOT_FEP) $(KB_ROOT_DIAG)
$(info P0_PAIRS $(p0_pairs))
$(foreach v,$(filter %_OBJS %_OBJECTS %_OBJ,$(.VARIABLES)),$(if $(filter-out ALL_TARGET_OBJECTS,$(v)),$(info P0_VAR $(v) $($(v)))))
p0-links: $(if $(filter x86,$(ARCH)),$(BUILD_OUT)/kernel.elf) $(p0_pairs)
""")
    database = make_output(['-np', '-f', str(query), f'BUILD_OUT={out}', f'KB_OUT={out}', f'KB_BODY_OUT={out}',
                            f'IMAGE={image_name}', 'ARCH=' + ('x86' if pc98 else 'ppc'),
                            'PLATFORM=' + ('pc98' if pc98 else 'powermac'), 'p0-links'])
    prerequisites = {}
    pairs = []
    variables = defaultdict(set)
    for line in database.splitlines():
        if line.startswith('P0_VFS '):
            vfs = line.split()[1:]
        elif line.startswith('P0_PAIRS '):
            pairs = line.split()[1:]
        elif line.startswith('P0_VAR '):
            _, variable, *objects = line.split()
            for obj in objects:
                variables[obj].add(variable)
        elif ': ' in line and not line.startswith(('#', ' ', '\t')):
            target, deps = line.split(': ', 1)
            if ' ' not in target and '=' not in deps:
                prerequisites[target] = deps.split(' | ')[0].split()
    for stem in vfs:
        for source in prerequisites[stem + '.c']:
            if Path(source).name == Path(stem + '.c').name:
                groups[source].add('build/targets/ppc-powermac.mk:KB_VFS')
                images[source].add(image_name)
                features[source].add('sqlite')
    links = {str(out / 'kernel.elf'): 'pc98'} if pc98 else {}
    for pair in pairs:
        elf, = [dep for dep in prerequisites[pair] if dep.endswith('.elf')]
        links[elf] = image_name + '/' + Path(pair).name
    for elf, image in links.items():
        for obj in prerequisites[elf]:
            if not obj.endswith('.o'):
                continue
            # The input of each compile rule precedes its generated headers.
            deps = prerequisites[obj]
            source = next((dep for dep in deps if Path(dep).suffix.lower() in SUFFIXES), None)
            if not source:
                continue
            if Path(source).is_absolute():
                # Generated SQLite VFS translation unit has a tracked template of the same name.
                source = next((dep for dep in prerequisites.get(source, ())
                               if Path(dep).name == Path(source).name), source)
            images[source].add(image)
            groups[source].update(variables[obj])
            if image != image_name:
                features[source].add(Path(image).stem)


# Negative results are meaningful only for the kernel tree searched by select.mk.
KERNEL_ROOTS = set('arch platform drivers hwdev kdebug init kernel mm exec fs gfx net input '
                   'kapi lib include sound'.split())


def read_graph(database):
    """Read concrete rules and the origins of source/object list variables."""
    definitions = database.split('# Variables\n', 1)[1].split('# Pattern-specific', 1)[0]
    names = {}
    origin = ''
    for line in definitions.splitlines():
        location = re.fullmatch(r"# makefile \(from '([^']+)', line \d+\)", line)
        if line.startswith('#'):
            origin = location[1] if location else ''
        elif origin.startswith('build/') and origin.endswith('.mk'):
            assignment = re.fullmatch(r'(\w+) [:?+]?= (.*)', line)
            if assignment:
                name, value = assignment.groups()
                if (re.search(r'\.(?:c|cpp|S|s|asm|rs|o|a)(?:\s|$|\))', value)
                        and not any(token in value for token in ('$(shell', '$@', '$<', '%', ' -'))):
                    names[name] = origin
            origin = ''
    rules = {}
    files = database.split('# Files\n', 1)[1]
    for line in files.splitlines():
        rule = re.fullmatch(r'([^\s:#=]+):(?: (.*))?', line)
        if rule and '=' not in (rule[2] or ''):
            rules[rule[1]] = (rule[2] or '').partition('|')[0].split()
    return rules, names


def collect_graph(directory, profile, args, goal, images, groups):
    database = make_output(['-np', *args, goal])
    rules, names = read_graph(database)
    # Expand only list-like assignments. The file database supplies their defining
    # makefile, including variables evaluated lazily with wildcard or patsubst.
    query = directory / 'lists.mk'
    query.write_text(''.join(f'$(info P0_LIST {v} $({v}))\n' for v in names))
    values = defaultdict(set)
    for line in make_output(['-n', *args, '-f', str(query), '--eval=p0-list: ; @:', 'p0-list']).splitlines():
        if line.startswith('P0_LIST '):
            _, name, *items = line.split()
            for item in items:
                values[item].add(f'{names[name]}:{name}')

    for target in rules:
        if not target.lower().endswith('.elf'):
            continue
        image = profile + '/' + target
        pending = list(rules[target])
        seen = set()
        while pending:
            item = pending.pop()
            if item in seen:
                continue
            seen.add(item)
            suffix = Path(item).suffix
            if suffix.lower() in SUFFIXES:
                images[item].add(image)
                groups[item].update(values[item])
            elif suffix == '.o':
                inputs = [dep for dep in rules.get(item, ()) if Path(dep).suffix.lower() in SUFFIXES]
                if inputs:
                    source = inputs[0]
                    groups[source].update(values[item])
                    pending.append(source)
            elif suffix == '.a':
                pending.extend(rules.get(item, ()))
    # Recognize source sets even when a translation unit has no ELF link rule.
    for item, labels in values.items():
        if Path(item).suffix.lower() in SUFFIXES:
            groups[item].update(labels)


def inventory():
    images, groups, features = (defaultdict(set) for _ in range(3))
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        graph = directory / 'graph.mk'
        graph.write_text("""include build/config.mk
ifeq ($(ARCH),x86)
BUILD_ID_SRC = $(BUILD_OUT)/build_id.c
include build/build-id.mk
endif
include build/targets/$(ARCH)-$(PLATFORM).mk
include build/abi_contract.mk
p0-graph: ; @:
""")
        collect_graph(directory, 'ppc-bootstrap', ['-f', 'build/ppc.mk'], 'all', images, groups)
        labels = {}
        for pattern in ('select.mk', 'sources/*.mk', 'platform/*.mk', 'arch/*.mk', 'features/*.mk'):
            for path in sorted((ROOT / 'build').glob(pattern)):
                instrument(path, directory, labels)
        # config.mk supplies the same toolchain and helper variables as the normal entry point.
        config = (ROOT / 'build/config.mk').read_text().replace(
            'include build/select.mk', f'include {directory}/select.mk')
        query = directory / 'query.mk'
        query.write_text(config + '''
ifeq ($(ARCH),ppc)
include build/targets/ppc-powermac.mk
endif
p0_sources = $(C_KERNEL) $(ASM_KERNEL) $(GAS_KERNEL) $(SQLITE_HARD_C)
p0_restore = $(patsubst $(KB_BODY_OUT)/%.o,%.S,$(filter %/sqlite_restore.o,$(KB_HARD)))
p0-extra:
\t@:
''' + '\n'.join(f'$(info {key} $({key}))' for key in ['IMAGE', 'KB_ROOT_FEP', 'KB_ROOT_DIAG', 'p0_restore', 'p0_sources', *labels.values()]))
        for path in sorted((ROOT / 'build/images').glob('*.mk')):
            if path.name == 'ppc-flags.mk':
                continue
            pc98 = path.stem == 'pc98'
            command = ['-f', str(query), 'p0-extra',
                       'ARCH=' + ('x86' if pc98 else 'ppc'), 'PLATFORM=' + ('pc98' if pc98 else 'powermac'),
                       f'IMAGE={path.stem}', f'BUILD_OUT={directory}/out', f'KB_OUT={directory}/kb']
            graph_args = ['-f', str(graph), *command[3:6]]
            collect_graph(directory, path.stem, graph_args, 'programs' if pc98 else 'p0-graph',
                          images, groups)
            if pc98:
                for (filename, variable, feature), key in labels.items():
                    if not variable.startswith('DIAGNOSTIC_'):
                        continue
                    flag = variable.removeprefix('DIAGNOSTIC_')
                    diagnostic = make_output([*command, flag + '=1'])
                    for line in diagnostic.splitlines():
                        name, *sources = line.split()
                        if name == key:
                            for source in sources:
                                groups[source].add(f'{filename}:{variable}')
                                images[source].add('pc98+' + flag)
                                if feature:
                                    features[source].add(feature)
            lines = make_output(command).splitlines()
            values = {words[0]: words[1:] for line in lines if (words := line.split())}
            effective, = values['IMAGE']
            if effective == path.stem and (pc98 or values['KB_ROOT_FEP'] or values['KB_ROOT_DIAG']):
                collect_links(directory, effective, images, groups, features)
            for source in values['p0_sources']:
                images[source].add(effective)
            for (filename, variable, feature), key in labels.items():
                for source in values[key]:
                    groups[source].add(f'{filename}:{variable}')
                    if feature:
                        features[source].add(feature)
            for source in values['p0_restore']:
                groups[source].add('build/targets/ppc-powermac.mk:KB_HARD')
                images[source].add(effective)
                features[source].add('sqlite')
    return images, groups, features


def rows(entries, images, groups, features):
    for path, mode in entries:
        suffix = Path(path).suffix.lower()
        kind = ('gitlink' if mode == '160000' else 'symlink' if mode == '120000'
                else 'source' if suffix in SUFFIXES else suffix.lstrip('.') or 'file')
        layer = '/'.join(filter(None, owner(path)))
        image = ','.join(sorted(images.get(path, ())))
        group = ','.join(sorted(groups.get(path, ())))
        evidence = layer if layer != 'common' else group or image
        status = ''
        if kind == 'source':
            if group:
                status = 'member'
            elif path.split('/')[0] in KERNEL_ROOTS or image:
                status = 'no-set'
            else:
                status = 'out-of-scope'
        yield (path, kind, evidence, image, group, layer, status), sorted(features.get(path, ()))


def tables_for(entries, membership):
    tables = {name: [] for name in ('owners', 'owner-empty', 'no-set', 'out-of-scope', 'multi-feature')}
    for row, features in rows(entries, *membership):
        tables['owners'].append(row)
        if not row[2]:
            tables['owner-empty'].append(row)
        if row[6] in ('no-set', 'out-of-scope'):
            tables[row[6]].append(row)
        if len(features) >= 2:
            tables['multi-feature'].append((*row, ','.join(features)))
    return tables


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    out = parser.parse_args().output
    out.mkdir(exist_ok=True, parents=True)
    raw = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=ROOT)
    entries = []
    for record in raw.decode().split('\0')[:-1]:
        meta, path = record.split('\t', 1)
        if meta.split()[2] != '0' or any(c in path for c in '\t\r\n'):
            raise ValueError(f'unrepresentable or unmerged index entry: {path!r}')
        entries.append((path, meta.split()[0]))
    tables = tables_for(entries, inventory())
    for name, records in tables.items():
        (out / (name + '.tsv')).write_text(''.join('\t'.join(row) + '\n' for row in records))
        print(name, len(records))


if __name__ == '__main__':
    main()
