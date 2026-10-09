"""Offline GUI contract tests: synthetic memory, guest font, recorded screenshot.

No HTTP may escape a test double, even when a transport injection is removed.
"""
import contextlib
import importlib.util
import io
import json
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import urllib.parse

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.np21w_mcp import gui
from tools.np21w_mcp import gui_desc
from PIL import Image

SRC = ROOT / "tools/np21w_mcp/gui.py"
RECORDED = ROOT / 'tools/tests/fixtures/gui_about.png'


class Clock:
    def __init__(self):
        self.now = 0
        self.sleeps = []

    def sleep(self, n):
        self.sleeps.append(n)
        self.now += n

    def monotonic(self):
        return self.now

    time = monotonic


class Http:
    def __init__(self, status=None):
        self.clock = Clock()
        self.posts = []
        self.reads = []
        self.memory = {}
        self.status = status or {'scrn_ymax': 480, 'grph_disp': 1, 'wab_relay': 0}
        self.bmp = None
        self.source = 'pc98'

    def put(self, addr, data):
        self.memory.update({addr + i: b for i, b in enumerate(data)})

    def post(self, path, data):
        self.posts.append((path, data))
        return b'{"ok":true}'

    def get(self, path):
        self.reads.append(path)
        if path == '/api/status':
            return json.dumps(self.status).encode(), {}
        if path.startswith('/api/screenshot'):
            return self.bmp, {'X-Screen-Source': self.source}
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)
        assert query['space'] == ['phys']
        addr, size = int(query['addr'][0], 0), int(query['len'][0])
        assert 0 < size <= 1024
        data = bytes(self.memory[addr + i] for i in range(size))
        return json.dumps({'hex': data.hex()}).encode(), {}

    def transport(self):
        return gui.Transport(self.post, self.get, self.clock)


# A real nm -S shape, including local symbols and sizeof(g_slot)=6*64.
KERNEL_NM = '''00170001 B __bss_end
00160000 00000180 b g_slot
00150000 00002000 b g_ring
00152000 00000004 b g_head
00152004 00000004 b g_tail
00152008 00000004 b g_count
0015200c 00000004 B appslot_trim_epoch
00152010 00000004 T appslot_trim_done
'''
SHELL_NM = '''00600000 00000004 B gshell_trim_delivered
00600004 00000004 B gshell_trim_skipped_full
'''


def symbols(path):
    def nm(args, **kwargs):
        assert args[1:3] == ['-S', '--defined-only']
        return types.SimpleNamespace(stdout=SHELL_NM if path == 'shell' else KERNEL_NM)
    return gui.ElfSymbols(path, runner=nm)


def setup_state(http):
    for owner in range(6):
        http.put(0x160000 + owner * 64, struct.pack('<I', 1 if owner == 1 else 0))
    # Independent arithmetic from memmap.h: round bss, heap, KAPI, guard, GUI offset.
    base = 0x171000 + 0x2c000 + 0x1000 + 0x1000 + 0x28000
    for slot in range(4):
        http.put(base + slot * 0x4000, struct.pack('<HHIHHHH', 1, slot, 10 + slot, 2, 3, 4, 0))
    for addr, value in ((0x15200c, 17), (0x600000, 18), (0x600004, 19)):
        http.put(addr, struct.pack('<I', value))
    return base


def font_cache():
    # Read the generated guest .kcgfont, not a host font approximation.
    import lz4.block
    data = (ROOT / 'assets/fonts/ipaexg16.kcgfont').read_bytes()
    magic, size, flags, _ = struct.unpack('<4sIII', data[:16])
    assert magic == b'KCG1'
    payload = lz4.block.decompress(data[16:], uncompressed_size=size) if flags & 1 else data[16:]
    return payload[291588:295684]


def draw(font, text, height=480, origin=(13, 19), fg=(240, 30, 100), bg=(20, 170, 80)):
    im = Image.new('RGB', (640, height), bg)
    for i, char in enumerate(text):
        for row, bits in enumerate(font[ord(char)*16:(ord(char)+1)*16]):
            for col in range(8):
                im.putpixel((origin[0] + i*8 + col, origin[1] + row), fg if bits & (128 >> col) else bg)
    return im


class GuiTests(unittest.TestCase):
    def setUp(self):
        # Catch any accidentally unmocked HTTP before it can reach the emulator.
        self.guard = patch('urllib.request.urlopen', side_effect=AssertionError('LIVE HTTP FORBIDDEN'))
        self.guard.start()
        self.addCleanup(self.guard.stop)
        self.http = Http()
        self.g = gui.Gui(self.http.transport(), 'kernel', 'shell', symbols,
                         descriptor=gui.NO_DESCRIPTOR)

    def test_coordinates_heights_and_bounds(self):
        for status, height in (({'scrn_ymax': 400, 'grph_disp': 1}, 400),
                               ({'scrn_ymax': 480, 'grph_disp': 1}, 480),
                               ({'scrn_ymax': 400, 'grph_disp': 0, 'wab_relay': 1, 'wab_height': 480}, 480)):
            self.http.status = status
            self.g.mouse('move', 0, 0)
            self.assertEqual(self.http.posts[-1][1], {'ax': 0, 'ay': 0})
            self.g.mouse('click', 639, height - 1)
            self.assertEqual(self.http.posts[-3][1], {'ax': 65535, 'ay': 65535})
        for coords in ((640, 0, 480), (-1, 0, 400), (0, 480, 480), (0, 0, 1)):
            with self.assertRaises(ValueError):
                gui.pixel_to_absolute(*coords)
        self.g.mouse('drag', 1, 2, 3, 4)
        self.g.mouse('press', btn=2)
        self.g.mouse('release')
        self.g.mouse('off')
        self.assertEqual(self.http.posts[-1][1], {'abs': 'off'})

    def test_not_gui_refuses_mouse_and_type(self):
        self.http.status = {'scrn_ymax': 400, 'grph_disp': 0}
        for call in (lambda: self.g.mouse('click', 1, 2), lambda: self.g.type(text='foo'),
                     lambda: self.g.launch('/x', timeout=15)):
            with self.assertRaisesRegex(ValueError, 'GUI is not active'):
                call()
        self.assertEqual(self.http.posts, [])
        self.g.mouse('off')  # Cleanup also works after CUI transition.

    def test_key_chunks_escapes_and_encoding(self):
        self.g.type(text=r'abcdefghij\e\x01\\x41')
        self.assertEqual([p[1] for p in self.http.posts], [
            {'text': 'abcd'}, {'text': 'efgh'}, {'text': 'ij'}, {'seq': 'ESC'},
            {'seq': 'CTRL+A'}, {'text': '\\x41'}])
        self.assertEqual(self.http.clock.sleeps, [0.35]*6)
        requests = []
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self): return b'{"ok":true}'
        with patch('urllib.request.urlopen', side_effect=lambda req, **kw: requests.append(req) or Response()):
            gui.Transport._post('/api/key', {'seq': 'CTRL+ESC'})
        self.assertEqual(requests[0].data, b'seq=CTRL%2BESC')
        with self.assertRaises(ValueError):
            self.g.type(text=r'\x00')

    def test_nm_repeated_constants_and_unrelated_locals(self):
        output = KERNEL_NM + '00000023 a USER_DS\n00000023 a USER_DS\n00123400 00000004 b local\n00123500 00000004 b local\n'
        runner = lambda *a, **kw: types.SimpleNamespace(stdout=output)
        table = gui.ElfSymbols('mock', runner).read()
        self.assertEqual(table['USER_DS'], (0x23, 0))
        self.assertEqual(table['g_slot'], (0x160000, 0x180))
        with self.assertRaisesRegex(ValueError, 'ambiguous ELF symbol: local'):
            table['local']
        output += '00160004 00000180 b g_slot\n'
        with self.assertRaisesRegex(ValueError, 'ambiguous ELF symbol: g_slot'):
            gui.ElfSymbols('mock', runner).read()['g_slot']

    def test_state_symbols_stride_and_separate_tables(self):
        base = setup_state(self.http)
        result = self.g.wm_state()
        self.assertEqual(result['version'], 1)
        self.assertEqual(result['front'], 0)
        self.assertEqual(result['windows'], [])
        self.assertEqual([s['slot'] for s in result['slots']], [0, 1, 2, 3])
        self.assertEqual(result['slots'][2], {'slot': 2, 'proto_version': 1, 'flags': 2,
                         'seq': 12, 'ring_head': 2, 'ring_tail': 3, 'dropped': 4, 'reserved': 0})
        self.assertEqual(result['kernel'], [{'owner': i, 'state': 1 if i == 1 else 0} for i in range(1,6)])
        self.assertNotIn('owner', result['slots'][0])
        self.assertEqual(result['counters'], {'appslot_trim_epoch': 17, 'gshell_trim_delivered': 18,
                                            'gshell_trim_skipped_full': 19})
        self.assertIn('/api/mem?addr=0x%x&len=16&space=phys' % base, self.http.reads)
        calls = []
        self.g.descriptor = types.SimpleNamespace(wm_state=lambda observer: calls.append(observer) or
            {'front': 4, 'windows': [{'id':91, 'owner':4, 'title':'demo'}],
             'slots': [{'slot':0, 'owner':4, 'used':True}]})
        v2 = self.g.wm_state()
        self.assertEqual(v2['version'], 2)
        self.assertEqual(v2['front'], 4)
        self.assertEqual(v2['slots'], result['slots'])
        self.assertEqual(v2['wm_slots'], [{'slot':0, 'owner':4, 'used':True}])
        self.assertEqual(calls, [self.g])

    def test_descriptor_failures_retain_v1_and_reason(self):
        setup_state(self.http)
        baseline = self.g.wm_state()
        for error in (gui_desc.DescriptorError('invalid magic'), OSError('read failed')):
            self.g.descriptor = types.SimpleNamespace(wm_state=lambda observer: None)
            with patch.object(self.g.descriptor, 'wm_state', side_effect=error):
                actual = self.g.wm_state()
            self.assertEqual(actual.pop('v2_error'), '%s: %s' % (type(error).__name__, error))
            self.assertEqual(actual, baseline)
        self.g.descriptor = None
        with patch.object(gui.importlib, 'import_module', side_effect=ModuleNotFoundError('no reader')):
            actual = self.g.wm_state()
        self.assertIn('no reader', actual.pop('v2_error'))
        self.assertEqual(actual, baseline)
        self.g.descriptor = gui.NO_DESCRIPTOR
        with patch.object(gui.importlib, 'import_module', side_effect=AssertionError('v1 must not import')):
            self.assertEqual(self.g.wm_state(), baseline)

    def test_v2_real_descriptor_fixture(self):
        # Reuse the roundtrip image compiled from actual gshell Rust types.
        from tools.tests import test_gui_desc as fixture
        setup_state(self.http)
        baseline = self.g.wm_state()
        with tempfile.TemporaryDirectory(prefix='gui-integration-') as temp:
            out = Path(temp)
            rlib = fixture.os32api_host.build(out)
            exe, table, blobs, read = fixture.build_fixture(fixture.GSHELL, out, rlib)
            fixture.roundtrip(exe, table, blobs, read)
            original_mem = self.g.mem
            def memory(address, size):
                if any(start <= address < start + length for start, length in table.values()):
                    return read(address, size)
                return original_mem(address, size)
            self.g.mem = memory
            self.g.gshell_elf = exe
            # v1 counters keep their synthetic shell symbols; the v2 reader
            # independently resolves the fixture ELF with its own real nm.
            self.g.symbol_factory = lambda path: symbols('shell' if path == exe else path)
            self.g.descriptor = None  # Exercise automatic import and the real adapter.
            actual = self.g.wm_state()
            self.assertEqual(actual['version'], 2)
            self.assertNotIn('v2_error', actual)
            self.assertEqual(actual['front'], fixture.EXPECTED['front'])
            self.assertEqual(actual['windows'], fixture.EXPECTED['windows'])
            self.assertEqual(actual['trim_sent'], fixture.EXPECTED['trim_sent'])
            self.assertEqual(actual['wm_slots'], [dict(s, slot=s['n'], used=True)
                                                for s in fixture.EXPECTED['slots']])
            for key in ('slots', 'kernel', 'counters'):
                self.assertEqual(actual[key], baseline[key])
            blobs['desc'] = b'\0' * len(blobs['desc'])
            rejected = self.g.wm_state()
            self.assertIn('DescriptorError: descriptor magic mismatch', rejected.pop('v2_error'))
            self.assertEqual(rejected, baseline)

    def states(self, sequence, version=1):
        sequence = list(sequence)
        def observe():
            state = sequence.pop(0) if len(sequence) > 1 else sequence[0]
            return {'version': version, 'kernel': [{'owner':i, 'state':state if i == 4 else 0} for i in range(2,6)],
                    'slots': [],
                    'wm_slots': [{'slot':0, 'used':True, 'owner':4}] if version == 2 else [],
                    'windows': [{'id':91, 'owner':4, 'title':'demo'}] if version == 2 and state != 0 else [],
                    'front':4 if version == 2 and state != 0 else 0}
        self.g.wm_state = observe

    def test_launch_success_owner_not_shm(self):
        self.states([0, 1, 2])
        result = self.g.launch('/usr/bin/demo.bin', timeout=15)
        self.assertTrue(result['ok'])
        self.assertEqual((result['owner'], result['slot']), (4, None))
        self.assertEqual([p[1]['seq'] for p in self.http.posts if 'seq' in p[1]],
                         ['CTRL+ESC','DOWN','DOWN','RETURN','RETURN'])
        self.states([0, 2], version=2)
        result = self.g.launch('/usr/bin/demo.bin', timeout=15)
        self.assertEqual((result['owner'], result['slot'], result['window_id'], result['title']), (4,0,91,'demo'))

    def test_launch_v2_requires_new_owner_window(self):
        self.states([0, 2], version=2)
        observe = self.g.wm_state
        def without_window():
            last = observe()
            last['windows'] = []
            return last
        self.g.wm_state = without_window
        self.assertFalse(self.g.launch('/x', timeout=15)['ok'])

    def test_launch_explicit_mouse_fallback(self):
        self.states([0, 2])
        result = self.g.launch('/x', timeout=15, fallback=True)
        self.assertTrue(result['ok'])
        self.assertEqual(sum(p[0] == '/api/mouse' for p in self.http.posts), 6)
        self.assertNotIn({'seq':'CTRL+ESC'}, [p[1] for p in self.http.posts])

    def test_launch_timeout_death_invalid_path_and_full(self):
        for sequence in ([0,0], [0,1,0], [0,3], [0,4], [0,1,0,2]):
            self.states(sequence)
            result = self.g.launch('/x', timeout=15)
            self.assertFalse(result['ok'])
            self.assertIn('last', result)
            self.assertIn('OP_WAIT', result['reason'])
        for path in ('relative', '/has space', '/'+'a'*255, '/あ', '/bad\x00'):
            with self.assertRaises(ValueError):
                self.g.launch(path)
        self.g.wm_state = lambda: {'kernel':[{'owner':i,'state':2} for i in range(2,6)]}
        self.assertEqual(self.g.launch('/x')['reason'], 'no free kernel owner')

    def test_wait_mem_timeout_last_and_success(self):
        self.http.put(0x15200c, struct.pack('<I', 7))
        self.assertEqual(self.g.wait_mem('appslot_trim_epoch', 'eq', 8, 15), {'ok':False, 'last':7})
        self.assertEqual(self.g.wait_mem('appslot_trim_epoch', 'ge', 7), {'ok':True, 'last':7})
        self.http.put(0x600000, struct.pack('<I', 9))
        self.assertTrue(self.g.wait_mem('gshell:gshell_trim_delivered','ne',8)['ok'])
        for timeout in (0, 14, float('inf'), float('nan')):
            with self.assertRaises(ValueError): self.g.wait_mem('appslot_trim_epoch','eq',7,timeout)
        self.assertTrue(all(n == 0.5 for n in self.http.clock.sleeps))

    def test_wait_text_both_sources_timeout_and_split(self):
        self.g.screen_text = lambda: {'lines':['abc'], 'unknown_count':0}
        self.assertEqual(self.g.wait_text('xyz','screen',15),
                         {'ok':False, 'last':{'lines':['abc'],'unknown_count':0}})
        self.assertTrue(self.g.wait_text('abc','screen')['ok'])
        chunks = iter(['fixt','ure PASS'])
        self.g.consink = lambda since=None: {'text':next(chunks),'since':1}
        self.assertTrue(self.g.wait_text('fixture PASS')['ok'])

    def test_consink_wrap_since_and_snapshot(self):
        setup_state(self.http)
        data = b'\x01\x0f\x02AB\x02\x03\x0c\x22\x04\x05'
        ring = bytearray(8192)
        tail = 8190
        for i,b in enumerate(data): ring[(tail+i)%8192] = b
        self.http.put(0x150000, ring)
        for addr,v in ((0x152000,(tail+len(data))%8192),(0x152004,tail),(0x152008,len(data))):
            self.http.put(addr,struct.pack('<I',v))
        result = self.g.consink()
        self.assertEqual(result['records'], [{'type':1,'color':15,'text':'AB'},{'type':2},
                                             {'type':3,'x':12,'y':34},{'type':4,'owner':5}])
        self.assertEqual(result['text'],'AB[EXIT id=5]\n')
        self.assertFalse(result['lost'])
        self.assertEqual(self.g.consink(since=3)['records'][0], {'type':2})
        self.assertEqual(self.g.consink(since=result['since'])['records'], [])
        self.assertTrue(self.g.consink(since=8191)['lost'])
        self.assertEqual(self.g.consink(grep='EXIT')['lines'],['AB[EXIT id=5]'])
        for payload in (b'\x01', b'\x01\x01\x03ab', b'\x03\x01', b'\x04', b'\xff'):
            with self.assertRaises(ValueError): gui.decode_ring(payload,0,0,len(payload))

    def test_legacy_transport_injection_mutant_caught_without_http(self):
        path = ROOT / 'tools/gui_gate.py'
        spec = importlib.util.spec_from_file_location('gate_test',path)
        gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)
        gate.post, gate.get, gate.time = self.http.post, self.http.get, self.http.clock
        mouse = gate.Mouse(480)
        # Module attributes may change even after a Mouse has been created.
        gate.post = lambda path, data: self.http.post(path, data)
        gate.key(text='abcde')
        mouse.click(639,479)
        self.assertEqual(len(self.http.posts),5)
        # Removing just post injection must fall into our HTTP guard, never pass.
        with patch.object(gate, '_transport', return_value=gui.Transport(get=gate.get,clock=gate.time)):
            with self.assertRaisesRegex(AssertionError,'LIVE HTTP FORBIDDEN'):
                gate.key(text='x')
            with self.assertRaisesRegex(AssertionError,'LIVE HTTP FORBIDDEN'):
                gate.Mouse(480).click(1,2)


class OcrTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.font = font_cache()

    def test_all_95_ascii_400_480_color_offset_palette_rgb(self):
        # Space is tested within each exact grid; literal '?' is not unknown.
        alphabet = ''.join(chr(c) for c in range(32,127))
        total = ambiguous = 0
        groups = {}
        for code in range(32, 127):
            signature = self.font[code*16:(code+1)*16]
            groups[signature] = groups.get(signature, '') + chr(code)
        for height, origin, fg, bg in ((400,(7,13),(0,0,0),(255,255,255)),
                                     (480,(19,23),(240,30,100),(20,170,80))):
            for start in (0,48):
                text = alphabet[start:start+48]
                im = draw(self.font,text,height,origin,fg,bg)
                for mode in ('RGB','P'):
                    source = im if mode == 'RGB' else im.convert('P',palette=Image.Palette.ADAPTIVE,colors=16)
                    stream=io.BytesIO(); source.save(stream,format='BMP',bits=4 if mode=='P' else 24)
                    result=gui.read_ascii(Image.open(io.BytesIO(stream.getvalue())),self.font,
                                          [*origin,len(text)*8,16])
                    expected, collisions = [], []
                    for col, char in enumerate(text):
                        candidates = groups[self.font[ord(char)*16:(ord(char)+1)*16]]
                        expected.append(char if len(candidates) == 1 else '?')
                        if len(candidates) > 1:
                            collisions.append({'pos':[0,col], 'chars':candidates})
                    self.assertEqual(result['lines'],[''.join(expected).rstrip()])
                    self.assertEqual(result['ambiguous'],collisions)
                    self.assertEqual(result['unknown_count'],0)
                    for char in text:
                        candidates = groups[self.font[ord(char)*16:(ord(char)+1)*16]]
                        probe = gui.read_ascii(draw(self.font,char), self.font, [13,19,8,16])
                        for candidate in alphabet:
                            self.assertEqual(gui.screen_matches(re.compile('^'+re.escape(candidate)+'$'), probe),
                                             candidate in candidates if char != ' ' else False)
                    total += len(text)
                    ambiguous += len(collisions)
        print('OCR synthetic: %d ASCII cells, substitutions=0, unknown=0, ambiguous=%d (400/480, 4bit/24bpp)' % (total,ambiguous))

    def test_auto_origin_and_unknown(self):
        im=draw(self.font,'ABC?DEF',origin=(13,19))
        # Replace D with a two-color pattern absent from the guest font.
        for row in range(16):
            for col in range(8):
                im.putpixel((13+4*8+col,19+row),(240,30,100) if col == row % 8 else (20,170,80))
        result=gui.read_ascii(im,self.font,[13,19,56,16])
        self.assertEqual(result['lines'],['ABC??EF'])
        self.assertEqual(result['unknown_count'],1)
        auto=gui.read_ascii(im,self.font)
        self.assertIn('ABC??EF',auto['lines'])
        self.assertEqual(auto['unknown_count'],1)

    def test_runtime_collision_groups_and_screen_regex(self):
        font = bytearray(self.font)
        # Runtime computation must handle an arbitrary group, including '?'.
        for char in '?AZ':
            font[ord(char)*16:(ord(char)+1)*16] = self.font[66*16:67*16]
        result = gui.read_ascii(draw(font,'?ABZ'), font, [13,19,32,16])
        self.assertEqual(result['lines'], ['????'])
        self.assertEqual(result['unknown_count'], 0)
        self.assertEqual(result['ambiguous'], [{'pos':[0,i],'chars':'?ABZ'} for i in range(4)])
        sample = {'lines':['?x?','?'], 'ambiguous':[
            {'pos':[0,0],'chars':'Il'}, {'pos':[0,2],'chars':'Il'}, {'pos':[1,0],'chars':'Il'}]}
        for regex, expected in ((r'^Ixl\nl$',True), (r'^[Il]x[Il]\nI$',True),
                                (r'^[^Il]',False), (r'^J',False), (r'(I)x\1',True),
                                (r'(?<=Ix)l',True), (r'IxI(?=\nI)',True)):
            self.assertEqual(gui.screen_matches(re.compile(regex),sample),expected,regex)
        # Bounded missing marker on many collisions must finish without 2**80 expansions.
        many = {'lines':['?'*80], 'ambiguous':[{'pos':[0,i],'chars':'Il'} for i in range(80)]}
        self.assertFalse(gui.screen_matches(re.compile('PASS'),many))
        g = gui.Gui(Http().transport())
        g.screen_text = lambda: sample
        self.assertTrue(g.wait_text(r'^lxl\nI$','screen',15)['ok'])
        timed = g.wait_text('J','screen',15)
        self.assertEqual(timed, {'ok':False,'last':sample})

    def test_recorded_known_lines(self):
        result=gui.read_ascii(Image.open(RECORDED),self.font)
        # Visually transcribed About window only. Exclude the taskbar Japanese
        # region (y>=456); full-screen OCR does not have a ground-truth claim.
        expectations = [('About OS32', []),
                        ('CPU: ?nte? 386+ (Protected Mode + Paging)', [(5,'Il'),(9,'Il')]),
                        ('AP?: v72', [(2,'Il')]),
                        ('PC-9801 OS32 v2.0 (Ring3 Native)', [])]
        for expected, collisions in expectations:
            row = result['lines'].index(expected)
            self.assertLess(result['origins'][row][1],456)
            self.assertEqual([(a['pos'][1],a['chars']) for a in result['ambiguous'] if a['pos'][0]==row],
                             collisions)
        self.assertTrue(gui.screen_matches(re.compile(r'CPU: Intel 386\+'),result))
        self.assertTrue(gui.screen_matches(re.compile('API: v72'),result))
        self.assertFalse(gui.screen_matches(re.compile('APJ: v72'),result))
        print('OCR recorded about.png:',json.dumps(result,ensure_ascii=False))

    def test_screen_transport_font_cache_and_png_evidence(self):
        for source in ('pc98','wab'):
            http=Http();http.source=source
            im=draw(self.font,'Evidence',origin=(13,19))
            stream=io.BytesIO();im.save(stream,format='BMP');http.bmp=stream.getvalue()
            http.put(0x1000+291588,self.font)
            g=gui.Gui(http.transport())
            with tempfile.TemporaryDirectory() as tmp:
                out=str(Path(tmp)/'proof.png')
                with patch('urllib.request.urlopen',side_effect=AssertionError('LIVE HTTP FORBIDDEN')):
                    result=g.screen_text([13,19,64,16],out)
                self.assertEqual(result['lines'],['Evidence'])
                self.assertEqual(result['source'],source)
                self.assertEqual(Image.open(out).format,'PNG')
                self.assertEqual(result['unknown_count'],0)


def mutate():
    global gui
    original_gui = gui
    mutations = [
        ('px * 65535 + 319', 'px * 65534 + 319', 'test_coordinates_heights_and_bounds'),
        ('payload[i:i + 4]', 'payload[i:i + 8]', 'test_key_chunks_escapes_and_encoding'),
        ('time.sleep(0.35)', 'time.sleep(0.3)', 'test_key_chunks_escapes_and_encoding'),
        ("states[owner] != app('APP_STATE_PARKED')", "states[owner] != app('APP_STATE_RUNNING')", 'test_launch_success_owner_not_shm'),
        ("'slot': None, 'last': last", "'slot': owner - 2, 'last': last", 'test_launch_success_owner_not_shm'),
        ("'ok': False, 'last': last", "'ok': True, 'last': last", 'test_wait_mem_timeout_last_and_success'),
        ('owner * stride', 'owner * 4', 'test_state_symbols_stride_and_separate_tables'),
        ("'front': 0, 'windows': []", "'front': None, 'windows': []", 'test_state_symbols_stride_and_separate_tables'),
        ("state['wm_slots'] = state.pop('slots')", "state['wm_slots'] = state['slots']", 'test_state_symbols_stride_and_separate_tables'),
        ("result['v2_error'] = '%s: %s' % (type(exc).__name__, exc)", "result['v2_error'] = ''", 'test_descriptor_failures_retain_v1_and_reason'),
        ("glyphs.get(signature, '') + chr(code)", "chr(code)", 'test_runtime_collision_groups_and_screen_regex'),
        ("elif len(candidates) > 1:", "elif False:", 'test_runtime_collision_groups_and_screen_regex'),
    ]
    failures = 0
    source = SRC.read_text()
    with tempfile.TemporaryDirectory(prefix='gui-mutants-') as tmp:
        for index, (old, new, case) in enumerate(mutations):
            if old not in source:
                print('MUTATION NOT APPLICABLE:', old); failures += 1; continue
            path = Path(tmp) / ('mut%d.py' % index)
            path.write_text(source.replace(old,new,1))
            spec = importlib.util.spec_from_file_location('gui_mutant', path)
            gui = importlib.util.module_from_spec(spec);spec.loader.exec_module(gui)
            gui.ROOT = ROOT
            result = unittest.TextTestRunner(stream=io.StringIO()).run(
                unittest.TestSuite([(OcrTests if case.startswith('test_runtime_') else GuiTests)(case)]))
            killed = not result.wasSuccessful()
            print('MUTATION %d %s: %s' % (index+1,'RED' if killed else 'MISSED',old))
            failures += not killed
    gui = original_gui
    return failures


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__]))
    rc = not result.wasSuccessful()
    if '--mutate' in sys.argv:
        rc += mutate()
    sys.exit(bool(rc))
