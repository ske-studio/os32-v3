#!/usr/bin/env python3
"""Private import ABI generation, compilation and actual nm-set regression tests."""
from kernel_includes import include_flags
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import host32
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
        altered[196].pop('retained')
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
        self.assertEqual(len(self.entries), 309)
        self.assertEqual([e['name'] for e in self.entries[221:234]], [
            'vfs_fstype', 'vfs_stat', 'vfs_open', 'vfs_close', 'vfs_read_fd',
            'vfs_write_fd', 'vfs_get_size', 'vfs_rm', 'dma_range_ok',
            'kcg_rom_read_ank', 'kcg_cache_stats', 'v86_io_reset_policy', 'paging_pte_flags'])
        # S5 (TASK_KPEEK §2-6・§2-8): peek's six imports plus T5's four LAN ones.
        self.assertEqual(gen.STAGES['S4'], 234)
        self.assertEqual(gen.STAGES['S5'], 264)
        self.assertEqual(gen.STAGES['S6'], 309)
        self.assertEqual(gen.STAGE_KINDS['S6'], ('FUNC',) * 38 + ('GETTER',) + ('SETTER',) * 2 + ('PTR',) * 3 + ('FUNC',))
        self.assertEqual([(e['stage'], e['name']) for e in self.entries[234:264]], [
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
        self.assertEqual(self.entries[196]['name'], 'v86_cui_display_restore')
        self.assertEqual(self.entries[196]['source'], 'kernel/v86/v86_io.h')
        self.assertTrue(self.entries[196]['retained'])
        self.assertEqual(self.entries[244]['name'], 'display_text_start')
        for index, name in ((244, 'display_text_start'), (250, 'display_restore_98'),
                            (251, 'display_recover_ready'), (252, 'display_recover_cell_read'),
                            (253, 'display_recover_cell_write')):
            self.assertEqual((self.entries[index]['name'], self.entries[index]['source']),
                             (name, 'kernel/console/display_restore.h'))
        self.assertEqual(self.entries[0]['name'], 'kprintf')
        self.assertEqual(self.entries[205]['name'], 'kdi_ptr_v86_session')
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
        additions = [('FUNC', 'os32x_layout_reason', 'exec/os32x_hdr.h'),
                     ('GETTER', 'con_sink_drop_count', 'include/con_sink.h'),
                     ('PTR', 'ring3_wm_shell_gen', 'exec/shell_service.h')]
        with tempfile.TemporaryDirectory(prefix='kdebug-s6-') as folder:
            out = Path(folder)
            document = json.loads(gen.DEFAULT_DEF.read_text())
            for kind, symbol, source in additions:
                document['imports'].append(dict(index=len(document['imports']),
                    stage='S6', kind=kind, symbol=symbol, source=source, context_safe=False))
            definition = out / 'imports.def'
            definition.write_text(json.dumps(document))
            with patch.dict(gen.STAGE_KINDS, S6=gen.STAGE_KINDS['S6'] + tuple(row[0] for row in additions)), \
                 patch.dict(gen.STAGES, S6=len(document['imports'])):
                entries = gen.load_def(definition)
                meta = gen.generate(entries, out, 'S6')
                self.assertEqual(meta['count'], len(self.entries) + len(additions))
                macros = (out / 'kdebug_import_macros.h').read_text()
                reader = next(e for e in self.entries if e['name'] == 'shlib_read_page')
                self.assertIn(f"#define KDI_INDEX_shlib_read_page {reader['index']}", macros)
                self.assertIn('__typeof__(&shlib_read_page)', macros)
                self.assertNotIn('KDI_ADDR_kprintf', macros)
                includes = ['.', 'include', 'kernel', 'drivers', 'exec', 'lib', 'gfx',
                            'fs', 'kapi', 'arch/x86', 'platform/pc98', 'sdk/include/os32']
                flags = ['-std=gnu11', '-ffreestanding', '-fno-pie', '-D__KERNEL_BUILD__']
                flags += include_flags(ROOT)
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
                omit_setter = False
                def with_s6(nm, paths, undefined):
                    refs = original(nm, paths, undefined)
                    if undefined and ROOT / 'build/out/kdebug/kselftest.o' in paths:
                        refs |= new_names
                    if undefined and omit_setter:
                        refs.discard('kdi_set_con_sink_drop_count')
                    return refs
                with patch.object(gen, 'symbols', side_effect=with_s6):
                    self.assertTrue(gen.check_nm(entries))
                    omit_setter = True
                    with self.assertRaisesRegex(ValueError, 'S6 nm mismatch.*kdi_set_con_sink_drop_count'):
                        gen.check_nm(entries)

    def test_nm_resolves_moved_objects_from_link_map(self):
        self.assertTrue(gen.check_nm(self.entries))
        original_read = Path.read_text
        def without_moved(path, *args, **kwargs):
            text = original_read(path, *args, **kwargs)
            if path == ROOT / 'build/out/kdebug/kdebug.map':
                text = '\n'.join(line for line in text.splitlines()
                    if not (line.startswith('LOAD ') and 'selftest_con_sink.o' in line))
            return text
        with patch.object(Path, 'read_text', without_moved):
            with self.assertRaisesRegex(ValueError, 'nm mismatch.*kst_con_sink'):
                gen.check_nm(self.entries)

    def test_con_kbd_move_contract_and_residency(self):
        self.assertEqual([e['name'] for e in self.entries if e['stage'] == 'S6'], [
            'con_sink_push_print', 'con_sink_push_clear', 'con_sink_push_cursor',
            'con_sink_push_exit', 'con_sink_stat', 'con_sink_read',
            'con_sink_owner_exit', 'appslot_cur', 'appslot_gfx_owner',
            'con_sink_reader_get', 'kbd_inject', 'kbd_inject_pending',
            'kbd_inject_take', 'kbd_inject_peek', 'kbd_inject_discard',
            'kdi_selftest_inj_push',
            'ledger_surface_create', 'ledger_surface_release', 'lease_acquire',
            'lease_check', 'lease_release', 'lease_revoke_all', 'paging_as_audit',
            'paging_lease_pte', 'paging_load_cr3', 'shlib_read_page',
            'kdi_selftest_lease_result',
            'kdi_selftest_exec_view', 'vfs_cwd_user', 'vfs_cwd',
            'vfs_devname_user', 'vfs_devname', 'path_get_drive_user',
            'path_get_drive', 'path_get_cwd_user', 'path_get_cwd',
            'kapi_reserved_nosys', 'os32x_layout_check',
            'kdi_get_kbd_inject_drop_count',
            'kdi_set_con_sink_drop_count', 'kdi_set_kbd_inject_drop_count',
            'kdi_ptr_ring3_ls_start', 'kdi_ptr_ring3_ls_end',
            'kdi_ptr___kernel_resident_end', 'gf_production_selected'])
        nm = str(self.cross / 'i386-elf-nm')
        kernel = gen.symbols(nm, [ROOT / 'build/out/kernel.elf'], False)
        moved = ROOT / 'build/out/kdebug/selftest_con_sink.o'
        module = gen.symbols(nm, [moved], False)
        for symbol in ('con_sink_selftest', 'kst_con_sink', 'g_self_buf', 'g_self_src'):
            self.assertNotIn(symbol, kernel)
        self.assertTrue({'kst_con_sink', 'g_self_buf', 'g_self_src'} <= module)
        self.assertIn('con_sink_drop_count', kernel)
        kbd = ROOT / 'build/out/kdebug/selftest_kbd_inject.o'
        self.assertIn('kst_kbd_inject', gen.symbols(nm, [kbd], False))
        for symbol in ('kbd_inject_selftest', 'kst_kbd_inject'):
            self.assertNotIn(symbol, kernel)
            self.assertNotIn(symbol, {e['symbol'] for e in self.entries})
        self.assertTrue({'kdi_selftest_inj_push', 'kbd_inject_drop_count'} <= kernel)
        self.assertEqual(gen.symbols(nm, [kbd], True) - {gen.ABI_SYMBOL}, {
            'appslot_cur', 'appslot_gfx_owner', 'con_sink_reader_get',
            'kbd_inject', 'kbd_inject_pending', 'kbd_inject_take',
            'kbd_inject_peek', 'kbd_inject_discard', 'kdi_selftest_inj_push',
            'kdi_get_kbd_inject_drop_count', 'kdi_set_kbd_inject_drop_count'})
        self.assertNotIn('con_sink_selftest', {e['symbol'] for e in self.entries})
        original = gen.symbols
        def without_setter(nm, paths, undefined):
            refs = original(nm, paths, undefined)
            if undefined:
                refs.discard('kdi_set_con_sink_drop_count')
            return refs
        with patch.object(gen, 'symbols', side_effect=without_setter):
            with self.assertRaisesRegex(ValueError, 'S6 nm mismatch.*kdi_set_con_sink_drop_count'):
                gen.check_nm(self.entries)

    def test_lease_move_contract_and_residency(self):
        nm = str(self.cross / 'i386-elf-nm')
        kernel = gen.symbols(nm, [ROOT / 'build/out/kernel.elf'], False)
        obj = ROOT / 'build/out/kdebug/selftest_lease.o'
        module = gen.symbols(nm, [obj], False)
        self.assertTrue({'kst_lease', 'tables_image'} <= module)
        for symbol in ('lease_selftest', 'kst_lease', 'tables_image'):
            self.assertNotIn(symbol, kernel)
            self.assertNotIn(symbol, {e['symbol'] for e in self.entries})
        self.assertTrue({'lease_selftest_result', 'kdi_selftest_lease_result'} <= kernel)
        self.assertEqual(gen.symbols(nm, [obj], True) - {gen.ABI_SYMBOL}, {
            'paging_kernel_pd_phys', 'paging_current_cr3', 'ledger_owner_new',
            'ledger_owner_retire', 'pgalloc_alloc_phys', 'pgalloc_free_n_owner',
            'ledger_surface_create', 'ledger_surface_release',
            'paging_addrspace_create_lease', 'paging_addrspace_destroy',
            'lease_acquire', 'lease_check', 'lease_release', 'lease_revoke_all',
            'paging_as_audit', 'paging_lease_pte', 'paging_load_cr3',
            'kdi_selftest_lease_result', 'kdi_ptr_ledger_surfaces',
            'kdebug_import_slots'})
        self.assertEqual(gen.address_references(self.entries, [obj], ROOT), {'shlib_read_page'})

    def test_exec_move_contract_and_residency(self):
        from test_exec_selftest import IMPORTS
        nm = str(self.cross / 'i386-elf-nm')
        obj = ROOT / 'build/out/kdebug/selftest_exec.o'
        kernel = gen.symbols(nm, [ROOT / 'build/out/kernel.elf'], False)
        module = gen.symbols(nm, [obj], False)
        for symbol in ('exec_tramp_user_selftest', 'exec_kapi_layout_selftest',
                       'kst_exec_tramp_user', 'kst_exec_kapi_layout'):
            self.assertNotIn(symbol, kernel)
            self.assertNotIn(symbol, {e['symbol'] for e in self.entries})
        self.assertTrue({'kst_exec_tramp_user', 'kst_exec_kapi_layout'} <= module)
        self.assertIn('kdi_selftest_exec_view', kernel)
        self.assertEqual(gen.symbols(nm, [obj], True) - {gen.ABI_SYMBOL},
                         set(IMPORTS) - {'kapi_reserved_nosys'} | {'kdebug_import_slots'})
        self.assertEqual(gen.address_references(self.entries, [obj], ROOT),
                         {'kapi_reserved_nosys'})

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

    def test_lease_callback_from_generated_slot(self):
        with tempfile.TemporaryDirectory(prefix='lease-import-address-') as folder:
            out = Path(folder)
            gen.generate(self.entries, out, 'S6')
            probe = out / 'callback.c'
            probe.write_text('#include "kdebug_import_macros.h"\n'
                'static int reader(u32 va, u32 frame) { return va == frame; }\n'
                'void _start(void) {\n'
                ' kdebug_import_slots[KDI_INDEX_shlib_read_page] = (uptr)reader;\n'
                ' int (*callback)(u32, u32) = KDI_ADDR_shlib_read_page;\n'
                ' int bad = callback != reader || callback == shlib_read_page ||\n'
                '     !callback(PAGE_SIZE, PAGE_SIZE) || callback(PAGE_SIZE, 0) ||\n'
                '     !shlib_read_page(PAGE_SIZE, PAGE_SIZE);\n'
                ' __asm__ volatile("int $0x80" : : "a"(1), "b"(bad));\n'
                ' for (;;) {}\n}\n')
            includes = ['.', 'include', 'kernel', 'drivers', 'exec', 'lib',
                        'gfx', 'fs', 'kapi', 'arch/x86', 'platform/pc98', 'sdk/include/os32']
            exe = out / 'callback'
            subprocess.run([str(self.cross / 'i386-elf-gcc'), '-m32', '-march=i386',
                '-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                '-nostdlib', '-static', '-D__KERNEL_BUILD__',
                '-Wl,--defsym=' + gen.ABI_SYMBOL + '=0',
                *include_flags(ROOT), str(probe),
                str(out / 'kdebug_import_slots.c'), str(out / 'kdebug_import_stubs.S'),
                '-o', str(exe)], check=True, capture_output=True)
            self.assertEqual(host32.run([str(exe)], capture_output=True, timeout=60).returncode, 0)

    def test_compile_all_generated_artifacts(self):
        includes = ['.', 'include', 'kernel', 'drivers', 'exec', 'lib', 'gfx', 'fs', 'kapi',
                    'arch/x86', 'platform/pc98', 'sdk/include/os32']
        flags = ['-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-builtin', '-D__KERNEL_BUILD__',
                 '-Werror=implicit-function-declaration'] + include_flags(ROOT)
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
