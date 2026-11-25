"""Evaluate the frozen GF table source for the generator and comparison suite."""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / 'tools/recover_tables'
ORACLE = DATA / 'recover_tables_oracle.c'
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-O0',
         '-ffunction-sections', '-fdata-sections', '-I' + str(ROOT / 'include')]


def run_extractor(exe):
    runner = os.environ.get('HOST32_RUNNERS', 'native qemu').split()[0]
    launch = {'native': ['setarch', os.uname().machine, '-R'], 'qemu': ['qemu-i386']}
    return subprocess.run([*launch[runner], str(exe)], cwd=ROOT, timeout=60,
                          capture_output=True)


def wrapper(source, text, name, bounds=None):
    """Discover declarations only; C, not Python, evaluates initializers."""
    arrays = re.findall(r'^static const (?:u32|struct recover_guard) (\w+)\[\]', text, re.M)
    evaluators = re.findall(r'^static int (\w+)\(const u32 \*\w+, u32 \w+, u32 \*\w+\)', text, re.M)
    exports = re.findall(r'^const (?:struct \w+|u32) (recover_gf_\w+)', text, re.M)
    prefix = ''.join(f'#define {symbol} oracle_{symbol}\n' for symbol in exports) if name == 'oracle' else ''
    result = prefix + f'#include "{source}"\n'
    result += 'static const char *rt_symbol(const void *p)\n{\n    if (!p) return "NULL";\n'
    result += ''.join(f'    if (p == {symbol}) return "{symbol}";\n' for symbol in arrays)
    result += '    return "UNKNOWN";\n}\n'
    result += 'static const char *rt_eval_symbol(int (*p)(const u32 *, u32, u32 *))\n{\n    if (!p) return "NULL";\n'
    result += ''.join(f'    if (p == {symbol}) return "{symbol}";\n' for symbol in evaluators)
    result += '    return "UNKNOWN";\n}\n'
    if bounds is not None:
        result += '#define RT_EXPAND_PRODUCT 1\n'
        result += f'#include "{bounds}"\n'
    result += f'#define RT_DUMP recover_tables_dump_{name}\n#include "{DATA / "recover_tables_host.h"}"\n'
    return result


def records(stream):
    result = []
    keys = set()
    for line in stream.splitlines():
        table, row, field, slot, value = line.split('\t')
        key = (table, int(row), field, int(slot))
        if key in keys or value == 'UNKNOWN':
            raise ValueError(f'duplicate field or unregistered reference: {line}')
        keys.add(key)
        result.append([*key, int(value) if value.isdecimal() else value])
    return result


def load_provenance():
    provenance = json.loads((DATA / 'recover_tables_oracle.json').read_text())
    if hashlib.sha256(ORACLE.read_bytes()).hexdigest() != provenance['oracle_sha256']:
        raise ValueError('frozen oracle SHA256 mismatch')
    return provenance


def extract_oracle():
    """Evaluate the frozen TU alone, also before a generated product exists."""
    provenance = load_provenance()
    with tempfile.TemporaryDirectory(prefix='recover-oracle-') as tmp:
        temp = pathlib.Path(tmp)
        unit = temp / 'oracle.c'
        unit.write_text(wrapper(ORACLE, ORACLE.read_text(), 'oracle'))
        main = temp / 'main.c'
        main.write_text((DATA / 'recover_tables_host.c').read_text().replace(
            '    recover_tables_dump_product();', ''))
        exe = temp / 'extract'
        subprocess.run(['gcc', *FLAGS, '-nostdlib', '-static', '-no-pie',
                        '-Wl,--gc-sections', str(unit), str(ROOT / 'kdebug/recover.c'),
                        str(main), '-o', str(exe)], check=True)
        run = run_extractor(exe)
        if run.returncode or not run.stdout.endswith(b'END\n'):
            raise RuntimeError(f'oracle extraction failed: {run.stderr!r}')
        return {'schema': 1, 'provenance': provenance,
                'records': records(run.stdout.decode()[:-4])}
