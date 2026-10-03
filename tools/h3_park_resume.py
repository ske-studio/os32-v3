#!/usr/bin/env python3
"""T2h/h3 PM-only playbook. No network until an explicit live subcommand.

layout: derive offsets from current headers with cross GCC, symbols with nm.
init: discover both fresh fixtures; publish diagnosed owner/generation once.
arm --capture: require a real OP_WAIT state; arm, install breakpoints BEFORE the
     focus click, then capture resume/fire. Loops check foreground, send STOP,
     and record observations in this same process.
verify: require e9/PM evidence JSON, reclamation, then a newly launched fixture.

The landing/pending capture is produced by the serialized capture pump; foreground/vector remain
REQUIRED external evidence:
current kernel has no counters for both setjmp landings or pending consumption.
See TASK_T2D_T2H §5-2. Missing evidence is an error, never a synthetic PASS.
"""
import argparse
import fcntl
from contextlib import contextmanager
from functools import wraps
import uuid
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
MODES = {'pf': 1, 'gp': 2, 'de': 3, 'ud': 4, 'USER-loop': 5, 'KAPI-loop': 6}
FIELDS = 'magic owner generation phase mode arm version fixture resumes consumed window gui_slot'.split()
MAGIC = 0x48335031
VERSION = 1
PHASES = dict(INIT=1, IDENTIFIED=2, WAIT=3, RESUMED=4, ARMED=5, FIRING=6, ERROR=7)
# R1: KAPI accepts WM kill OR syscall-boundary abort, both without pending.
ROUTES = {1: (1, 0, 1), 2: (1, 0, 1), 3: (1, 0, 1), 4: (1, 0, 1),
          5: (1, 1, 1), 6: (0, 0, 0)}  # fault_kill, abort, pending
# OS32 PIT is 100 Hz; fixture grace is 500 ticks, runaway grace 200.
GUEST_HZ = 100
RUNAWAY_WAIT_TICKS = 210
CAPTURE_POLL = 0.25
TICK_STALL_SECONDS = 120

VECTORS = {1: 14, 2: 13, 3: 0, 4: 6, 5: None, 6: None}
COUNTERS = ('ring3_switch_count', 'fault_kill_count', 'ring3_abort_count',
            'appslot_reclaim_count', 'appslot_last_reclaim_id',
            'kctx_irq_depth', 'kctx_exc_depth', 'ring3_wm_depth',
            'ring3_in_syscall', 'ledger_irq_ops', 'ledger_exc_ops', 'g_pending_id')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def debug_reply(reply, path):
    """Individual BP deletion uses integer status; other endpoints use bool."""
    if path.split('?')[0] == '/api/break/del':
        require(type(reply.get('ok')) is int and reply['ok'] == 1 and
                type(reply.get('removed')) is int and reply['removed'] == 1,
                'debug operation failed: ' + path)
    else:
        require(reply.get('ok') is True, 'debug operation failed: ' + path)
    return reply


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def symbols(text, wanted=None):
    result = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3 and (wanted is None or parts[2] in wanted):
            try:
                value = int(parts[0], 16)
            except ValueError:
                continue
            require(parts[2] not in result, 'ambiguous nm symbol: ' + parts[2])
            result[parts[2]] = value
    return result


def trace_sites(disasm, sym):
    """Fail closed if current compiler no longer emits the expected call/store sites."""
    functions = {}
    current = None
    for line in disasm.splitlines():
        header = re.match(r'^[0-9a-f]+ <([^>]+)>:$', line)
        if header:
            current = header[1]
            functions[current] = []
        insn = re.match(r'^\s*([0-9a-f]+):\s+(.*)$', line)
        if insn and current:
            functions[current].append((int(insn[1], 16), insn[2]))
    kill = functions.get('exec_kill', [])
    require(len(kill) >= 2 and kill[0][0] == sym['exec_kill'] and
            re.fullmatch(r'push\s+%ebp', kill[0][1]) and
            re.fullmatch(r'mov\s+%esp,%ebp', kill[1][1]),
            'exec_kill cdecl frame prologue changed')
    abort = [address for address, insn in functions.get('ring3_abort_check', [])
             if re.match(r'(call|jmp)\s+', insn) and insn.endswith('<ring3_kill_kind>')]
    require(len(abort) == 1, 'ambiguous syscall abort site')
    sites = {'pending_finish': sym['exec_pending_finish'], 'wm_kill': sym['exec_kill'],
             'syscall_abort': abort[0]}
    for name, label in (('exec_launch', 'launch'), ('exec_resume', 'resume')):
        rows = functions.get(name, [])
        for callee, suffix in (('exec_setjmp', '_landing'), ('exec_pending_finish', '_pending')):
            hits = [i for i, (_, insn) in enumerate(rows) if
                    insn.startswith('call') and insn.endswith('<' + callee + '>')]
            require(len(hits) == 1 and hits[0] + 1 < len(rows), 'ambiguous trace site: ' + name + suffix)
            i = hits[0]
            sites[label + suffix] = rows[i + 1 if suffix == '_landing' else i][0]
    rows = functions.get('exec_pending_finish', [])
    stores = [i for i, (_, insn) in enumerate(rows) if re.fullmatch(
        r'movl?\s+\$0x0,0x' + format(sym['g_pending_id'], 'x'), insn)]
    require(len(stores) == 1 and stores[0] + 1 < len(rows), 'ambiguous pending consumption')
    sites['consumed'] = rows[stores[0] + 1][0]
    return sites


def capture_counts(case, capture, layout):
    require(capture['case_id'] == case['case_id'] and
            capture['elf_sha256'] == layout['elf_sha256'] and
            capture['sites'] == layout['trace_sites'], 'stale breakpoint capture')
    counts = dict(launch_pending=0, resume_pending=0, pending_consumed=0, wm_kill=0, syscall_abort=0)
    active = False
    for sample in capture['samples']:
        site = sample['site']
        require(sample['eip'] == capture['sites'][site], 'wrong capture PC')
        if site in ('launch_pending', 'resume_pending') and sample['pending'] == case['identity']['app']:
            require(not active, 'overlapping pending transfer')
            counts[site] += 1
            active = True
        elif site == 'consumed' and active:
            require(sample['pending'] == 0 and sample['irq'] == 0 and sample['exc'] == 0,
                    'pending not consumed at normal-context site')
            counts['pending_consumed'] += 1
            active = False
        elif site == 'wm_kill' and sample['argument'] == case['identity']['app']:
            require(sample['irq'] == 0 and sample['exc'] == 0, 'WM kill outside normal context')
            counts['wm_kill'] += 1
        elif (site == 'syscall_abort' and sample['current'] == case['identity']['app'] and
              sample['irq'] == 0 and sample['exc'] == 0):
            counts['syscall_abort'] += 1
    require(not active, 'incomplete pending capture')
    return counts


def capture_trace(p, case, seconds=15, start=None, advance=None, guest_time=False):
    """Serialize traps/callbacks; active runs use PIT time, passive traces wall time."""
    client = p.emu.client
    def get(path):
        return debug_reply(json.loads(client.get(path)), path)
    def post(path):
        reply = json.loads(client.post(path))
        return debug_reply(reply, path)
    require(not get('/api/break').get('breakpoints'), 'release existing breakpoints first')
    instance = get('/api/instance')
    require(not instance['trap_pause'] and not instance['user_pause'], 'release existing pause first')
    sites = p.layout['trace_sites']
    watched = {name: sites[name] for name in ('launch_pending', 'resume_pending', 'consumed', 'wm_kill', 'syscall_abort')}
    result = dict(case_id=case['case_id'], elf_sha256=p.layout['elf_sha256'],
                  sites=sites, samples=[], started_at=time.time())
    installed = []
    owned_trap = False
    failure = None
    p.emu.capture_active = True
    try:
        for address in watched.values():
            post(f'/api/break/add?addr=0x{address:x}')
            installed.append(address)
        if start:
            start()
        deadline = time.monotonic() + seconds
        first_tick = last_tick = None
        progressed_at = time.monotonic() if guest_time else None
        while guest_time or time.monotonic() < deadline:
            instance = get('/api/instance')
            require(not instance['user_pause'], 'user pause during capture')
            if not instance['trap_pause']:
                if guest_time:
                    try:
                        tick = p.guest_tick()
                    except CaptureTrap:
                        continue
                    now = time.monotonic()
                    if first_tick is None:
                        first_tick = tick
                    if tick != last_tick:
                        last_tick, progressed_at = tick, now
                    require(now - progressed_at < TICK_STALL_SECONDS,
                            'guest tick stalled during capture')
                if advance:
                    try:
                        if advance():
                            break
                    except CaptureTrap:
                        continue  # owned trap won the pause race; drain it first
                # Give advance the boundary tick too: FIRING + 210 may become
                # eligible exactly at the budget. STOP then gets its full tail.
                # Unsigned subtraction handles the 32-bit PIT wrap.
                if guest_time and (not case.get('stop_sent') and
                        ((tick - first_tick) & 0xffffffff) >= seconds * GUEST_HZ):
                    break
                p.sleep(CAPTURE_POLL if guest_time else 0.01)
                continue
            regs = get('/api/regs')
            eip = int(regs['eip'], 16)
            names = [name for name, address in watched.items() if address == eip]
            require(len(names) == 1, 'unowned debugger trap')
            owned_trap = True
            site = names[0]
            sample = dict(site=site, eip=eip, eax=int(regs['eax'], 16),
                pending=p.word(p.s['g_pending_id']), irq=p.word(p.s['kctx_irq_depth']),
                exc=p.word(p.s['kctx_exc_depth']), current=p.word(p.s['g_cur']),
                observed_at=time.time())
            if site == 'wm_kill':
                sample['esp'] = int(regs['esp'], 16)
                # System V i386 cdecl: breakpoint is BEFORE push %ebp.
                sample['argument'] = p.word(sample['esp'] + 4)
            result['samples'].append(sample)
            # Execute the trapped instruction once, then re-arm exactly our BP.
            post(f'/api/break/del?addr=0x{eip:x}')
            installed.remove(eip)
            post('/api/step?n=1')
            post(f'/api/break/add?addr=0x{eip:x}')
            installed.append(eip)
            post('/api/resume')
            owned_trap = False
    except Exception as exc:
        failure = exc
        raise
    finally:
        errors = []
        for address in installed:
            try:
                post(f'/api/break/del?addr=0x{address:x}')
            except Exception as exc:
                errors.append(str(exc))
        try:
            # Verify actual debugger state, including deletions that failed.
            remaining = sorted({int(bp['eip'], 16) for bp in get('/api/break')['breakpoints']}
                               & set(watched.values()))
            if remaining:
                errors.append('remaining watched breakpoints: ' +
                              ', '.join(f'0x{address:x}' for address in remaining) +
                              '; PM must delete them manually before resume')
            instance = get('/api/instance')
            if instance['trap_pause'] or instance['user_pause']:
                eip = int(get('/api/regs')['eip'], 16)
                watched_trap = instance['trap_pause'] and eip in watched.values()
                # Cleanup never synthesizes samples: a late event invalidates
                # successful collection, even if we can release its trap safely.
                if failure is None and watched_trap:
                    errors.append('incomplete capture: unrecorded watched trap')
                can_resume = (not remaining and instance['trap_pause'] and
                              not instance['user_pause'] and (owned_trap or watched_trap))
                if errors or not can_resume:
                    errors.append(f"pause state: trap_pause={instance['trap_pause']}, "
                                  f"user_pause={instance['user_pause']}, eip=0x{eip:x}")
                if can_resume:
                    post('/api/resume')
        except Exception as exc:
            errors.append(str(exc))
        finally:
            p.emu.capture_active = False
        if errors:
            message = 'capture cleanup failed: ' + '; '.join(errors)
            if failure is not None:
                message = str(failure) + '; ' + message
            raise RuntimeError(message) from failure
    result['ended_at'] = time.time()
    return result


class CaptureTrap(RuntimeError):
    """Our trap arrived between capture polling and a frozen observation."""


@contextmanager
def live_lock():
    # One lock for all cases: debugger pause/breakpoints belong to the instance,
    # not the SHM case. Hold it before creating a client or reading the case.
    path = Path(os.environ.get('TMPDIR', tempfile.gettempdir())) / 'os32-h3-live.lock'
    with path.open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('another h3 live operation owns capture/pause') from None
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def json_bytes(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def save_capture(case, capture, path):
    raw = json_bytes(capture)
    with Path(path).open('xb') as output:
        output.write(raw)
    case['capture_record'] = dict(path=str(Path(path).resolve()),
        sha256=hashlib.sha256(raw).hexdigest(), started_at=capture['started_at'],
        ended_at=capture['ended_at'])


def make_layout(elf, kernel_map):
    cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'
    nm = str(cross / 'i386-elf-nm')
    with tempfile.TemporaryDirectory(prefix='h3-layout-') as directory:
        obj = str(Path(directory) / 'layout.o')
        subprocess.run([str(cross / 'i386-elf-gcc'), '-std=gnu11', '-ffreestanding',
                        '-Iinclude', '-Ikernel', '-Iexec', '-Ilib', '-c',
                        'tools/h3_layout.c', '-o', obj], cwd=ROOT, check=True)
        offsets = symbols(subprocess.check_output([nm, '--defined-only', obj], text=True))
    required = ('g_cur', 'tick_count', 'exec_kill', 'exec_pending_finish', 'g_slot', 'shm_state', 'shm_block_owner', 'shm_block_span',
                'ledger_owners', '__bss_end', *COUNTERS)
    sym = symbols(subprocess.check_output([nm, '--defined-only', str(elf)], text=True), required)
    for name in required:
        require(name in sym, 'missing current diagnostic symbol: ' + name)
    # map/ELF consistency for global anchors; locals come from nm, not map guesses.
    map_text = Path(kernel_map).read_text()
    for name in ('__bss_end', 'ring3_switch_count'):
        import re
        require(any(int(m, 16) == sym[name] for m in re.findall(
            r'(0x[0-9a-fA-F]+)\s+' + name + r'\b', map_text)), 'map/ELF mismatch: ' + name)
    disasm = subprocess.check_output([str(cross / 'i386-elf-objdump'), '-d',
                                     '--no-show-raw-insn', str(elf)], text=True)
    return {'trace_sites': trace_sites(disasm, sym), 'elf_sha256': digest(elf), 'map_sha256': digest(kernel_map),
            'symbols': sym, 'offsets': {k[3:]: v for k, v in offsets.items() if k.startswith('h3_')}}


def frozen(method):
    @wraps(method)
    def call(self, *args, **kwargs):
        with self.emu.freeze():
            return method(self, *args, **kwargs)
    return call


class Emulator:
    def __init__(self):
        from np21w_mcp import np21w_client
        self.client = np21w_client
        self.freeze_depth = 0

    @contextmanager
    def freeze(self):
        outer = self.freeze_depth == 0
        if outer:
            instance = json.loads(self.client.get('/api/instance'))
            if getattr(self, 'capture_active', False) and instance.get('trap_pause'):
                raise CaptureTrap('capture trap pending')
            require(instance.get('ok') is True and
                    instance.get('trap_pause') is False and instance.get('user_pause') is False,
                    'PM must release an existing debugger/user pause first')
            reply = json.loads(self.client.post('/api/pause'))
            if reply.get('ok') is not True and getattr(self, 'capture_active', False):
                raced = json.loads(self.client.get('/api/instance'))
                if raced.get('trap_pause') and not raced.get('user_pause'):
                    raise CaptureTrap('capture trap won pause race')
            require(reply.get('ok') is True, 'pause failed')
        self.freeze_depth += 1
        try:
            yield
        finally:
            self.freeze_depth -= 1
            if outer:
                reply = json.loads(self.client.post('/api/resume'))
                require(reply.get('ok') is True, 'resume failed')

    def read(self, address, size):
        reply = json.loads(self.client.get(
            f'/api/mem?addr=0x{address:x}&len={size}&space=phys'))
        debug_reply(reply, '/api/mem')
        data = bytes.fromhex(reply['hex'])
        require(len(data) == size, 'short physical read')
        return data

    def write(self, address, data):
        reply = json.loads(self.client.post(
            f'/api/mem?addr=0x{address:x}&space=phys', data.hex()))
        require(reply.get('ok') is True, 'memory write rejected')

    def click(self, x, y, height):
        position = (f'ax={(x * 65535 + 319) // 639}&'
                    f'ay={(y * 65535 + (height - 1) // 2) // (height - 1)}')
        debug_reply(json.loads(self.client.post('/api/mouse', position)), '/api/mouse')
        debug_reply(json.loads(self.client.post('/api/mouse', 'btn=1')), '/api/mouse')
        time.sleep(0.4)
        debug_reply(json.loads(self.client.post('/api/mouse', 'btn=0')), '/api/mouse')

    def stop(self):
        reply = json.loads(self.client.post('/api/key', 'seq=CTRL%2BSTOP&hold=300'))
        require(reply.get('ok') is True, 'STOP rejected')


class Playbook:
    def __init__(self, emu, layout, sleep=time.sleep):
        self.emu, self.layout, self.sleep = emu, layout, sleep
        self.s, self.o = layout['symbols'], layout['offsets']

    def word(self, address):
        return struct.unpack('<I', self.emu.read(address, 4))[0]

    def put(self, address, value):
        self.emu.write(address, struct.pack('<I', value))

    def block(self, address):
        return dict(zip(FIELDS, struct.unpack('<12I', self.emu.read(address, 48))))

    def counters(self):
        return {name: self.word(self.s[name]) for name in COUNTERS}

    def identity(self, index):
        o, s = self.o, self.s
        require(0 <= index < o['block_count'], 'not an allocatable SHM block')
        require(self.emu.read(s['shm_state'] + index, 1) == b'\x01', 'SHM not allocated RW')
        require(self.word(s['shm_block_span'] + index * 4) == 1, 'not a private single block')
        app = self.word(s['shm_block_owner'] + index * 4)
        require(o['app_min'] <= app <= o['app_max'], 'invalid SHM app owner')
        slot = s['g_slot'] + app * o['slot_size']
        state = self.word(slot + o['slot_state'])
        require(state != o['free'], 'dead appslot')
        address_space = self.word(slot + o['slot_as'])
        require(0 < address_space < s['__bss_end'] + o['shm_delta'], 'invalid AS pointer')
        owner = self.word(address_space + o['as_owner'])
        generation = self.word(address_space + o['as_generation'])
        require(0 < owner < o['ledger_count'] and generation > 0, 'invalid AS identity')
        ledger = s['ledger_owners'] + owner * o['ledger_size']
        require(self.emu.read(ledger + o['ledger_kind'], 1)[0] == o['ledger_as'], 'not a live AS owner')
        require(self.emu.read(ledger + o['ledger_id'], 1)[0] == app, 'ledger/appslot mismatch')
        require(self.word(ledger + o['ledger_pages']) > 0, 'empty AS owner')
        base = ((s['__bss_end'] + 4095) & ~4095) + o['shm_delta']
        return {'index': index, 'address': base + index * o['block_size'],
                'app': app, 'owner': owner, 'generation': generation, 'slot': slot}

    @frozen
    def discover(self, fixture):
        found = []
        for index in range(self.o['block_count']):
            if self.emu.read(self.s['shm_state'] + index, 1) != b'\x01':
                continue
            try:
                ident = self.identity(index)
            except RuntimeError:
                continue  # unrelated SHM / multi-block tail is not our fixture
            b = self.block(ident['address'])
            if b['magic'] == MAGIC and b['version'] == VERSION and b['fixture'] == fixture:
                found.append(ident)
        require(len(found) == 1, 'missing or duplicate fixture')
        return found[0]

    @frozen
    def checked(self, ident):
        require(self.identity(ident['index']) == ident, 'owner/generation/address changed')
        b = self.block(ident['address'])
        require(b['magic'] == MAGIC and b['version'] == VERSION, 'bad SHM protocol')
        require((b['owner'], b['generation']) == (ident['owner'], ident['generation']), 'stale SHM identity')
        return b

    def initialize(self, fixture):
        with self.emu.freeze():
            ident = self.discover(fixture)
            b = self.block(ident['address'])
            require(b['phase'] == PHASES['INIT'] and b['owner'] == 0 and b['generation'] == 0,
                    'identity already published or not INIT')
            require(self.identity(ident['index']) == ident, 'initial identity changed')
            self.put(ident['address'] + 4, ident['owner'])
            self.put(ident['address'] + 8, ident['generation']) # publish last
        self.wait(lambda: self.checked(ident)['phase'] in (PHASES['IDENTIFIED'], PHASES['WAIT'], PHASES['RESUMED']), 'identity receipt')
        return ident

    def wait(self, predicate, label, attempts=300):
        for _ in range(attempts):
            value = predicate()
            if value:
                return value
            self.sleep(0.1)
        raise RuntimeError('timeout: ' + label)

    def guest_tick(self):
        # One aligned u32 read needs no multi-field frozen snapshot.
        return self.word(self.s['tick_count'])

    @frozen
    def arm(self, ident, mode):
        require(mode in MODES.values(), 'invalid mode')
        b = self.checked(ident)
        require(b['phase'] == PHASES['WAIT'] and b['arm'] == 0 and b['consumed'] == 0, 'not unarmed WAIT')
        state = self.word(ident['slot'] + self.o['slot_state'])
        # Idle fixtures need not yield: WM may keep one inside OP_WAIT.
        # Only the current RUNNING app with the live wait mark is admissible.
        parked_wait = (state == self.o['parked'] and
                       self.word(ident['slot'] + self.o['slot_wait']) == 1)
        running_wait = (state == self.o['running'] and
                        self.word(self.s['g_cur']) == ident['app'] and
                        self.word(ident['slot'] + self.o['slot_in_wait']) == 1)
        require(parked_wait or running_wait, 'not actual OP_WAIT state')
        survivor = self.discover(3 - b['fixture'])
        self.checked(survivor)
        before = self.counters()
        # Immediately before each write, reject owner reuse. No retry/keys fallback.
        self.checked(ident)
        self.put(ident['address'] + 16, mode)
        self.checked(ident)
        self.put(ident['address'] + 20, 1)
        return {'survivor': survivor, 'identity': ident, 'mode': mode, 'before': before, 'resumes': b['resumes'],
                'fixture': b['fixture'], 'window': b['window'], 'case_id': uuid.uuid4().hex,
                'armed_in_running_wait': running_wait, 'armed_at': time.time()}

    @frozen
    def resumed(self, case):
        b = self.checked(case['identity'])
        if b['phase'] not in (PHASES['ARMED'], PHASES['FIRING']):
            require(b['phase'] != PHASES['ERROR'], 'fixture ERROR')
            return False
        require(b['mode'] == case['mode'] and b['arm'] == 0 and b['consumed'] == 1,
                'arm not consumed exactly once')
        require(b['resumes'] > case['resumes'], 'missing resume mark')
        switches = self.word(self.s['ring3_switch_count'])
        before_switches = case['before']['ring3_switch_count']
        # A live OP_WAIT may return on the focus event without switching AS.
        require(switches > before_switches or
                (case.get('armed_in_running_wait') is True and switches == before_switches),
                'missing switch increment')
        return {'block': b, 'switch_count': self.word(self.s['ring3_switch_count'])}

    def quiescent(self, case, c):
        survivor, o, s = case['survivor'], self.o, self.s
        in_wait = (self.word(s['g_cur']) == survivor['app'] and
                   self.word(survivor['slot'] + o['slot_state']) == o['running'] and
                   self.word(survivor['slot'] + o['slot_in_wait']) == 1)
        return c['kctx_irq_depth'] == 0 and c['kctx_exc_depth'] == 0 and (
            (c['ring3_in_syscall'], c['ring3_wm_depth']) in
                ((0, 0), (1, 1)) if in_wait else
                c['kctx_irq_depth'] == 0 and c['kctx_exc_depth'] == 0 and
                (c['ring3_in_syscall'], c['ring3_wm_depth']) == (0, 0))

    def reclaimed(self, case):
        # Also retry the (1,0) int80-before-wm_enter window, always thawing.
        for _ in range(20):
            with self.emu.freeze():
                if self.quiescent(case, self.counters()):
                    return self.reclaimed_snapshot(case)
            self.sleep(0.1)
        raise RuntimeError('no quiescent reclamation sample')

    def reclaimed_snapshot(self, case):
        ident, o, s = case['identity'], self.o, self.s
        c = self.counters()
        require(self.word(ident['slot'] + o['slot_state']) == o['free'], 'app not reclaimed')
        require(self.word(s['shm_block_owner'] + 4 * ident['index']) == 0 and
                self.emu.read(s['shm_state'] + ident['index'], 1) == b'\x00', 'SHM owner remains')
        require(self.emu.read(s['ledger_owners'] + ident['owner'] * o['ledger_size'] + o['ledger_kind'], 1) == b'\x00' and
                self.word(s['ledger_owners'] + ident['owner'] * o['ledger_size'] + o['ledger_pages']) == 0,
                'ledger owner pages remain')
        survivor = case['survivor']
        self.checked(survivor)
        require(self.quiescent(case, c), 'not a quiescent survivor OP_WAIT sample')
        for name in ('kctx_irq_depth', 'kctx_exc_depth',
                     'ledger_irq_ops', 'ledger_exc_ops', 'g_pending_id'):
            require(c[name] == 0, 'nonzero ' + name)
        require(c['appslot_reclaim_count'] - case['before']['appslot_reclaim_count'] == 1 and
                c['appslot_last_reclaim_id'] == ident['app'], 'reclaim must occur once for target')
        kills, aborts, _ = ROUTES[case['mode']]
        if case['mode'] == 6:
            kills = c['fault_kill_count'] - case['before']['fault_kill_count']
            require(kills in (0, 1), 'wrong KAPI kill route')
            aborts = kills
        require(c['fault_kill_count'] - case['before']['fault_kill_count'] == kills, 'wrong kill route')
        require(c['ring3_abort_count'] - case['before']['ring3_abort_count'] == aborts,
                'wrong fault/STOP kind')
        return c

    def verify_trace(self, case, trace):
        # e9 or PM debugger trace, never inferred from a phase/counter snapshot.
        require(trace['map_sha256'] == self.layout['map_sha256'], 'stale trace map')
        require(trace['identity'] == case['identity'] and trace['mode'] == case['mode'] and
                trace['case_id'] == case['case_id'], 'wrong trace case')
        capture = trace['capture']
        record = case.get('capture_record')
        require(record is not None, 'missing recorded capture')
        require(hashlib.sha256(json_bytes(capture)).hexdigest() == record['sha256'],
                'embedded capture hash mismatch')
        require(capture['started_at'] == record['started_at'] and
                capture['ended_at'] == record['ended_at'] and
                case['armed_at'] <= capture['started_at'] <= capture['ended_at'],
                'capture predates arm or has invalid timestamps')
        require(all(capture['started_at'] <= row['observed_at'] <= capture['ended_at']
                    for row in capture['samples']), 'sample outside capture interval')
        pending = ROUTES[case['mode']][2]
        counts = capture_counts(case, capture, self.layout)
        syscall = counts['syscall_abort'] if case['mode'] == 6 else 0
        require(syscall in (0, 1), 'duplicate syscall abort')
        require(counts == dict(launch_pending=0, resume_pending=pending,
                               pending_consumed=pending, wm_kill=1 - pending - syscall,
                               syscall_abort=syscall), 'breakpoint route counts')
        landing = 'resume' if pending else ('syscall-abort' if syscall else 'wm-kill')
        require(trace['landing'] == landing and trace['launch_pending'] == 0 and
                trace['resume_pending'] == pending and trace['pending_consumed'] == pending, 'landing/pending trace')
        if case['mode'] == 6:
            reclaimed = case['reclaimed']
            require(reclaimed['fault_kill_count'] - case['before']['fault_kill_count'] == syscall and
                    reclaimed['ring3_abort_count'] - case['before']['ring3_abort_count'] == syscall,
                    'KAPI counter/capture route disagreement')
        require(trace['fired'] == case['mode'] and trace['resume_observed'] is True, 'missing firing evidence')
        require(trace['vector'] == VECTORS[case['mode']],
                'wrong exception vector')
        if case['mode'] >= 5:
            require(trace['foreground_window'] == case['window'] and
                    trace['foreground_app'] == case['identity']['app'], 'STOP foreground mismatch')

    @frozen
    def observe(self, case):
        ident = case['identity']
        space = self.word(ident['slot'] + self.o['slot_as'])
        require(space == 0 or space < self.s['__bss_end'] + self.o['shm_delta'], 'invalid AS pointer')
        return dict(case_id=case['case_id'], identity=ident, counters=self.counters(),
            current=self.word(self.s['g_cur']), tick=self.word(self.s['tick_count']),
            addresses={name: self.s[name] for name in (*COUNTERS, 'g_cur', 'tick_count')},
            slot_addresses={name: ident['slot'] + self.o['slot_' + name]
                            for name in ('state', 'in_wait', 'abort', 'tick', 'as')},
            slot={name: self.word(ident['slot'] + self.o['slot_' + name])
                  for name in ('state', 'in_wait', 'abort', 'tick', 'as')},
            address_space=dict(address=space,
                owner=self.word(space + self.o['as_owner']) if space else None,
                generation=self.word(space + self.o['as_generation']) if space else None),
            observed_at=time.time())

    def foreground(self, case, path):
        try:
            trace = json.loads(Path(path).read_text())
            required = ('case_id', 'observed_at', 'phase', 'identity', 'map_sha256',
                        'foreground_window', 'foreground_app')
            require(isinstance(trace, dict) and all(key in trace for key in required) and
                    type(trace['observed_at']) in (int, float), 'invalid foreground fields')
        except (OSError, ValueError, RuntimeError) as exc:
            raise RuntimeError('STOP not sent during capture: foreground evidence missing/invalid; '
                               'STOP foreground not confirmed: ' + str(exc)) from exc
        require(trace['case_id'] == case['case_id'] and
                0 <= time.time() - trace['observed_at'] and
                trace['observed_at'] >= case['armed_at'] and
                trace['phase'] == PHASES['FIRING'] and trace['identity'] == case['identity'] and
                trace['map_sha256'] == case['map_sha256'] and
                trace['foreground_window'] == case['window'] and
                trace['foreground_app'] == case['identity']['app'],
                'STOP not sent during capture: foreground evidence mismatch; STOP foreground not confirmed')
        if time.time() - trace['observed_at'] > 5:
            return None  # Valid but stale: wait for the PM to refresh it.
        return trace

    def loop_phase(self, case):
        # This live hint authorizes only waiting. Revalidate identity and phase
        # in the frozen snapshot immediately before STOP.
        return self.word(case['identity']['address'] + FIELDS.index('phase') * 4)

    def loop_evidence(self, case, foreground, firing_at, status):
        elapsed = (self.guest_tick() - firing_at) & 0xffffffff
        status['reason'] = f'210 ticks not reached: FIRING elapsed={elapsed}'
        if elapsed < RUNAWAY_WAIT_TICKS or not Path(foreground).exists():
            if elapsed >= RUNAWAY_WAIT_TICKS:
                status['reason'] = 'foreground evidence missing'
            return None
        evidence = self.foreground(case, foreground)
        status['reason'] = 'foreground evidence stale (host age > 5s)'
        return evidence

    def stop_loop(self, case, foreground, persist=lambda: None):
        firing_at = None
        status = dict(reason='FIRING not observed')
        def advance():
            nonlocal firing_at
            if self.loop_phase(case) != PHASES['FIRING']:
                return
            if firing_at is None:
                firing_at = self.guest_tick()
            evidence = self.loop_evidence(case, foreground, firing_at, status)
            if evidence is None:
                return
            with self.emu.freeze():
                require(self.checked(case['identity'])['phase'] == PHASES['FIRING'], 'loop not firing')
            # Identity reads also cost host time; refresh evidence after them.
            evidence = self.loop_evidence(case, foreground, firing_at, status)
            if evidence is None:
                return
            self.emu.stop()
            case['stop_sent'] = True
            case['foreground_observation'] = evidence
            persist()
            return True
        capture_trace(self, case, seconds=30, advance=advance, guest_time=True)
        require(case.get('stop_sent') is True, 'STOP not sent during capture: ' + status['reason'])

    def run_capture(self, case, capture_path, out, foreground=None, click=None, persist=lambda: None):
        require(case['mode'] < 5 or foreground is not None, 'loop requires --trace foreground evidence')
        status = dict(reason='FIRING not observed')
        firing_at = None
        stop_at = None
        observations = {}
        def advance():
            nonlocal firing_at, stop_at
            if case.get('stop_sent'):
                elapsed = time.monotonic() - stop_at
                for delay in (2, 10):
                    key = 'after_' + str(delay) + 's'
                    if elapsed >= delay and key not in observations:
                        observations[key] = self.observe(case)
                return 'after_10s' in observations
            if not case.get('resume_verified'):
                if self.loop_phase(case) not in (PHASES['ARMED'], PHASES['FIRING'], PHASES['ERROR']):
                    return
                resumed = self.resumed(case)
                if not resumed:
                    return
                case['resume_observation'] = resumed
                case['resume_verified'] = True
                persist()
            if case['mode'] < 5:
                return
            if self.loop_phase(case) != PHASES['FIRING']:
                return
            if firing_at is None:
                firing_at = self.guest_tick()
            evidence = self.loop_evidence(case, foreground, firing_at, status)
            if evidence is None:
                return
            # Freeze only the final identity/observation snapshot, never each
            # waiting poll. Drain any owned trap before retrying this snapshot.
            with self.emu.freeze():
                require(self.checked(case['identity'])['phase'] == PHASES['FIRING'], 'loop not firing')
                observations['before_stop'] = self.observe(case)
            evidence = self.loop_evidence(case, foreground, firing_at, status)
            if evidence is None:
                return
            stop_at = time.monotonic()
            self.emu.stop()
            case['stop_sent'] = True
            case['foreground_observation'] = evidence
            persist()
        capture = capture_trace(self, case, seconds=30, start=click, advance=advance, guest_time=True)
        save_capture(case, capture, capture_path)
        persist()
        require(case['mode'] < 5 or case.get('stop_sent') is True, 'STOP not sent during capture: ' + status['reason'])
        require(case.get('resume_verified') is True, 'resume not captured')
        require(case['mode'] < 5 or 'after_10s' in observations, 'timed observations incomplete')
        observations['final'] = self.observe(case)
        with Path(out).open('xb') as output:
            output.write(json_bytes(dict(case_id=case['case_id'], observations=observations)))
        return capture

    @frozen
    def next_launch(self, case):
        fresh = self.discover(case['fixture'])
        require(fresh['generation'] != case['identity']['generation'], 'old next launch generation')
        b = self.block(fresh['address'])
        require(b['phase'] == PHASES['INIT'] and b['owner'] == 0 and b['generation'] == 0, 'next launch not fresh INIT')
        return fresh


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('layout', 'init', 'arm', 'stop', 'observe', 'trace-watch', 'loop-watch', 'reclaim', 'verify'))
    parser.add_argument('--elf', type=Path, default=ROOT / 'build/out/kernel.elf')
    parser.add_argument('--map', type=Path, default=ROOT / 'build/out/kernel.map')
    parser.add_argument('--layout', type=Path, required=True)
    parser.add_argument('--case', type=Path)
    parser.add_argument('--fixture', type=int, choices=(1, 2))
    parser.add_argument('--mode', choices=MODES)
    parser.add_argument('--trace', type=Path)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--height', type=int, choices=(400, 480), default=400)
    args = parser.parse_args()
    if args.action == 'layout':
        args.layout.write_text(json.dumps(make_layout(args.elf, args.map), indent=2) + '\n')
        return
    with live_lock():
        live_main(args)


def live_main(args):
    layout = json.loads(args.layout.read_text())
    require(digest(args.elf) == layout['elf_sha256'] and digest(args.map) == layout['map_sha256'],
            'layout is not from current map/ELF')
    require(args.case is not None, '--case required')
    p = Playbook(Emulator(), layout)
    if args.action == 'init':
        require(args.fixture in (1, 2), '--fixture required')
        require(not args.case.exists(), 'case file already exists')
        ident = p.initialize(args.fixture)
        args.case.write_text(json.dumps({'identity': ident, 'map_sha256': layout['map_sha256']}, indent=2) + '\n')
        return
    case = json.loads(args.case.read_text())
    require(case['map_sha256'] == layout['map_sha256'], 'stale case map')
    if args.action == 'arm':
        require(args.mode is not None, '--mode required')
        require('mode' not in case, 'case already armed')
        require(args.capture is not None and args.out is not None, 'arm requires --capture and --out')
        require(not args.capture.exists() and not args.out.exists(), 'capture/output file already exists')
        require(MODES[args.mode] < 5 or args.trace is not None, 'loop requires --trace foreground evidence')
        case.update(p.arm(case['identity'], MODES[args.mode]))
        def persist():
            args.case.write_bytes(json_bytes(case))
        persist()
        p.run_capture(case, args.capture, args.out, args.trace,
            click=lambda: p.emu.click(90 if case['fixture'] == 1 else 400, 68, args.height), persist=persist)
        return
    if args.action == 'loop-watch':
        require(case['mode'] >= 5 and case.get('resume_verified') is True and not case.get('stop_sent'),
                'loop-watch requires resumed loop without STOP')
        require(args.capture is not None and args.out is not None and args.trace is not None,
                'loop-watch requires --capture, --out and --trace')
        require(not args.capture.exists() and not args.out.exists(), 'capture/output file already exists')
        p.run_capture(case, args.capture, args.out, args.trace,
            persist=lambda: args.case.write_bytes(json_bytes(case)))
        return
    if args.action == 'trace-watch':
        require(args.trace is not None and case.get('resume_verified') is True, 'resumed case and --trace required')
        require(not args.trace.exists(), 'capture file already exists')
        save_capture(case, capture_trace(p, case), args.trace)
        args.case.write_bytes(json_bytes(case))
        return
    if args.action == 'observe':
        require(args.out is not None and args.out != args.case, 'observe requires separate --out')
        with args.out.open('xb') as output:
            output.write(json_bytes(p.observe(case)))
        print('observation only; not PASS')
        return
    if args.action == 'stop':
        require(case['mode'] >= 5 and case.get('resume_verified') is True and not case.get('stop_sent'),
                'STOP requires a resumed loop case')
        require(args.trace is not None, 'STOP requires fresh foreground evidence --trace')
        require(p.checked(case['identity'])['phase'] == PHASES['FIRING'], 'loop not firing')
        # Use the same guest grace, freshness retry and bounded pump as arm.
        p.stop_loop(case, args.trace, persist=lambda: args.case.write_bytes(json_bytes(case)))
        args.case.write_text(json.dumps(case, indent=2) + '\n')
        return
    if args.action == 'reclaim':
        case['reclaimed'] = p.reclaimed(case)
        args.case.write_text(json.dumps(case, indent=2) + '\n')
        return
    require(case.get('resume_verified') is True and 'reclaimed' in case and args.trace is not None, 'reclaim snapshot and --trace required')
    require(case['mode'] < 5 or case.get('stop_sent') is True, 'STOP was not sent by this case')
    p.verify_trace(case, json.loads(args.trace.read_text()))
    case['next_launch'] = p.next_launch(case)
    case['trace_sha256'] = digest(args.trace)
    args.case.write_text(json.dumps(case, indent=2) + '\n')
    print('h3 case PASS (PM trace + physical diagnostics + next launch)')


if __name__ == '__main__':
    main()
