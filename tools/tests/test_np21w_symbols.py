"""Offline identity tests with real linked ELF/maps and synthetic guest RAM."""
CHECK = dict(name='np21w-symbols-host', tier=1,
    covers=['platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'tools/tests/test_np21w_symbols.py', 'tools/np21w_mcp/symbols.py',
            'tools/np21w_mcp/server.py', 'tools/np21w_mcp/gui.py',
            'tools/np21w_mcp/np21w_client.py', 'userland/gshell/src/startmenu.rs',
            'userland/gshell/src/taskbar.rs', 'include/bootinfo.h', 'include/build_id.h', 'include/types.h'],
    needs=[], budget=3.1)


import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.np21w_mcp import symbols, gui, server

SELECTOR = symbols.VerifiedSymbols
SOURCE = ROOT / 'tools/np21w_mcp/symbols.py'


class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        self.root = self.home / 'os32-v3'
        self.a = self.build(self.root, 0)
        self.b = self.build(self.home / 'wt/os32-v3/deployed', 16)
        self.boot(self.b)

    def build(self, root, padding, sub=None):
        out = root / 'build' / 'out'
        if sub:
            out = out / sub
        out.mkdir(parents=True)
        asm = out / 'fixture.s'
        asm.write_text('''.text
.global kentry
kentry: nop
.data
.global os32_build_commit
.type os32_build_commit,@object
os32_build_commit: .asciz "abcdef0-dirty"
.zero 10
.size os32_build_commit,24
.zero %d
.global s_bootinfo
.type s_bootinfo,@object
s_bootinfo: .zero 48
.size s_bootinfo,48
.global probe
.type probe,@object
probe: .long 7
.size probe,4
''' % padding)
        linker = out / 'fixture.ld'
        linker.write_text('SECTIONS { . = 0x100000; .text : { *(.text) } .data : { *(.data) } }')
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'
        subprocess.run([str(cross / 'i386-elf-as'), '--32', '-o', str(out / 'fixture.o'), str(asm)], check=True, capture_output=True)
        subprocess.run([str(cross / 'i386-elf-ld'), '-T', str(linker), '-Map=' + str(out / 'kernel.map'),
                        '-o', str(out / 'kernel.elf'), str(out / 'fixture.o')], check=True, capture_output=True)
        base, binary = symbols.elf_image(out / 'kernel.elf')
        # Identity reads only header CRCs; a real compressed block is unnecessary.
        image = bytearray(struct.pack('<11I', 0x32334b56, 44, 2, 1,
            base, len(binary), 44, 1, zlib.crc32(binary), 45, 0) + b'x')
        struct.pack_into('<I', image, 40, zlib.crc32(image))
        (out / 'vmkernel.lz4').write_bytes(image)
        (out / 'build_id.c').write_text('const char os32_build_commit[BUILD_COMMIT_MAX] = "abcdef0-dirty";')
        return out / 'kernel.map'

    def boot(self, path):
        self.elf = gui.ElfSymbols(path.with_name('kernel.elf')).read()
        self.memory = {}
        base, binary = symbols.elf_image(path.with_name('kernel.elf'))
        self.put(base, binary)
        image = path.with_name('vmkernel.lz4').read_bytes()
        boot = bytearray(48)
        boot[36] = 1
        struct.pack_into('<II', boot, 40, struct.unpack_from('<I', image, 40)[0], len(image))
        self.put(self.elf['s_bootinfo'][0], boot)

    def put(self, addr, data):
        self.memory.update((addr + i, b) for i, b in enumerate(data))

    def read(self, addr, size):
        return bytes(self.memory.get(addr + i, 0) for i in range(size))

    def selector(self, **kw):
        return SELECTOR(self.read, root=self.root, home=self.home, **kw)

    def test_probe_layout_matches_target_headers(self):
        source = self.home / 'layout.c'
        source.write_text('#include "platform/pc98/bootinfo_native.h"\n#include "build_id.h"\n_Static_assert(sizeof(struct bootinfo) == 48, "bootinfo size");\n_Static_assert(__builtin_offsetof(struct bootinfo, status) == 0, "status");\n_Static_assert(__builtin_offsetof(struct bootinfo, img_valid) == 36, "valid");\n_Static_assert(__builtin_offsetof(struct bootinfo, img_crc) == 40, "crc");\n_Static_assert(__builtin_offsetof(struct bootinfo, img_size) == 44, "size");\n_Static_assert(BUILD_COMMIT_MAX == 24, "commit");\n')
        gcc = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(gcc), '-std=gnu11', '-I', str(ROOT), '-I', str(ROOT / 'include'),
                        '-c', str(source), '-o', str(self.home / 'layout.o')],
                       check=True, capture_output=True)

    def test_discovery_selects_deployed_build_with_same_dirty_commit(self):
        selected = self.selector()
        self.assertEqual(selected.resolve('probe'), self.elf['probe'][0])
        self.assertEqual(selected.info['path'], str(self.b))
        self.assertEqual(selected.info['commit'], 'abcdef0-dirty')
        self.assertEqual(selected.annotate(self.elf['probe'][0]), 'probe')

    def test_discovery_selects_r1_image(self):
        # The R1 map lives in build/out/r1/ beside its own kernel.elf/vmkernel.lz4.
        r1 = self.build(self.home / 'wt/os32-v3/deployed', 32, sub='r1')
        self.boot(r1)
        selected = self.selector()
        self.assertIn(r1.resolve(), selected.candidates())
        self.assertEqual(selected.resolve('probe'), self.elf['probe'][0])
        self.assertEqual(selected.info['path'], str(r1))

    def test_mismatch_refuses_symbols_but_keeps_numeric_access(self):
        selected = self.selector(explicit=str(self.a))
        with self.assertRaisesRegex(ValueError, 'no matching kernel.map'):
            selected.resolve('probe')
        self.assertEqual(selected.resolve('0x1234'), 0x1234)
        self.assertEqual(selected.annotate(0x100000), '')
        self.assertIsNone(selected.info['path'])

    def test_explicit_override_is_restricted_and_verified(self):
        with patch.dict(os.environ, {'OS32_KERNEL_MAP': str(self.b)}):
            selected = self.selector()
            self.assertEqual(selected.resolve('probe+4'), self.elf['probe'][0] + 4)
            self.assertEqual(selected.info['path'], str(self.b))
        with patch.dict(os.environ, {'OS32_KERNEL_MAP': str(self.a)}):
            with self.assertRaises(ValueError):
                self.selector().resolve('probe')

    def test_missing_and_stale_artifacts_fail_closed(self):
        # Wrong map beside the right image, then wrong ELF/image pairing.
        self.b.write_text(self.a.read_text())
        with self.assertRaisesRegex(ValueError, 'map/ELF'):
            self.selector(explicit=str(self.b)).resolve('probe')
        shutil.copyfile(self.a.with_name('kernel.elf'), self.b.with_name('kernel.elf'))
        with self.assertRaisesRegex(ValueError, 'ELF/VK32'):
            self.selector(explicit=str(self.b)).resolve('probe')
        self.b.with_name('vmkernel.lz4').unlink()
        with self.assertRaises(ValueError):
            self.selector(explicit=str(self.b)).resolve('probe')

    def test_guest_commit_crc_and_validity_are_required(self):
        address = self.elf['s_bootinfo'][0]
        commit = self.elf['os32_build_commit'][0]
        for addr in (address, address + 36, address + 40, address + 44, commit):
            before = self.memory[addr]
            self.memory[addr] ^= 1
            with self.subTest(addr=addr), self.assertRaises(ValueError):
                self.selector(explicit=str(self.b)).resolve('probe')
            self.memory[addr] = before

    def test_image_crc_is_checked_independently(self):
        self.memory[self.elf['s_bootinfo'][0] + 40] ^= 1
        with self.assertRaisesRegex(ValueError, 'Image CRC'):
            self.selector(explicit=str(self.b)).resolve('probe')

    def test_elf_payload_crc_is_checked_independently(self):
        image = bytearray(self.b.with_name('vmkernel.lz4').read_bytes())
        image[32] ^= 1  # Only the raw payload CRC differs; length is unchanged.
        struct.pack_into('<I', image, 40, 0)
        struct.pack_into('<I', image, 40, zlib.crc32(image))
        self.b.with_name('vmkernel.lz4').write_bytes(image)
        self.boot(self.b)
        with self.assertRaisesRegex(ValueError, 'ELF/VK32'):
            self.selector(explicit=str(self.b)).resolve('probe')

    def test_reset_rechecks_running_guest(self):
        selected = self.selector()
        selected.select()
        self.boot(self.a)
        selected.reset()
        self.assertEqual(selected.resolve('probe'), self.elf['probe'][0])
        self.assertEqual(selected.info['path'], str(self.a))

    def get(self, url):
        query = parse_qs(urlsplit(url).query)
        self.assertEqual(urlsplit(url).path, '/api/mem')
        return json.dumps({'hex': self.read(int(query['addr'][0], 0), int(query['len'][0])).hex()}).encode()

    def test_mcp_read_and_wait_mem_use_selected_map_and_report_it(self):
        selected = self.selector()
        transport = gui.Transport(get=lambda url: (self.get(url), {}))
        observer = gui.Gui(transport)
        observer.kernel_maps = self.selector()
        observer.gshell_elf = str(self.b.with_name('kernel.elf'))
        with patch.object(server, 'SYMS', selected), patch.object(server, 'GUI', observer), \
                patch.object(server.emu, 'get', side_effect=self.get):
            for name, args in (
                ('emu_read_mem', {'addr': 'probe', 'len': 4}),
                ('emu_wait_mem', {'symbol': 'probe', 'op': 'eq', 'value': 7, 'timeout': 15})):
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    server._handle({'id': 1, 'method': 'tools/call',
                                    'params': {'name': name, 'arguments': args}})
                reply = json.loads(output.getvalue())['result']
                self.assertNotIn('isError', reply, reply)
                payload = json.loads(reply['content'][0]['text'])
                self.assertEqual(payload['kernel_map']['path'], str(self.b))
                self.assertEqual(payload.get('hex', '07000000'), '07000000')

    def test_mcp_mismatch_refuses_target_read_for_both_tools(self):
        selected = self.selector(explicit=str(self.a))
        observer = gui.Gui(gui.Transport(get=lambda url: (self.get(url), {})))
        observer.kernel_maps = self.selector(explicit=str(self.a))
        observer.gshell_elf = str(self.b.with_name('kernel.elf'))
        with patch.object(server, 'SYMS', selected), patch.object(server, 'GUI', observer), \
                patch.object(server.emu, 'get', side_effect=self.get):
            for name, args in (
                ('emu_read_mem', {'addr': 'probe', 'len': 4}),
                ('emu_wait_mem', {'symbol': 'probe', 'op': 'eq', 'value': 7, 'timeout': 15})):
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    server._handle({'id': 1, 'method': 'tools/call',
                                    'params': {'name': name, 'arguments': args}})
                reply = json.loads(output.getvalue())['result']
                self.assertTrue(reply['isError'])
                self.assertIn('no matching kernel.map', reply['content'][0]['text'])


def mutate():
    source = SOURCE.read_text()
    cases = [
        ("if explicit:", "if False:", 'test_explicit_override_is_restricted_and_verified'),
        ("for sub in ('.', R1_SUBDIR)]", "for sub in ('.',)]", 'test_discovery_selects_r1_image'),
        ("or boot[36] != 1", "or False", 'test_guest_commit_crc_and_validity_are_required'),
        ("struct.unpack_from('<II', boot, 40) != (crc, size)", "False", 'test_image_crc_is_checked_independently'),
        ("or self.read(commit_addr, commit_size) != commit", "or False", 'test_guest_commit_crc_and_validity_are_required'),
        ("raw_crc != zlib.crc32(binary)", "False", 'test_elf_payload_crc_is_checked_independently'),
        ("elf.get(name, (None, 0))[0] != addr", "False", 'test_missing_and_stale_artifacts_fail_closed'),
    ]
    global SELECTOR
    failed = 0
    for old, new, case in cases:
        assert source.count(old) == 1, old
        namespace = dict(__file__=str(SOURCE), __package__='tools.np21w_mcp')
        exec(compile(source.replace(old, new), str(SOURCE), 'exec'), namespace)
        SELECTOR = namespace['VerifiedSymbols']
        result = unittest.TextTestRunner(stream=io.StringIO()).run(IdentityTests(case))
        killed = bool(result.failures) and not result.errors
        print('MUTATION %s: %s' % ('RED' if killed else 'MISSED', old))
        failed += not killed
    SELECTOR = symbols.VerifiedSymbols
    return failed


if __name__ == '__main__':
    with patch.dict(os.environ, {'OS32_KERNEL_MAP': ''}):
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(IdentityTests))
        rc = not result.wasSuccessful()
        if '--mutate' in sys.argv:
            rc += mutate()
    sys.exit(bool(rc))


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_np21w_symbols.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
