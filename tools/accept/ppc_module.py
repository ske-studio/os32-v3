"""Exercise the paired module transaction on a PowerMac link-sh root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
FIX = ROOT / 'tools/tests/ppc_module'
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
import mkmod
import root_pair
from gen_sqlite_imports import generate
from kboot_accept import build, mutant_settings
from ppc_shm import guest_available


def fixtures(work, cross):
    definition = work / 'imports.json'
    definition.write_text(json.dumps(dict(schema=1, contracts=[], imports=[dict(
        symbol='probe_sum', declaration='int probe_sum(int *, ...);', context='normal', irq=False)])))
    metadata = generate(work, definition, module='kcg', target='ppc-powermac')
    prefix = str(cross / 'bin/powerpc-elf-')
    cc = [prefix + 'gcc', '-O2', '-m32', '-mcpu=7450', '-msoft-float', '-msdata=none',
          '-ffreestanding', '-fno-pic', '-fno-stack-protector', '-I' + str(work),
          '-I' + str(ROOT / 'include'), '-c']
    def run(args):
        subprocess.run(args, cwd=ROOT, check=True)
    run([*cc, FIX / 'provider.c', '-o', work / 'provider.o'])
    reloc = subprocess.check_output([prefix + 'objdump', '-r', work / 'provider.o'], text=True)
    assert 'RELOCATION RECORDS FOR [.text]' not in reloc
    run([prefix + 'objcopy', '-O', 'binary', '-j', '.text', work / 'provider.o', work / 'provider.bin'])
    code = (work / 'provider.bin').read_bytes()
    assert 0 < len(code) <= 4096
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    kapi, layout = spec['version'], spec['generations']['memory_layout']
    (work / 'probe_data.h').write_text(
        f'#define PROBE_ABI {metadata["module_abi_gen"]}u\n#define PROBE_KAPI {kapi}u\n'
        f'#define PROBE_LAYOUT {layout}u\nstatic const struct module_symbol_contract probe_import = '
        f'{{{mkmod.fnv1a(b"probe_sum")}u,1,1}};\n'
        f'static const struct module_symbol_contract probe_export = '
        f'{{{mkmod.fnv1a(b"initialize")}u,1,0}};\nstatic const u8 probe_code[] = '
        '{' + ','.join(map(str, code)) + '};\n')
    manifest = json.loads((ROOT / 'build/ppc-ram-root.json').read_text())
    config = work / 'system.cfg'
    config.write_text('KDEBUG=1\n')
    manifest['/etc/system.cfg'] = str(config)
    for src in ('kcg_import_stubs.S', 'kcg_import_slots.c'):
        run([*cc, work / src, '-o', work / (src + '.o')])
    for name, value in [('a', 11), ('b', 27)]:
        run([*cc, f'-DPROBE_VALUE={value}', FIX / 'body.c', '-o', work / 'body.o'])
        elf = work / (name + '.elf')
        run([prefix + 'ld', '--emit-relocs', '-z', 'noexecstack', '-T', 'build/ppc-module.ld', '-e', 'kcg_module_init',
             work / 'body.o', work / 'kcg_import_stubs.S.o', work / 'kcg_import_slots.c.o', '-o', elf])
        image, _ = mkmod.build(elf.read_bytes(), metadata, None, kapi, layout,
                               target='ppc-powermac', module_id=5)
        path = work / (name + '.mod')
        path.write_bytes(image)
        manifest['/sys/' + path.name] = str(path)
    path = work / 'manifest.json'
    path.write_text(json.dumps(manifest))
    return path


def foreign_image(elf, pair):
    data = bytearray(elf.read_bytes())
    wire, rec, off, frame = root_pair.wire(data)
    own = zlib.crc32(wire)
    assert own != pair
    iso = root_pair.bind_iso((elf.parent / 'root.iso').read_bytes(), pair)
    block = root_pair.compress(iso)
    assert len(block) <= frame
    data[off:off + frame] = block.ljust(frame, b'\0')
    struct.pack_into('<4I', data, rec, own, zlib.crc32(iso), len(block), len(iso))
    out = elf.with_name('foreign.elf')
    out.write_bytes(data)
    return out


def observe(elf, folder, expected):
    from link_sh_run import Session
    folder.mkdir()
    for cpu in ('7450', '7455'):
        session = Session(elf, cpu, folder / cpu, 240)
        try:
            session.start()
            session.wait(session.ready, 'module probe READY', 90)
            serial = (session.out / 'serial-a.log').read_text(errors='replace')
            lines = [line[line.index('[module]'):].strip() for line in serial.splitlines() if '[module]' in line]
            assert lines == expected, (cpu, lines, expected)
        finally:
            session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not guest_available():
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-module-')).resolve()
    manifest = fixtures(work, Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')))
    probe = work / 'ppc_kernel.c'
    kernel = (ROOT / 'platform/powermac/ppc_kernel.c').read_text()
    anchor = '    if (root_ready) {'
    assert kernel.count(anchor) == 1
    probe.write_text('static int probe_modules(void);\n' + kernel.replace(
        anchor, anchor + '\n        kprintf(KBOOT_ATTR, "[module] result=%d\\n", probe_modules());') +
        (FIX / 'probe.inc').read_text())
    images = []
    for case in (['normal', 'bypass'] if args.mutate else ['normal']):
        settings = []
        if case == 'bypass':
            settings = [x.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for x in mutant_settings(
                work, case, 'kernel/module/module_image.c', 'if (h[MI_PAIR_CRC] != policy->pair_image_crc)',
                'if (0)')]
        elf = build(work / case, 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(work / 'user'),
                    'KB_MUT_SRC=platform/powermac/ppc_kernel.c', 'KB_MUT_FILE=' + str(probe),
                    'KB_ROOT_TEMPLATE=' + str(manifest), *settings, 'kb-user', 'kernel')
        cfg = subprocess.check_output(['isoinfo', '-i', elf.parent / 'root.iso',
                                       '-x', '/ETC/SYSTEM.CFG;1'])
        assert cfg == b'KDEBUG=1\n', 'paired guest must enable the product diagnostic loader'
        images.append(elf)
    cases = [('normal', images[0], ['[module] diag state=1 list=0 unsupported=2', '[module] result=0'])]
    if args.mutate:
        pairs = [zlib.crc32(root_pair.wire(path.read_bytes())[0]) for path in images]
        cases.extend([('foreign', foreign_image(images[0], pairs[1]),
                       ['[module] diag state=4 list=-1 unsupported=-1', '[module] first rc=4 starts=0', '[module] result=4']),
                      ('bypass', foreign_image(images[1], pairs[0]), ['[module] diag state=1 list=0 unsupported=2', '[module] result=0'])])
    digests = set()
    for case, elf, expected in cases:
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in digests
        digests.add(digest)
        print(case, digest, elf, flush=True)
        if not args.build_only:
            observe(elf, work / ('guest-' + case), expected)
    print(f'{len(digests)} distinct PowerMac module acceptance images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
