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
from unittest.mock import patch

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

    def test_retained_slot_requires_kernel_implementation(self):
        original = gen.symbols
        def without_legacy(nm, paths, undefined):
            refs = original(nm, paths, undefined)
            if not undefined and ROOT / 'build/out/kernel.elf' in paths:
                refs.discard('v86_cui_display_restore')
            return refs
        with patch.object(gen, 'symbols', side_effect=without_legacy):
            with self.assertRaisesRegex(ValueError, 'retained imports missing from kernel'):
                gen.check_nm(self.entries)
        altered = copy.deepcopy(self.entries)
        altered[201].pop('retained')
        with self.assertRaisesRegex(ValueError, 'nm mismatch: missing=.*v86_cui_display_restore'):
            gen.check_nm(altered)

    def test_gf_slots_retained_without_core_callers(self):
        for index in (e['index'] for e in self.entries if e['symbol'] in gen.GF_ONLY_SYMBOLS):
            altered = copy.deepcopy(self.entries)
            altered[index].pop('retained')
            with self.assertRaisesRegex(ValueError, 'GF-only slot must be retained'):
                gen.check_nm(altered)
            original = gen.symbols
            def without_slot(nm, paths, undefined):
                refs = original(nm, paths, undefined)
                if not undefined and ROOT / 'build/out/kernel.elf' in paths:
                    refs.discard(self.entries[index]['symbol'])
                return refs
            with patch.object(gen, 'symbols', side_effect=without_slot):
                with self.assertRaisesRegex(ValueError, 'retained imports missing from kernel'):
                    gen.check_nm(self.entries)

    def test_reject_gf_object_in_core_map(self):
        original = Path.read_text
        def contaminated(path, *args, **kwargs):
            text = original(path, *args, **kwargs)
            if path == ROOT / 'build/out/kdebug/kdebug.map':
                text += '\nLOAD build/out/hwdev/gfinit.o\n'
            return text
        with patch.object(Path, 'read_text', contaminated):
            with self.assertRaisesRegex(ValueError, 'GF object still linked'):
                gen.check_nm(self.entries)

    def test_reject_gf_slot_from_core_caller(self):
        original = gen.symbols
        def contaminated(nm, paths, undefined):
            refs = original(nm, paths, undefined)
            if undefined and ROOT / 'build/out/kdebug/kdebug_main.o' in paths:
                refs.add('pci_cfg_read32')
            return refs
        with patch.object(gen, 'symbols', side_effect=contaminated):
            with self.assertRaisesRegex(ValueError, 'GF-only slot must be retained'):
                gen.check_nm(self.entries)

    def test_selftest_boot_references_are_not_excluded(self):
        original = gen.symbols
        for symbol in ('memory_boot_page_counts', 'kdebug'):
            with self.subTest(symbol=symbol):
                def with_reference(nm, paths, undefined):
                    refs = original(nm, paths, undefined)
                    if undefined and ROOT / 'build/out/kdebug/kselftest.o' in paths:
                        refs.add(symbol)
                    return refs
                with patch.object(gen, 'symbols', side_effect=with_reference):
                    with self.assertRaisesRegex(ValueError, 'unexpected=.*' + symbol):
                        gen.check_nm(self.entries)

    def test_exact_order_kinds_context_and_hash(self):
        self.assertEqual(len(self.entries), 269)
        self.assertEqual([e['name'] for e in self.entries[226:239]], [
            'vfs_fstype', 'vfs_stat', 'vfs_open', 'vfs_close', 'vfs_read_fd',
            'vfs_write_fd', 'vfs_get_size', 'vfs_rm', 'dma_range_ok',
            'kcg_rom_read_ank', 'kcg_cache_stats', 'v86_io_reset_policy', 'paging_pte_flags'])
        # S5 (TASK_KPEEK §2-6・§2-8): peek's six imports plus T5's four LAN ones.
        self.assertEqual(gen.STAGES['S4'], 239)
        self.assertEqual(gen.STAGES['S5'], 269)
        self.assertEqual(gen.STAGES['S6'], 269)
        self.assertEqual(gen.STAGE_KINDS['S6'], ())
        self.assertEqual([(e['stage'], e['name']) for e in self.entries[239:]], [
            ('S5', 'paging_is_present'), ('S5', 'paging_map_phys'), ('S5', 'paging_pte_raw'),
            ('S5', 'paging_pte_restore'), ('S5', 'pci_find'), ('S5', 'pci_get'),
            ('S5', 'e82557_send'), ('S5', 'e82557_get_mac'), ('S5', 'e82557_state'),
            ('S5', 'sfs_lan_peer_mac'), ('S5', 'display_text_start'),
            ('S5', 'serial_get_setup'), ('S5', 'serial_trygetchar'),
            ('S5', 'serial_cmd_port'), ('S5', 'serial_data_port'),
            ('S5', 'serial_txrdy_mask'), ('S5', 'display_restore_98'),
            ('S5', 'display_recover_ready'), ('S5', 'display_recover_cell_read'),
            ('S5', 'display_recover_cell_write'),
            ('S5', 'serial_checked_begin'),
            ('S5', 'serial_checked_ready'),
            ('S5', 'serial_checked_put'),
            ('S5', 'serial_checked_empty'),
            ('S5', 'serial_checked_end'),
            ('S5', 'pci_cfg_read32'),
            ('S5', 'pci_cfg_read16'),
            ('S5', 'pci_cfg_write32'),
            ('S5', 'pci_cfg_write16'),
            ('S5', 'hwdev_module_ops'),
        ])
        self.assertEqual(self.entries[201]['name'], 'v86_cui_display_restore')
        self.assertEqual(self.entries[201]['source'], 'kernel/v86_io.h')
        self.assertTrue(self.entries[201]['retained'])
        self.assertEqual(self.entries[249]['name'], 'display_text_start')
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

    def test_s6_preparation_with_future_contracts(self):
        # Exercise future kinds without adding unused imports to the live ABI.
        additions = [('FUNC', 'shlib_read_page', 'kernel/shlib.h'),
                     ('GETTER', 'con_sink_drop_count', 'include/con_sink.h'),
                     ('SETTER', 'con_sink_drop_count', 'include/con_sink.h'),
                     ('PTR', 'ring3_ls_start', 'exec/ring3_ls.h')]
        with tempfile.TemporaryDirectory(prefix='kdebug-s6-') as folder:
            out = Path(folder)
            document = json.loads(gen.DEFAULT_DEF.read_text())
            for kind, symbol, source in additions:
                document['imports'].append(dict(index=len(document['imports']),
                    stage='S6', kind=kind, symbol=symbol, source=source, context_safe=False))
            definition = out / 'imports.def'
            definition.write_text(json.dumps(document))
            with patch.dict(gen.STAGE_KINDS, S6=tuple(row[0] for row in additions)), \
                 patch.dict(gen.STAGES, S6=len(document['imports'])):
                entries = gen.load_def(definition)
                meta = gen.generate(entries, out, 'S6')
                self.assertEqual(meta['count'], len(self.entries) + len(additions))
                macros = (out / 'kdebug_import_macros.h').read_text()
                self.assertIn('#define KDI_INDEX_shlib_read_page 269', macros)
                self.assertIn('__typeof__(&shlib_read_page)', macros)
                self.assertNotIn('KDI_ADDR_kprintf', macros)
                includes = ['.', 'include', 'kernel', 'drivers', 'exec', 'lib', 'gfx',
                            'fs', 'kapi', 'arch/x86', 'platform/pc98', 'sdk/include/os32']
                flags = ['-std=gnu11', '-ffreestanding', '-fno-pie', '-D__KERNEL_BUILD__']
                flags += ['-I' + str(ROOT / d) for d in includes]
                for name in ('kdebug_kexports.c', 'kdebug_import_slots.c', 'kdebug_import_stubs.S'):
                    subprocess.run([str(self.cross / 'i386-elf-gcc'), *flags, '-c',
                                    str(out / name), '-o', str(out / (name + '.o'))],
                                   check=True, capture_output=True)
                probe = out / 'probe.c'
                probe.write_text('#include "kdebug_import_macros.h"\n'
                    'int (*probe(void))(u32,u32) { kdi_set_con_sink_drop_count(0); '
                    'return KDI_ADDR_shlib_read_page; }\n')
                subprocess.run([str(self.cross / 'i386-elf-gcc'), *flags, '-c',
                                str(probe), '-o', str(out / 'probe.o')], check=True, capture_output=True)
                refs = gen.symbols(str(self.cross / 'i386-elf-nm'), [out / 'probe.o'], True)
                self.assertIn('kdebug_import_slots', refs)
                self.assertNotIn('shlib_read_page', refs)
                # Both data operations must be used: GETTER cannot mask a lost SETTER.
                original = gen.symbols
                new_names = {e['name'] for e in entries[len(self.entries):]}
                def with_s6(nm, paths, undefined):
                    refs = original(nm, paths, undefined)
                    if undefined and ROOT / 'build/out/kdebug/kselftest.o' in paths:
                        refs |= new_names
                    return refs
                with patch.object(gen, 'symbols', side_effect=with_s6):
                    self.assertTrue(gen.check_nm(entries))
                    new_names.remove('kdi_set_con_sink_drop_count')
                    with self.assertRaisesRegex(ValueError, 'S6 nm mismatch.*kdi_set_con_sink_drop_count'):
                        gen.check_nm(entries)

    def test_nm_resolves_moved_objects_from_link_map(self):
        original_symbols, original_read = gen.symbols, Path.read_text
        moved = ROOT / 'build/out/kdebug/selftest_con_sink.o'
        def read_map(path, *args, **kwargs):
            if path == ROOT / 'kdebug/selftest_con_sink.c':
                return '/* fixture: no raw address import */'
            text = original_read(path, *args, **kwargs)
            if path == ROOT / 'build/out/kdebug/kdebug.map':
                text += '\nLOAD build/out/kdebug/selftest_con_sink.o\n'
            return text
        def moved_symbols(nm, paths, undefined):
            refs = original_symbols(nm, [p for p in paths if p != moved], undefined)
            if undefined and ROOT / 'build/out/kdebug/kselftest.o' in paths:
                refs.add('kst_con_sink')
                refs.discard('con_sink_selftest')
            if moved in paths:
                refs.add('con_sink_selftest' if undefined else 'kst_con_sink')
            return refs
        original_exists = Path.exists
        with patch.object(Path, 'read_text', read_map), \
             patch.object(Path, 'exists', lambda p: p == moved or original_exists(p)), \
             patch.object(gen, 'symbols', side_effect=moved_symbols):
            self.assertTrue(gen.check_nm(self.entries))
        # Without the linked moved body, the local call must not become an import.
        with patch.object(gen, 'symbols', side_effect=moved_symbols):
            with self.assertRaisesRegex(ValueError, 'nm mismatch.*kst_con_sink'):
                gen.check_nm(self.entries)

    def test_address_manifest_uses_only_linked_moved_sources(self):
        entries = [dict(name=name, kind_name='FUNC') for name in gen.ADDRESS_IMPORTS]
        with tempfile.TemporaryDirectory(prefix='kdebug-address-') as folder:
            root = Path(folder)
            (root / 'kdebug').mkdir()
            source = root / 'kdebug/selftest_exec.c'
            source.write_text('return KDI_ADDR_kapi_reserved_nosys;\n')
            obj = root / 'build/out/kdebug/selftest_exec.o'
            self.assertEqual(gen.address_references(entries, [obj], root), {'kapi_reserved_nosys'})
            self.assertEqual(gen.address_references(entries, [], root), set())
            with self.assertRaisesRegex(ValueError, 'invalid address import'):
                gen.address_references([], [obj], root)
            source.write_text('return kapi_reserved_nosys;\n')
            with self.assertRaisesRegex(ValueError, 'missing address import'):
                gen.address_references(entries, [obj], root)
            source.write_text('return KDI_ADDR_shlib_read_page;\n')
            with self.assertRaisesRegex(ValueError, 'invalid address import'):
                gen.address_references(entries, [obj], root)

    def test_compile_all_generated_artifacts(self):
        includes = ['.', 'include', 'kernel', 'drivers', 'exec', 'lib', 'gfx', 'fs', 'kapi',
                    'arch/x86', 'platform/pc98', 'sdk/include/os32']
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
