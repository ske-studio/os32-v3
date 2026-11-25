"""Inspect local build products. Attribute object records only with independent TU evidence."""
import argparse
from functools import lru_cache
from collections import defaultdict
from pathlib import Path
import json
import os
import re
import subprocess

from path_refs import SOURCE, table

UNKNOWN = '未確定'


def compile_units(log):
    units = defaultdict(set)
    for line in log.splitlines():
        args = line.split()
        if '-o' not in args or not ('-c' in args or 'nasm' in args):
            continue
        index = args.index('-o')
        if index + 1 < len(args):
            for arg in args:
                if Path(arg).suffix in SOURCE and Path(arg).is_file():
                    units[args[index + 1]].add(arg)
    return units


def resolve(value, candidates, sources):
    normalized = os.path.relpath(value) if os.path.isabs(value) else os.path.normpath(value)
    if normalized in sources:
        return normalized, 'source path'
    matches = {p for p in candidates if Path(p).name == Path(value).name}
    if len(matches) == 1:
        return matches.pop(), 'object compile input + filename'
    return UNKNOWN, 'no unique compile input'


@lru_cache(None)
def macro_lines(source):
    path = Path(source)
    if source == UNKNOWN or not path.is_file() or source.startswith(('docs/archive/', 'docs/hw/')):
        return ''
    return ','.join(str(i) for i, line in enumerate(path.read_text(errors='replace').splitlines(), 1)
                    if re.search(r'\b__FILE__\b', line))


def sections(path):
    listing = subprocess.check_output(['readelf', '-WS', path], text=True)
    for name, offset, size in re.findall(
            r'^\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)',
            listing, re.M):
        yield name, int(offset, 16), int(size, 16)


def inventory(out):
    sources = set(subprocess.check_output(['git', 'ls-files', '-z']).decode().strip('\0').split('\0'))
    build_logs = [out / 'build.log']
    if (out / 'build-ppc.log').is_file():
        build_logs.append(out / 'build-ppc.log')
    units = defaultdict(set)
    provenance = defaultdict(set)
    for log in build_logs:
        for product, inputs in compile_units(log.read_text()).items():
            units[product].update(inputs)
            provenance[product].update(f'{log.name}: {source}' for source in inputs)
    sources.update(p for values in units.values() for p in values)
    deploy = json.loads(Path('build/out/deploy-set.json').read_text())
    products = {Path(entry['host']) for entry in deploy['files'] if 'host' in entry}
    for root in ('build/out', 'userland', 'boot', 'sdk', 'lib'):
        products.update(p for p in Path(root).rglob('*') if p.suffix.lower() in
                        ('.elf', '.o', '.bin', '.mod', '.osx', '.iso', '.img', '.lz4', '.a', '.rlib'))
    rows = []
    coverage = []
    for path in sorted(products):
        if not path.is_file():
            continue
        data = path.read_bytes()
        candidates = units[str(path)].copy()
        origins = provenance[str(path)].copy()
        dep = path.with_suffix('.d')
        if dep.is_file() and ':' in (deps := dep.read_text(errors='replace')):
            rhs = deps.split(':', 1)[1].replace('\\\n', ' ').split()
            if rhs and Path(rhs[0]).suffix in SOURCE and Path(rhs[0]).is_file():
                candidates.add(rhs[0])
                origins.add(f'{dep}: {rhs[0]}')
        origin = ' | '.join(sorted(origins)) or UNKNOWN
        records = set()
        elf = data.startswith(b'\x7fELF')
        archive = data.startswith(b'!<arch>\n')
        symbol_status = 'no ELF symbol table'
        if elf or archive:
            result = subprocess.run(['readelf', '-Ws', path], text=True, errors='replace', capture_output=True)
            diagnostics = set(result.stderr.splitlines())
            if result.returncode and not (archive and diagnostics and
                    all('LLVM bitcode file' in line for line in diagnostics)):
                raise ValueError(f'{path}: {result.stderr}')
            symbol_status = 'LLVM bitcode member 未確定' if result.returncode else 'readelf -Ws'
            for line in result.stdout.splitlines():
                fields = line.split(None, 7)
                if len(fields) == 8 and fields[3] == 'FILE' and fields[0].endswith(':'):
                    records.add(('STT_FILE', fields[7]))
        if elf:
            if b'.debug_line\0' in data:
                lines = subprocess.check_output(['readelf', '-W', '--debug-dump=decodedline', path],
                                                text=True, errors='replace')
                records.update(('DWARF line CU', v) for v in re.findall(r'^CU: (.+):$', lines, re.M))
                records.update(('DWARF line file', v) for v in re.findall(
                    r'^([^\s].*?)\s+(?:\d+|-)\s+(?:0x[0-9a-f]+|0)\b', lines, re.M))
            if b'.debug_info\0' in data:
                info = subprocess.check_output(['readelf', '--debug-dump=info', path], text=True, errors='replace')
                for block in re.split(r'(?=^\s*<\d+><)', info, flags=re.M):
                    if 'DW_TAG_compile_unit' in block:
                        for label, value in re.findall(r'DW_AT_(name|comp_dir)\s*:\s*(.*)', block):
                            records.add(('DW_AT_' + label, value.split('): ')[-1].strip()))
        # Physical bytes are evidence of a spelling, not proof of macro expansion.
        section_ranges = None
        for m in re.finditer(rb'(?<![A-Za-z0-9_.+/-])[A-Za-z0-9_.+/-]+\.(?:c|h|cpp|asm|inc|rs|S|s)\x00', data):
            value = m[0][:-1].decode()
            if '/' in value:
                records.add(('path bytes', value))
                if elf:
                    if section_ranges is None:
                        section_ranges = list(sections(path))
                    for section, start, size in section_ranges:
                        if start <= m.start() < start + size and section.startswith(('.rodata', '.data')):
                            records.add(('runtime string ' + section, value))
                            break
        for kind, value in sorted(records):
            source, basis = resolve(value, candidates, sources)
            tu = next(iter(candidates)) if len(candidates) == 1 else UNKNOWN
            if kind.startswith('runtime string') and (lines := macro_lines(source)):
                basis += '; __FILE__ token at ' + source + ':' + lines + '; expansion 未確定'
            rows.append((str(path), kind, value, source, tu, basis, origin))
        coverage.append((str(path), 'ELF' if elf else 'ar' if archive else 'raw', ','.join(sorted(candidates)) or UNKNOWN, len(records), symbol_status, origin))
    table(out / 'artifact_paths.tsv', 'product kind spelling source compiling_TU evidence TU_evidence', rows)
    table(out / 'products.tsv', 'product format compile_inputs records symbol_evidence TU_evidence', coverage)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    inventory(parser.parse_args().output)
