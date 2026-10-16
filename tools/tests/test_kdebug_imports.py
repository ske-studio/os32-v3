#!/usr/bin/env python3
"""Private import ABI generation, compilation and actual nm-set regression tests."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_kdebug_imports as gen


class ImportsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = gen.load_def()
        cls.cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'

    def test_stage_sets_from_actual_original_objects(self):
        self.assertTrue(gen.check_nm(self.entries))
        altered = copy.deepcopy(self.entries)
        altered[1]['symbol'] = 'invented_import'
        with self.assertRaisesRegex(ValueError, 'nm mismatch'):
            gen.check_nm(altered)

    def test_exact_order_kinds_context_and_hash(self):
        self.assertEqual(len(self.entries), 218)
        self.assertEqual(self.entries[0]['name'], 'kprintf')
        self.assertEqual(self.entries[210]['name'], 'kdi_ptr_v86_session')
        self.assertEqual({e['name'] for e in self.entries if e['context_safe']}, {
            'serial_puts_polled', 'kdi_get_kctx_irq_depth', 'kdi_get_kctx_exc_depth',
            'kdi_ptr_v86_gcap_phase', 'kdi_get_v86_gp_cs', 'kdi_get_v86_gp_ip', 'kdi_ptr_v86_session'})
        for field, value in [('name', 'changed'), ('signature', 'int changed(void);'), ('index', 9)]:
            changed = copy.deepcopy(self.entries)
            changed[0][field] = value
            self.assertNotEqual(gen.abi_hash(self.entries), gen.abi_hash(changed))
        self.assertEqual(gen.fnv1a(b'hello'), 0x4f9f2cab)

    def test_actual_declarations(self):
        by_name = {e['name']: e for e in self.entries}
        self.assertEqual(by_name['kdi_get_ring3_in_syscall']['signature'], 'int kdi_get_ring3_in_syscall(void);')
        self.assertEqual(by_name['kdi_ptr_time_test_feed']['signature'], 'struct time_test_feed *kdi_ptr_time_test_feed(void);')
        self.assertIn('...', by_name['kprintf']['signature'])
        # Re-reading a changed authoritative header changes the ABI input.
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'probe.h'
            row = {'kind': 'FUNC', 'symbol': 'probe', 'source': 'probe.h'}
            header.write_text('int probe(const char *s);\n')
            before = gen.declaration(row, Path(folder))
            header.write_text('unsigned probe(const char *s);\n')
            self.assertNotEqual(before, gen.declaration(row, Path(folder)))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bad.def'
            document = json.loads(gen.DEFAULT_DEF.read_text())
            document['imports'][1]['index'] = 0
            path.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, 'index/stage/kind'):
                gen.load_def(path)

    def test_compile_all_generated_artifacts(self):
        includes = ['.', 'include', 'kernel', 'drivers', 'exec', 'lib', 'gfx', 'fs', 'kapi',
                    'arch/i386', 'platform/pc98', 'sdk/include/os32']
        flags = ['-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-builtin', '-D__KERNEL_BUILD__',
                 '-Werror=implicit-function-declaration'] + ['-I' + str(ROOT / d) for d in includes]
        with tempfile.TemporaryDirectory(prefix='kdebug-imports-') as folder:
            out = Path(folder)
            for stage, count in gen.STAGES.items():
                metadata = gen.generate(self.entries, out, stage)
                self.assertEqual(metadata['count'], count)
                for name in ['kdebug_kexports.c', 'kdebug_import_slots.c', 'kdebug_import_stubs.S']:
                    subprocess.run([str(self.cross / 'i386-elf-gcc'), *flags, '-c', str(out / name),
                                    '-o', str(out / (name + '.o'))], check=True, capture_output=True)
                stubs = gen.symbols(str(self.cross / 'i386-elf-nm'), [out / 'kdebug_import_stubs.S.o'], False)
                self.assertEqual(stubs, {e['name'] for e in self.entries[:count]})
                slot_symbols = subprocess.check_output([str(self.cross / 'i386-elf-nm'), '-S',
                                                       str(out / 'kdebug_import_slots.c.o')], text=True)
                self.assertIn(f'{count * 4:08x} D kdebug_import_slots', slot_symbols)
            # The struct object must dereference the pointer, arrays must not.
            macros = (out / 'kdebug_import_macros.h').read_text()
            self.assertIn('#define v86_session (*kdi_ptr_v86_session())', macros)
            self.assertIn('#define ledger_regions (kdi_ptr_ledger_regions())', macros)
            probe = out / 'probe.c'
            probe.write_text('#include "kdebug_import_macros.h"\n'
                             'int probe(void) { kdi_set_ring3_in_syscall(0); '
                             'time_test_feed[0].p1 = 1; return v86_session.aborting + kctx_irq_depth; }\n')
            subprocess.run([str(self.cross / 'i386-elf-gcc'), *flags, '-c', str(probe), '-o', str(out / 'probe.o')],
                           check=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true', help='negative contract probes are always exercised')
    parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ImportsTests))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
