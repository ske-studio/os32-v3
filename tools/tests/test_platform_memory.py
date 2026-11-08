"""Fixed-name memory provider selection, including a runtime wrong-include mutant."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='platform-memory-host', tier=1,
    covers=['include/callconv.h', 'tools/tests/test_platform_memory.py', 'include/platform_memory.h',
            'include/types.h', 'include/memmap.h', 'include/pc98.h',
            'platform/pc98/platform_memory_desc.h',
            'platform/powermac/platform_memory_desc.h'], needs=[], budget=2.7)

MOCK = '''static const struct platform_memory_geometry platform_memory_geometry = {
    11, 22, 33, 44, 55
};
static const struct platform_memory_attributes platform_memory_attributes = {
    {{1, 2}, {3, 4}, {5, 6}}, {7, 8}, {9, 10}
};
'''


def probe(work, provider, expected, mutant=False):
    header = (ROOT / 'include/platform_memory.h').read_text()
    old = '#include "platform_memory_desc.h"'
    assert header.count(old) == 1
    if mutant:
        header = header.replace(old, '#include "../platform/pc98/platform_memory_desc.h"')
    (work / 'include/platform_memory.h').write_text(header)
    (work / 'selected/platform_memory_desc.h').write_text(provider)
    # The genuine PC98 provider remains available to the wrong-include mutant;
    # compilation succeeds, and selecting its values must fail at runtime.
    (work / 'probe.c').write_text('''#include "platform_memory.h"
int main(void) {
    const struct platform_memory_geometry *g = &platform_memory_geometry;
    const struct platform_memory_attributes *a = &platform_memory_attributes;
    unsigned values[] = {g->legacy_end, g->system_first, g->system_end,
        g->high_first, g->ram_ceiling,
        a->native_uc[0].first, a->native_uc[0].end,
        a->native_uc[1].first, a->native_uc[1].end,
        a->native_uc[2].first, a->native_uc[2].end,
        a->bios_rom.first, a->bios_rom.end, a->low_device.first, a->low_device.end};
    unsigned expected[] = {''' + ','.join(map(str, expected)) + '''};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); i++)
        if (values[i] != expected[i]) return 1;
    return 0;
}
''')
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-MMD',
        '-I' + str(work / 'include'), '-I' + str(work / 'selected'),
        str(work / 'probe.c'), '-o', str(work / 'probe')], check=True)
    result = subprocess.run([str(work / 'probe')])
    assert result.returncode == (1 if mutant else 0), (mutant, result.returncode)
    deps = (work / 'probe.d').read_text()
    assert ('platform/pc98/platform_memory_desc.h' in deps) == mutant
    assert mutant or 'selected/platform_memory_desc.h' in deps
    print('RED: wrong PC98 include (runtime)' if mutant else 'GREEN: selected provider')


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='platform-memory-') as directory:
        work = Path(directory)
        for sub in ('include', 'selected', 'platform/pc98'):
            (work / sub).mkdir(parents=True)
        for rel in ('include/callconv.h', 'include/types.h', 'include/memmap.h', 'include/pc98.h',
                    'platform/pc98/platform_memory_desc.h'):
            (work / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, work / rel)
        probe(work, MOCK, [11,22,33,44,55,1,2,3,4,5,6,7,8,9,10])
        if mutate:
            probe(work, MOCK, [11,22,33,44,55,1,2,3,4,5,6,7,8,9,10], True)
        probe(work, (ROOT / 'platform/powermac/platform_memory_desc.h').read_text(), [0]*15)
        # PC98 values are also covered against actual memory users by
        # test_dep_dir_c_memory and the strict x86 artifact comparison.


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    main(parser.parse_args().mutate)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_platform_memory.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
