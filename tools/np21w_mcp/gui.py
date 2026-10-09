"""Host GUI observations and input. No network activity at import time.

Kernel owners and GUI SHM slots are separate namespaces in v1. All I/O and
clock access is injected through Transport; the legacy gui_gate wrappers pass
its module attributes on each call, so monkeypatches remain effective.
"""
import importlib
import io
import json
import operator
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
BASE = os.environ.get("NP21W_AIDEBUG_URL", "http://127.0.0.1:8025")
NO_DESCRIPTOR = object()  # Explicit v1 observation, without importing gui_desc.


class Transport:
    def __init__(self, post=None, get=None, clock=time):
        self.post = post or self._post
        self.get = get or self._get
        self.clock = clock

    @staticmethod
    def _get(path, timeout=20):
        with urllib.request.urlopen(BASE + path, timeout=timeout) as reply:
            return reply.read(), dict(reply.headers)

    @staticmethod
    def _post(path, data, timeout=20):
        body = urllib.parse.urlencode(data).encode()
        request = urllib.request.Request(BASE + path, data=body, method="POST")
        with urllib.request.urlopen(request, timeout=timeout) as reply:
            payload = reply.read()
        result = json.loads(payload)
        if result.get("ok") is not True:
            raise ValueError("input rejected: %s" % result)
        return payload


def start_row(h, r):
    """Root-menu row centre derived from the running shell's source layout."""
    source = (ROOT / 'userland/gshell/src/startmenu.rs').read_text()
    taskbar = (ROOT / 'userland/gshell/src/taskbar.rs').read_text()
    def constant(text, name):
        return int(re.search(r'\bconst ' + name + r': \w+ = (\d+);', text)[1])
    items = constant(source, 'ROOT_ITEMS')
    item_h = constant(source, 'ITEM_H')
    border = constant(source, 'BORDER')
    taskbar_h = constant(taskbar, 'TASKBAR_H')
    if not 0 <= r < items:
        raise ValueError('Start menu row outside ROOT_ITEMS')
    top = h - taskbar_h - (items * item_h + border * 2)
    return (82, top + border + item_h * r + item_h // 2)


def pixel_to_absolute(px, py, h):
    if h <= 1 or not (0 <= px <= 639 and 0 <= py < h):
        raise ValueError("pixel coordinates outside 640 x %s" % h)
    ax = (px * 65535 + 319) // 639
    ay = (py * 65535 + (h - 1) // 2) // (h - 1)
    return ax, ay


default_transport = Transport()

# 逃がし記法 (票 tools/TASK_KEY_INJECT.md §2-2)。展開はここ (台本の側) でやり、
# `/api/key` の `text=` の意味は一切変えない。`text=` に `\` を載せると今までどおり
# YEN キー (0x0d) が飛ぶ — 既存の台本と emu_agent はそのまま動く (受入 K2 / K6)。
_ESC_CHR = {"e": 0x1b, "n": 0x0a, "r": 0x0d, "t": 0x09, "b": 0x08}
# キー名で送るバイト。0x01〜0x1a の残りは CTRL+英字 で作る。
_ESC_SEQ = {0x08: "BS", 0x09: "TAB", 0x0a: "RETURN", 0x0d: "RETURN",
            0x1b: "ESC", 0x7f: "DEL"}
_HEX = "0123456789abcdefABCDEF"


def _byte_to_step(b):
    """1 バイトを注入 1 手 ("text" か "seq") に落とす。

    PC-98 のキーボードで作れないバイト (0x00、0x1c〜0x1f、0x80 以上) は
    黙って捨てずに ValueError にする — 落ちたことに気付かないほうが困る ([V4])。"""
    if b in _ESC_SEQ:
        return ("seq", _ESC_SEQ[b])
    if 0x01 <= b <= 0x1a:
        return ("seq", "CTRL+" + chr(ord("A") + b - 1))   # 0x01=CTRL+A 〜 0x1a=CTRL+Z
    if 0x20 <= b <= 0x7e:
        return ("text", chr(b))
    raise ValueError("0x%02x は PC-98 のキー注入では作れない "
                     "(0x00 / 0x1c〜0x1f / 0x80 以上)。かなや漢字は FEP 経由で" % b)


_ESC_RE = re.compile(r"\\\\|\\x[0-9A-Fa-f]{2}|\\.", re.S)


def expand_escapes(text):
    """`\\xNN` `\\e` `\\n` `\\r` `\\t` `\\b` `\\\\` を注入の手順に展開する。

    返り値は ("text", 文字列) / ("seq", コード) の列。隣り合う文字はまとめて返すので、
    呼び手は text の塊だけを 4 文字ずつに切ればよい (**記法の途中では切れない**)。

    **`\\\\` を最優先で食う**のが肝 (`_ESC_RE` の並び)。`\\\\x41` は
    「`\\` 1 個 + 文字列 `x41`」であって「逃がした 0x41」ではない。左から順に
    食わないとこの区別が壊れる。"""
    steps = []
    buf = []
    pos = 0

    def flush():
        if buf:
            steps.append(("text", "".join(buf)))
            del buf[:]

    for m in _ESC_RE.finditer(text):
        buf.extend(text[pos:m.start()])
        tok = m.group(0)
        pos = m.end()
        if tok == "\\\\":
            buf.append("\\")            # YEN キー = PC-98 の `\`
            continue
        if tok[1] == "x":
            step = _byte_to_step(int(tok[2:], 16))
        elif tok[1] in _ESC_CHR:
            step = _byte_to_step(_ESC_CHR[tok[1]])
        else:
            raise ValueError("未知の逃がし記法 `%s`" % tok)
        if step[0] == "text":
            buf.append(step[1])
        else:
            flush()
            steps.append(step)
    tail = text[pos:]
    if tail.endswith("\\"):
        raise ValueError("末尾が単独の `\\`。`\\` 自身は `\\\\` と書く")
    buf.extend(tail)
    flush()
    return steps


def key(seq=None, text=None, escapes=True, transport=None, hold=None):
    """文字列は 4 文字ずつ送る。raw リングは 32 エントリ (make+break で 1 文字 2 本) しか
    無く、長い text を一度に注入すると後ろが落ちる (2026-09-06: Run... のパスが
    `/usr/bin/gui_dem` で切れた)。8 文字 / 0.3 秒でも 9801 (planar) でアプリ実行中は
    WM の drain が追いつかず 2 文字落ちた (2026-09-07: `v12_api_test.n`) ので 4 文字に。

    `\\xNN` などの逃がし記法は**既定で有効** (2026-09-18)。`\\` 自身を送るなら `\\\\` と書く。
    素通しにしたいときだけ `escapes=False`。ツリー内に `text=` で `\\` を送る利用者は
    **調査の結果 1 件も無かった**ので、既定を有効にしても既存の台本は壊れない。
    制御文字は `seq=` の和音に化けるので、**4 文字の分割が記法の途中で切れることは無い**。"""
    if hold is not None:
        if type(hold) is not int or not 0 <= hold <= 5000:
            raise ValueError('hold must be an integer from 0 to 5000 milliseconds')
        if text is not None or not seq or not seq.strip() or ',' in seq:
            raise ValueError('hold requires a single seq chord without text')
    post = (transport or default_transport).post
    time = (transport or default_transport).clock
    if text is not None:
        steps = expand_escapes(text) if escapes else [("text", text)]
        for kind, payload in steps:
            if kind == "seq":
                post("/api/key", {"seq": payload})
                time.sleep(0.35)
                continue
            i = 0
            while i < len(payload):
                post("/api/key", {"text": payload[i:i + 4]})
                time.sleep(0.35)
                i += 4
    if seq is not None:
        data = {"seq": seq}   # urlencode が + を %2B にする
        if hold is not None:
            data['hold'] = hold
        post("/api/key", data)



def gui_entered(st, h):
    """`status()` の値から、GUI (gshell) の画面に --h ラインで居るかを判定する。

    98 の GDC / PEGC の GUI は --h ラインのグラフィック表示 (`grph_disp == 1`)。CUI は
    400 ラインでグラフィックを消している (R2 の予備調査: CUI は `scrn_ymax 400 grph_disp 0`、
    PEGC の GUI は `scrn_ymax 480 grph_disp 1`)。9801 (--h 400) では scrn_ymax が
    CUI と同じなので、grph_disp が決め手になる。
    Cirrus の GUI は WAB 中継 (`wab_relay == 1`) で、画面高は `wab_height`。このとき 98 の
    表示レジスタは CUI と同じ `scrn_ymax 400 grph_disp 0` のまま (2026-09-29 夕の R2)。"""
    if st.get("scrn_ymax") == h and st.get("grph_disp") == 1:
        return True
    return st.get("wab_relay") == 1 and st.get("wab_height") == h


def gui_height(st):
    """GUI に居るなら実際の画面高、居なければ None (`back_to_cui` が使う)。

    `gui_entered` と同じ規則: WAB 中継中 (Cirrus) は `wab_height`、そうでなく
    `grph_disp == 1` なら `scrn_ymax`。Cirrus では `scrn_ymax` が 400 のままなので、
    それで座標を作ると Start に当たらない。値が無いときは 0 (呼び手が --h で補う)。"""
    if st.get("wab_relay") == 1:
        return st.get("wab_height") or 0
    if st.get("grph_disp") == 1:
        return st.get("scrn_ymax") or 0
    return None



class Mouse:
    def __init__(self, h, transport=None):
        self.h = h
        self.transport = transport or default_transport

    def move(self, px, py, settle=0.4):
        ax, ay = pixel_to_absolute(px, py, self.h)
        self.transport.post("/api/mouse", {"ax": ax, "ay": ay})
        self.transport.clock.sleep(settle)

    def press(self, btn=1):
        self.transport.post("/api/mouse", {"btn": btn})
        self.transport.clock.sleep(0.4)

    def release(self):
        self.transport.post("/api/mouse", {"btn": 0})
        self.transport.clock.sleep(0.6)

    def click(self, px, py, btn=1):
        self.move(px, py)
        self.press(btn)
        self.release()

    def drag(self, x0, y0, x1, y1, mid=None):
        self.move(x0, y0)
        self.press()
        for (mx, my) in (mid or []):
            self.move(mx, my)
        self.move(x1, y1)
        self.release()

    def off(self):
        self.transport.post("/api/mouse", {"abs": "off"})



class ElfValues(dict):
    def __getitem__(self, name):
        value = super().__getitem__(name)
        if value is None:
            raise ValueError('ambiguous ELF symbol: ' + name)
        return value

    def get(self, name, default=None):
        return self[name] if name in self else default


class ElfSymbols:
    """Resolve every observation from fresh nm -S output, including local names."""
    def __init__(self, path, runner=subprocess.run):
        self.path, self.runner = str(path), runner

    def read(self):
        nm = os.environ.get('OS32_NM') or os.path.join(
            os.environ.get('CROSS_DIR', '/home/hight/opt/cross'), 'bin/i386-elf-nm')
        result = self.runner([nm, '-S', '--defined-only', self.path],
                             capture_output=True, text=True, check=True)
        table = ElfValues()
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) not in (3, 4):
                continue
            addr = int(fields[0], 16)
            size = int(fields[1], 16) if len(fields) == 4 else 0
            name = fields[-1]
            value = (addr, size)
            # Assemblers repeat absolute constants; unrelated local symbols
            # may collide. Reject a differing definition only when requested.
            if name in table and dict.__getitem__(table, name) != value:
                table[name] = None
            else:
                table[name] = value
        return table


def c_constants(path, extra=None):
    """Evaluate the numeric #define subset used by the memory/slot headers.

    No execution of C or arbitrary Python: only integer arithmetic AST nodes.
    The dynamic __bss_end is supplied from the current kernel ELF.
    """
    import ast
    source = re.sub(r'/\*.*?\*/', '', Path(path).read_text(), flags=re.S)
    source = source.replace('\\\n', '')
    definitions = dict(re.findall(r'^\s*#define\s+(\w+)\s+([^\n]+)', source, re.M))
    cache = dict(extra or {})
    ops = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
           ast.BitAnd: operator.and_, ast.BitOr: operator.or_, ast.LShift: operator.lshift,
           ast.RShift: operator.rshift}

    def visit(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name):
            return resolve(node.id)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            return ~visit(node.operand)
        if isinstance(node, ast.BinOp) and type(node.op) in ops:
            return ops[type(node.op)](visit(node.left), visit(node.right))
        raise ValueError('unsupported constant expression')

    def resolve(name):
        if name not in cache:
            expr = re.sub(r'\b(0x[0-9A-Fa-f]+|[0-9]+)[uUlL]+\b', r'\1', definitions[name])
            expr = re.sub(r'\(u32\)|&(?=__bss_end)', '', expr)
            cache[name] = visit(ast.parse(expr.strip(), mode='eval').body)
        return cache[name]
    return resolve


def c_word_offsets(path, name, through):
    """Derive a struct's 32-bit scalar prefix from its C declaration.

    Like c_constants, accept only the subset needed here and fail closed if
    the source layout changes to a type/declarator this reader cannot handle.
    AppSlot's prefix is int/u32 on the target i386 ABI, both four bytes.
    """
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', Path(path).read_text(), flags=re.S)
    match = re.search(r'\btypedef\s+struct\s*\{([^{}]*)\}\s*' + re.escape(name) + r'\s*;', source)
    if match is None:
        raise ValueError('unsupported C struct layout: ' + name)
    offsets = {}
    for declaration in match[1].split(';'):
        field = re.fullmatch(r'\s*(?:int|u32)\s+(\w+)\s*', declaration)
        if field is None:
            raise ValueError('unsupported C field layout: ' + declaration.strip())
        offsets[field[1]] = len(offsets) * struct.calcsize('<I')
        if field[1] == through:
            return offsets
    raise ValueError('missing C field layout: ' + through)


class Gui:
    def __init__(self, transport=None, kernel_elf=None, gshell_elf=None,
                 symbol_factory=ElfSymbols, descriptor=None):
        self.transport = transport or default_transport
        self.kernel_elf = kernel_elf or os.environ.get(
            'OS32_KERNEL_ELF', '/home/hight/os32-v3/build/out/kernel.elf')
        self.gshell_elf = gshell_elf or os.environ.get(
            'OS32_GSHELL_ELF', '/home/hight/os32-v3/userland/gshell.elf')
        self.symbol_factory = symbol_factory
        self.descriptor = descriptor

    def status(self):
        return json.loads(self.transport.get('/api/status')[0])

    def require_gui(self):
        height = gui_height(self.status())
        if height is None or height <= 1:
            raise ValueError('GUI is not active; CUI uses emu_cmd')
        return height

    def mouse(self, action, x=None, y=None, x1=None, y1=None, btn=1):
        if action == 'off':
            Mouse(480, self.transport).off()
            return {'ok': True}
        mouse = Mouse(self.require_gui(), self.transport)
        if action in ('move', 'click', 'drag'):
            if x is None or y is None:
                raise ValueError('x and y are required for ' + action)
            pixel_to_absolute(x, y, mouse.h)
        if btn not in (1, 2):
            raise ValueError('btn must be 1 or 2')
        if action == 'drag':
            if x1 is None or y1 is None:
                raise ValueError('x1 and y1 are required for drag')
            pixel_to_absolute(x1, y1, mouse.h)
            mouse.drag(x, y, x1, y1)
        elif action == 'move':
            mouse.move(x, y)
        elif action == 'click':
            mouse.click(x, y, btn)
        elif action == 'press':
            mouse.press(btn)
        elif action == 'release':
            mouse.release()
        else:
            raise ValueError('unknown mouse action: ' + action)
        return {'ok': True}

    def type(self, text=None, seq=None, escapes=True, hold=None):
        self.require_gui()
        if text is None and seq is None:
            raise ValueError('text or seq is required')
        if text is not None and any(ord(c) > 127 or ord(c) < 32 for c in text):
            raise ValueError('type accepts ASCII; use escape notation for controls')
        key(seq=seq, text=text, escapes=escapes, transport=self.transport, hold=hold)
        return {'ok': True}

    def mem(self, address, length):
        out = bytearray()
        while length:
            size = min(length, 1024)
            result = json.loads(self.transport.get(
                '/api/mem?addr=0x%x&len=%d&space=phys' % (address, size))[0])
            chunk = bytes.fromhex(result['hex'])
            if len(chunk) != size:
                raise ValueError('short physical memory read')
            out.extend(chunk)
            address += size
            length -= size
        return bytes(out)

    def word(self, address):
        return struct.unpack('<I', self.mem(address, 4))[0]

    def symbols(self):
        return (self.symbol_factory(self.kernel_elf).read(),
                self.symbol_factory(self.gshell_elf).read())

    def wm_state(self):
        """Return SHM slots and kernel owners; front is the foreground owner or 0.

        v2 adds windows and wm_slots without replacing SHM slots. Reader
        failures retain v1 observations and their reason in v2_error.
        """
        kernel, shell = self.symbols()
        mem = c_constants(ROOT / 'include/memmap.h', {'__bss_end': kernel['__bss_end'][0]})
        app = c_constants(ROOT / 'exec/appslot.h')
        count = app('APP_SLOT_COUNT')
        address, size = kernel['g_slot']
        if not size or size % count:
            raise ValueError('g_slot size is not divisible by APP_SLOT_COUNT')
        stride = size // count
        offsets = c_word_offsets(ROOT / 'exec/appslot.h', 'AppSlot', 'in_op_wait')
        prefix_size = offsets['in_op_wait'] + struct.calcsize('<I')
        if prefix_size > stride:
            raise ValueError('AppSlot prefix exceeds g_slot stride')
        owners = []
        for owner in range(1, count):
            prefix = self.mem(address + owner * stride, prefix_size)
            owners.append(dict(owner=owner, **{name: struct.unpack_from('<I', prefix, offsets[name])[0]
                                              for name in ('state', 'in_op_wait')}))
        slots = []
        for slot in range(mem('GUI_SLOT_MAX')):
            addr = mem('MEM_SHM_GUI_BASE') + slot * mem('GUI_SLOT_SIZE')
            values = struct.unpack('<HHIHHHH', self.mem(addr, 16))
            fields = ('proto_version', 'flags', 'seq', 'ring_head', 'ring_tail', 'dropped', 'reserved')
            slots.append(dict(zip(fields, values), slot=slot))
        counters = {name: self.word(kernel[name][0]) for name in kernel
                    if name in ('appslot_trim_epoch', 'appslot_trim_request_count',
                                'appslot_trim_mark_count', 'appslot_trim_done_count',
                                'appslot_trim_done_reject_count', 'appslot_trim_pages_total')}
        counters.update({name: self.word(shell[name][0]) for name in
                         ('gshell_trim_delivered', 'gshell_trim_skipped_full')})
        result = {'front': 0, 'windows': [], 'slots': slots,
                  'kernel': owners, 'counters': counters, 'version': 1}
        descriptor = self.descriptor
        if descriptor is NO_DESCRIPTOR:
            return result
        try:
            if descriptor is None:
                name = (__package__ + '.gui_desc') if __package__ else 'gui_desc'
                descriptor = importlib.import_module(name)
            state = dict(descriptor.wm_state(self))
            state['wm_slots'] = state.pop('slots')
        except Exception as exc:
            # This optional reader boundary also covers import, ELF and I/O
            # failures. Keep the already collected v1 data visibly downgraded.
            result['v2_error'] = '%s: %s' % (type(exc).__name__, exc)
            return result
        result.update(state)
        result['version'] = 2
        return result

    def launch(self, path, timeout=60, fallback=False):
        if not path.startswith('/') or any(c.isspace() for c in path):
            raise ValueError('Run requires an absolute path without whitespace')
        try:
            raw = path.encode('ascii')
        except UnicodeEncodeError:
            raise ValueError('Run requires an ASCII path') from None
        if not raw or len(raw) > 255 or any(c < 33 or c > 126 for c in raw):
            raise ValueError('Run path must be at most 255 ASCII bytes')
        self._timeout(timeout)
        height = self.require_gui()
        before = self.wm_state()
        before_windows = {w['id'] for w in before.get('windows', [])}
        app = c_constants(ROOT / 'exec/appslot.h')
        free = {item['owner'] for item in before['kernel']
                if item['owner'] >= app('APP_ID_MIN') and item['state'] == app('APP_STATE_FREE')}
        since = self.consink()['since']  # Before any input, for wait_text(since=...).
        if not free:
            return {'ok': False, 'reason': 'no free kernel owner', 'last': before, 'since': since}
        source = (ROOT / 'userland/gshell/src/startmenu.rs').read_text()
        run_row = int(re.search(r'const IT_RUN: usize = (\d+);', source)[1])
        if fallback:
            # Explicit fallback only: retrying a timed-out launch could start it twice.
            mouse = Mouse(height, self.transport)
            mouse.click(30, height - 12)
            mouse.click(*start_row(height, run_row))
        else:
            key(seq='CTRL+ESC', transport=self.transport)
            self.transport.clock.sleep(0.35)
            for _ in range(run_row):
                key(seq='DOWN', transport=self.transport)
                self.transport.clock.sleep(0.35)
        key(seq='RETURN', transport=self.transport) if not fallback else None
        self.transport.clock.sleep(0.5)
        key(text=path, escapes=False, transport=self.transport)
        key(seq='RETURN', transport=self.transport)
        seen = set()
        exited = set()

        def ready(last):
            owners = {item['owner']: item for item in last['kernel']}
            states = {owner: item['state'] for owner, item in owners.items()}
            exited.update(owner for owner in seen if states[owner] == 0)
            seen.update(owner for owner in free if states[owner] != 0)
            # OP_WAIT can run the WM while its owner stays RUNNING. PARKED
            # is exclusively OP_WAIT; WAIT_KEY / WAIT_POLL don't prove GUI attach.
            if exited or len(seen) != 1:
                return False
            owner = next(iter(seen))
            if not (states[owner] == app('APP_STATE_PARKED') or
                    (states[owner] == app('APP_STATE_RUNNING') and owners[owner].get('in_op_wait') == 1)):
                return False
            return last['version'] == 1 or any(
                w.get('owner') == owner and w['id'] not in before_windows for w in last['windows'])

        waited = self._wait(self.wm_state, ready, timeout)
        waited['since'] = since
        if not waited['ok']:
            waited['reason'] = 'owner did not remain alive and enter OP_WAIT (PARKED or RUNNING in_op_wait=1)'
            return waited
        owner = next(iter(seen))
        last = waited['last']
        result = {'ok': True, 'owner': owner, 'slot': None, 'last': last, 'since': since}
        if last['version'] == 2:
            matches = [s for s in last['wm_slots'] if s.get('owner') == owner and s.get('used')]
            if len(matches) == 1:
                result['slot'] = matches[0]['slot']
                windows = [w for w in last['windows'] if w.get('owner') == owner]
                if windows:
                    result.update(window_id=windows[0]['id'], title=windows[0]['title'])
        return result

    @staticmethod
    def _timeout(timeout):
        if timeout < 15 or not float(timeout) < float('inf'):
            raise ValueError('timeout must be finite and at least 15 seconds')

    def _wait(self, observe, predicate, timeout):
        self._timeout(timeout)
        deadline = self.transport.clock.monotonic() + timeout
        while True:
            last = observe()
            if predicate(last):
                return {'ok': True, 'last': last}
            if self.transport.clock.monotonic() >= deadline:
                return {'ok': False, 'last': last}
            self.transport.clock.sleep(0.5)

    def wait_mem(self, symbol, op, value, timeout=60):
        operations = {'eq': operator.eq, 'ne': operator.ne, 'lt': operator.lt,
                      'le': operator.le, 'gt': operator.gt, 'ge': operator.ge}
        if op not in operations:
            raise ValueError('unknown comparison: ' + op)
        def read():
            kernel, shell = self.symbols()
            table = shell if symbol.startswith('gshell:') else kernel
            name = symbol.split(':', 1)[-1]
            return self.word(table[name][0])
        return self._wait(read, lambda last: operations[op](last, value), timeout)

    def wait_text(self, regex, source='consink', timeout=60, since=None):
        if source not in ('consink', 'screen'):
            raise ValueError('source must be consink or screen')
        self._timeout(timeout)
        if source == 'screen' and since is not None:
            raise ValueError('since is only valid for consink')
        pattern = re.compile(regex)
        chunks = []
        # Discard the retained snapshot when taking the default starting cursor.
        position = self.consink()['since'] if source == 'consink' and since is None else since
        def read():
            nonlocal position
            if source == 'screen':
                return self.screen_text()
            last = self.consink(since=position)
            position = last['since']
            chunks.append(last['text'])
            last['text'] = ''.join(chunks)
            return last
        return self._wait(read, lambda last: screen_matches(pattern, last)
                          if source == 'screen' else bool(pattern.search(last['text'])), timeout)

    def consink(self, since=None, grep=None):
        kernel = self.symbol_factory(self.kernel_elf).read()
        size = c_constants(ROOT / 'include/con_sink.h')('CON_SINK_RING_SIZE')
        def metadata():
            return {name: self.word(kernel['g_' + name][0])
                    for name in ('head', 'tail', 'count')}
        for _ in range(3):
            before = metadata()
            ring = self.mem(kernel['g_ring'][0], size)
            after = metadata()
            if before == after:
                break
        else:
            raise ValueError('console ring changed during snapshot; retry')
        if not (0 <= before['head'] < size and 0 <= before['tail'] < size and
                0 <= before['count'] <= size and
                (before['tail'] + before['count']) % size == before['head']):
            raise ValueError('invalid console ring metadata')
        return decode_ring(ring, **before, since=since, grep=grep)

    def screen_text(self, region=None, path=None):
        from PIL import Image
        body, headers = self.transport.get('/api/screenshot?src=auto')
        image = Image.open(io.BytesIO(body))
        if path:
            image.convert('RGB').save(path, format='PNG')
        address = c_constants(ROOT / 'include/memmap.h')('MEM_FONT_CACHE_BASE') + 282752 + 8836
        cache = self.mem(address, 256 * 16)
        fetched = self.mem(address + 256 * 16, 256)
        result = read_ascii(image, cache, region, fetched=fetched)
        result.update(source=headers.get('X-Screen-Source', '?'), size=list(image.size))
        if path:
            result['path'] = path
        return result


def decode_ring(ring, head, tail, count, since=None, grep=None):
    """since is a record boundary (head from previous call); no owner/slot inference.

    Physical positions cannot detect an entire unseen lap. Report that limit;
    if a cursor is no longer retained, return all retained records and lost=True.
    """
    size = len(ring)
    buf = bytes(ring[(tail + i) % size] for i in range(count))
    records, boundaries = [], []
    i = 0
    while i < len(buf):
        boundaries.append((tail + i) % size)
        kind = buf[i]
        if kind == 1:
            if i + 3 > len(buf):
                raise ValueError('truncated PRINT header')
            n = buf[i + 2]
            if n > 200 or not n or i + 3 + n > len(buf):
                raise ValueError('truncated or invalid PRINT')
            record = {'type': 1, 'color': buf[i + 1], 'text': buf[i + 3:i + 3 + n].decode('utf-8', 'replace')}
            length = 3 + n
        elif kind == 2:
            record, length = {'type': 2}, 1
        elif kind == 3:
            if i + 3 > len(buf):
                raise ValueError('truncated CURSOR')
            record, length = {'type': 3, 'x': buf[i + 1], 'y': buf[i + 2]}, 3
        elif kind == 4:
            if i + 2 > len(buf):
                raise ValueError('truncated EXIT')
            record, length = {'type': 4, 'owner': buf[i + 1]}, 2
        else:
            raise ValueError('unknown console record type: %d' % kind)
        records.append(record)
        i += length
    lost = since is not None and since not in boundaries and since != head
    if since == head:
        records = []
    elif since in boundaries:
        records = records[boundaries.index(since):]
    text = ''.join(r.get('text', '[EXIT id=%d]\n' % r['owner'] if r['type'] == 4 else '')
                   for r in records)
    lines = text.splitlines()
    if grep is not None:
        lines = [line for line in lines if re.search(grep, line)]
    return {'records': records, 'text': text, 'lines': lines, 'since': head,
            'head': head, 'tail': tail, 'count': count, 'lost': lost,
            'cursor_limit': 'an unseen full lap or reset cannot be detected from physical positions'}


def read_ascii(image, font, region=None, fetched=None):
    """Exact 8x16 ASCII recognition, independently of palette/RGB BMP storage.

    region=[x,y,w,h] specifies the cell origin and bounds (also counts entirely
    unknown cells). Without it, find exact glyphs at all pixel origins, choose
    each text row's best 8px phase, and decode the span between its anchors.
    Unanchored text cannot be located; report this limitation, never call OCR a
    guest acceptance verdict.
    """
    if len(font) != 4096:
        raise ValueError('ANK cache must contain 256 x 16 bytes')
    if fetched is not None and len(fetched) != 256:
        raise ValueError('ANK fetched flags must contain 256 bytes')
    font = bytes(font)
    image = image.convert('RGB')
    glyphs = {}
    for code in range(32, 127):
        if fetched is not None and not fetched[code]:
            continue
        signature = font[code * 16:(code + 1) * 16]
        # Multiple ASCII codepoints may have identical guest bitmaps. Guessing
        # the last dictionary entry would silently misread I as l.
        glyphs[signature] = glyphs.get(signature, '') + chr(code)
    if region is not None:
        if len(region) != 4 or any(type(v) is not int for v in region):
            raise ValueError('region must be [x,y,width,height] integers')
        x, y, w, h = region
        if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > image.width or y + h > image.height:
            raise ValueError('region outside screenshot')
        image = image.crop((x, y, x + w, y + h))
    else:
        x, y = 0, 0

    def cell(cx, cy):
        tile = image.crop((cx, cy, cx + 8, cy + 16))
        colors = tile.getcolors(128)
        if len(colors) == 1:
            return glyphs.get(bytes(16), '')
        if len(colors) != 2:
            return ''
        pixels = list(tile.getdata())
        for _, foreground in colors:
            signature = bytes(sum((pixels[row * 8 + col] == foreground) << (7 - col)
                                  for col in range(8)) for row in range(16))
            char = glyphs.get(signature)
            if char is not None:
                return char
        return ''

    spans = []
    if region is not None:
        spans = [(cy, 0, image.width // 8 * 8) for cy in range(0, image.height - 15, 16)]
    else:
        # A glyph uses exactly two colors. Build sliding 8-bit masks per
        # foreground, then match all 16 rows; no fuzzy substitutions.
        palette = image.getcolors(image.width * image.height) or []
        pixels = list(image.getdata())
        w, h = image.size
        matches = {}
        signatures = {key: val for key, val in glyphs.items() if val != ' '}
        middle = {key[7] for key in signatures} - {0}
        for frequency, foreground in palette:
            if frequency < 4:
                continue
            rows = []
            for cy in range(h):
                bits, row = 0, bytearray()
                for cx in range(w):
                    bits = ((bits << 1) | (pixels[cy * w + cx] == foreground)) & 255
                    if cx >= 7:
                        row.append(bits)
                rows.append(bytes(row))
            for cy in range(h - 15):
                for cx, byte in enumerate(rows[cy + 7]):
                    if byte not in middle:
                        continue
                    signature = bytes(row[cx] for row in rows[cy:cy + 16])
                    if signature in signatures and cell(cx, cy) == signatures[signature]:
                        matches.setdefault((cy, cx % 8), set()).add(cx)
        # More anchors win over accidental matches inside border/icon pixels.
        candidates = sorted(matches, key=lambda k: (-len(matches[k]), k))
        chosen = []
        for cy, phase in candidates:
            anchors = sorted(matches[(cy, phase)])
            if len(anchors) < 2 or any(abs(cy - old) < 16 for old in chosen):
                continue
            chosen.append(cy)
            spans.append((cy, anchors[0], anchors[-1] + 8))
        spans.sort()
    lines, origins, unknown, ambiguous = [], [], 0, []
    for cy, left, right in spans:
        chars = []
        for col, cx in enumerate(range(left, right, 8)):
            candidates = cell(cx, cy)
            chars.append(candidates if len(candidates) == 1 else '?')
            if not candidates:
                unknown += 1
            elif len(candidates) > 1:
                ambiguous.append({'pos': [len(lines), col], 'chars': candidates})
        lines.append(''.join(chars).rstrip())
        origins.append([x + left, y + cy])
    return {'lines': lines, 'unknown_count': unknown, 'ambiguous': ambiguous,
            'origins': origins, 'method': 'exact guest ANK 8x16, two colors',
            'limitation': 'automatic mode only locates rows with at least two ASCII anchors; region fixes the cell grid'}


def screen_matches(pattern, result):
    """One regex search over private characters representing candidate groups.

    Consuming atoms accept a group iff they accept at least one of its ASCII
    candidates. Contextual assertions/backreferences cannot preserve that
    existential contract and are rejected, even on an unambiguous screenshot.
    Ordinary regex backtracking cost is still governed by the user's pattern.
    """
    from re import _parser, _compiler, _constants as c

    lines = result.get('lines', [])
    text = list('\n'.join(lines))
    offsets, offset = [], 0
    for line in lines:
        offsets.append(offset)
        offset += len(line) + 1
    groups = {}
    # Avoid collisions with literal text/pattern characters. Classes are guarded
    # below, including broad Unicode ranges that contain the private characters.
    occupied = set(text) | set(pattern.pattern)
    private = (chr(code) for code in range(0xe000, 0xf900) if chr(code) not in occupied)
    for item in result.get('ambiguous', []):
        chars = item['chars']
        if not chars or any(ord(char) < 32 or ord(char) > 126 for char in chars):
            raise ValueError('screen candidates must be printable ASCII')
        if chars not in groups:
            marker = next(private, None)
            if marker is None:
                raise ValueError('too many screen candidate groups')
            groups[chars] = marker
        row, col = item['pos']
        text[offsets[row] + col] = groups[chars]

    parsed = _parser.parse(pattern.pattern, pattern.flags)
    markers = [(c.LITERAL, ord(marker)) for marker in groups.values()]

    def transform(nodes, flags):
        transformed = []
        for op, arg in nodes:
            if op in (c.LITERAL, c.NOT_LITERAL, c.IN, c.CATEGORY, c.ANY):
                atom = _parser.SubPattern(parsed.state, [(op, arg)])
                matcher = _compiler.compile(atom, flags)
                accepted = [(c.LITERAL, ord(marker)) for chars, marker in groups.items()
                            if any(matcher.fullmatch(char) for char in chars)]
                if markers:
                    original = _parser.SubPattern(parsed.state, [
                        (c.ASSERT_NOT, (1, _parser.SubPattern(parsed.state, [(c.IN, markers)]))),
                        (op, arg)])
                    branches = [original]
                    if accepted:
                        branches.append(_parser.SubPattern(parsed.state, [(c.IN, accepted)]))
                    transformed.append((c.BRANCH, (None, branches)))
                else:
                    transformed.append((op, arg))
            elif op == c.SUBPATTERN:
                group, add, remove, child = arg
                transformed.append((op, (group, add, remove,
                                         transform(child, (flags | add) & ~remove))))
            elif op == c.BRANCH:
                transformed.append((op, (arg[0], [transform(b, flags) for b in arg[1]])))
            elif op in (c.MAX_REPEAT, c.MIN_REPEAT):
                transformed.append((op, (arg[0], arg[1], transform(arg[2], flags))))
            elif op == c.AT and arg in (c.AT_BEGINNING, c.AT_BEGINNING_STRING,
                                       c.AT_END, c.AT_END_STRING):
                transformed.append((op, arg))
            else:
                raise ValueError('screen regex cannot expand %s: contextual assertions, '
                                 'boundaries, backreferences, atomic groups and possessive repeats are unsupported' % op)
        return _parser.SubPattern(parsed.state, transformed)

    expanded = _compiler.compile(transform(parsed, parsed.state.flags), parsed.state.flags)
    return bool(expanded.search(''.join(text)))
