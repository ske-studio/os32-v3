"""Load the shipped FEP on mac99 and exercise its relocated import stubs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from ppc_module import ROOT, build_media, foreign_image, mkmod, mutant_settings, observe_lines, root_pair, guest_available


def verify_media(elf, module, symbols):
    wire, record, _, _ = root_pair.wire(elf.read_bytes())
    pair = zlib.crc32(wire)
    assert struct.unpack_from('<I', elf.read_bytes(), record)[0] == pair
    final = subprocess.check_output(['isoinfo', '-i', elf.parent / 'root.iso',
                                     '-x', '/BOOT/FEP.MOD;1'])
    assert final == root_pair.bind_module(module.read_bytes(), pair)
    header = dict(zip(mkmod.HEADER_FIELDS, struct.unpack_from(
        '<' + 'I' * len(mkmod.HEADER_FIELDS), final)))
    assert header['IMPORT_COUNT'] == len(symbols)
    for i, name in enumerate(symbols):
        record = struct.unpack_from('<IHHI', final, header['IMPORT_OFF'] + 12 * i)
        assert record == (mkmod.fnv1a(name.encode()), 1, 1, header['DATA_OFF'] + 4 * i), name
    disk = elf.parent / 'copy.ext2'
    with disk.open('wb') as stream:
        stream.truncate(4 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', disk], check=True)
    copy = elf.parent / 'fep.mod'
    copy.write_bytes(final)
    subprocess.run(['debugfs', '-w', '-R', 'mkdir /boot', disk], check=True)
    subprocess.run(['debugfs', '-w', '-R', f'write {copy} /boot/fep.mod', disk], check=True)
    assert subprocess.check_output(['debugfs', '-R', 'cat /boot/fep.mod', disk]) == final
    print(f'{elf}: pair={pair:08x} fep.mod={len(final)} bytes', flush=True)


def probe_source(work, directory, symbols):
    elf = mkmod.Elf((directory / 'fep.elf').read_bytes(), 'ppc-powermac')
    data = ['#include "module_runtime.h"', '#include "os32_generations.h"',
            f'#include "{directory}/fep_imports.h"']
    data.append('static const struct module_symbol_contract probe_imports[] = {')
    data.extend(f'    {{{mkmod.fnv1a(name.encode())}u, 1, 1}},' for name in symbols)
    data.append('};')
    data.append('extern const struct module_symbol_contract fep_export_contracts[];')
    for label, symbol in [('COPY', 'kmemcpy'), ('PRINT', 'kprintf')]:
        slot = symbols.index(symbol)
        data.extend([f'#define PROBE_{label} {elf.symbol(symbol)["value"]}u',
                     f'#define PROBE_{label}_SLOT {slot}'])
    data.append(f'#define PROBE_KAPI {json.loads((ROOT / "sdk/kapi.json").read_text())["version"]}')
    (work / 'fep_probe_data.h').write_text('\n'.join(data) + '\n')
    source = (ROOT / 'platform/powermac/ppc_kernel.c').read_text()
    anchor = '    if (root_ready) {'
    assert source.count(anchor) == 1
    path = work / 'ppc_kernel.c'
    path.write_text('static void fep_probe(void);\n' + source.replace(anchor, anchor + '\n        fep_probe();')
                    + (ROOT / 'tools/tests/ppc_fep/probe.inc').read_text())
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not guest_available():
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-fep-')).resolve()
    config = work / 'system.cfg'
    config.write_text('KDEBUG=1\n')
    common = ['IMAGE=powermac', f'KB_SH_OUT={work}/user', f'PPC_SYSTEM_CFG={config}']
    normal_dir = work / 'normal/fep'
    subprocess.run(['make', 'ARCH=ppc', 'PLATFORM=powermac', *common,
                    f'KB_OUT={normal_dir.parent}', normal_dir / 'fep.mod'], check=True)
    definition = json.loads((ROOT / 'tools/prototypes/fep_imports.def').read_text())
    symbols = [row['symbol'] for row in definition['imports']]
    probe = probe_source(work, normal_dir, symbols)
    images = {}
    digests = set()
    for case in (['normal', 'bypass'] if args.mutate else ['normal']):
        settings = []
        if case == 'bypass':
            settings = [s.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for s in mutant_settings(
                work, case, 'kernel/module/module_image.c', 'if (h[MI_PAIR_CRC] != policy->pair_image_crc)',
                'if (0)')]
        media = build_media(work / case, *common, 'KB_MUT_SRC=platform/powermac/ppc_kernel.c',
                    f'KB_MUT_FILE={probe}', *settings, 'kb-user', 'kernel')
        module = media.elf.parent / 'fep/fep.mod'
        assert module.read_bytes() == (normal_dir / 'fep.mod').read_bytes()
        verify_media(media.elf, module, symbols)
        images[case] = media
    donor = images.get('bypass') or build_media(work / 'donor', *common, 'kb-user', 'kernel')
    normal = images['normal']
    def pair(media):
        return zlib.crc32(root_pair.wire(media.elf.read_bytes())[0])
    cases = [('local', normal), ('foreign', foreign_image(normal, pair(donor)))]
    if args.mutate:
        cases.append(('bypass', foreign_image(donor, pair(normal))))
    for case, media in cases:
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert digest not in digests
        digests.add(digest)
        print(case, digest, flush=True)
        if not args.build_only:
            call = '[module] fep 1 2 3 4 5 6 7'
            accepted = [call, '[module] result=0 starts=1']
            expected = accepted if case == 'local' else ['[module] result=4 starts=0']

            def check(cpu, lines):
                if lines == expected:
                    assert case != 'bypass', f'{cpu}: pair bypass survived'
                else:
                    assert case == 'bypass' and lines == accepted, (cpu, lines, expected)
                    print(f'RED {cpu}: foreign FEP started: {lines}', flush=True)

            observe_lines(media, work / ('guest-' + case), check)
    print(f'{len(digests)} distinct FEP acceptance images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
