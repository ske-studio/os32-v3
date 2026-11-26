"""Cold-boot the three PC-98 disks from WSL through the NP21/W HTTP interface."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import np21w_ctl
from in2_gui_mutations import PC98_MUTATIONS, D_FAILURES
from np21w_ini import transform
from np21w_mcp import np21w_client as emu
from in2_gui_trial import run_trial


class ObservedMutation(Exception):
    pass


class Session:
    def __init__(self, pid, base, mutant):
        self.pid = pid
        self.base = base
        self.mutant = mutant
        self.history = ''
        self.offset = 0
        self.bounds = None

    def confirm_instance(self):
        try:
            info = json.loads(emu.get('/api/instance', base=self.base, timeout=2))
        except emu.EmuError:
            return False
        if int(info['pid']) != self.pid:
            raise RuntimeError(f'{self.base} belongs to PID {info["pid"]}, expected {self.pid}')
        return True

    def require_instance(self):
        if not self.confirm_instance():
            raise RuntimeError(f'Cannot verify PID {self.pid} at {self.base}')

    def post(self, path, body):
        self.require_instance()
        status, payload, _ = emu.post(path, body, base=self.base, response=True)
        if status != 200 or not json.loads(payload).get('ok'):
            raise RuntimeError(f'{path} rejected {body!r} (HTTP {status}): {payload!r}')

    def screen(self):
        self.require_instance()
        data = emu.get('/api/tvram', base=self.base)
        return [line.strip() for line in json.loads(data)['lines']]

    def trace(self):
        self.require_instance()
        data = json.loads(emu.get('/api/serial/read', base=self.base))
        self.history += bytes.fromhex(data['hex']).decode('latin1')
        return self.history

    def wait(self, predicate, label):
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            time.sleep(0.025)
        raise TimeoutError(label)

    def wait_match(self, pattern):
        def match():
            trace = self.trace()
            if self.mutant in D_FAILURES and self.mutant != 'drop-tab':
                previous, failure = D_FAILURES[self.mutant]
                if failure in trace.partition(previous)[2]:
                    raise ObservedMutation(failure)
            found = re.search(pattern, trace[self.offset:])
            if self.mutant == 'no-release' and found and found[0].startswith('D-GUI exit RELEASED '):
                # P -> no foreground -> Q adds two focus transitions. Reclaim
                # should have added its own exclusive release before both.
                previous = re.search(r'D-GUI exit-P EXIT [^\r\n]+ gen=(\d+)', trace)
                if previous and int(found[5]) == int(previous[1]) + 2:
                    raise ObservedMutation('exit generation advanced only for the two focus changes')
            return found
        return self.wait(match, pattern)

    def seen(self, marker):
        if self.mutant == 'drop-tab' and marker in ('D-GUI RAW 15 1', 'D-GUI RAW 15 0'):
            return
        self.wait_match(re.escape(marker))

    def key(self, name, down):
        scan = {'a': 0x1d, 'pause': 0x60, 'ctrl': 0x74, 'alt': 0x73, 'tab': 0x0f}[name]
        self.offset = len(self.trace())
        self.post('/api/key', urlencode({'scan': scan, 'down': int(down)}))

    def mouse(self, **args):
        self.offset = len(self.trace())
        self.post('/api/mouse', urlencode(args))

    def button(self, name, down):
        self.mouse(btn={'left': 1, 'right': 2}[name] if down else 0)

    def move(self, dx, dy):
        if self.bounds is None:
            self.mouse(dx=dx, dy=dy)
            return
        width, height = self.bounds
        self.x += dx
        self.y += dy
        def absolute(pixel, extent):
            if not 0 <= pixel < extent:
                raise ValueError(pixel)
            # The PC-98 backend halves getmpos and truncates to screen pixels.
            return 2 * ((pixel * 32767 + extent - 2) // (extent - 1))
        self.mouse(ax=absolute(self.x, width), ay=absolute(self.y, height))


@contextmanager
def cold_boot(paths, ini, base, mutant, exe=None):
    ctl = [sys.executable, str(Path(np21w_ctl.__file__))]
    env = dict(os.environ, NP21W_DIR=paths.wsl, NP21W_AIDEBUG_URL=base)
    exe_args = ['--exe', exe] if exe else []
    start = subprocess.run([*ctl, 'start', '--ini', ini.name, '--alive', '0', *exe_args],
                           env=env, stdout=subprocess.PIPE, text=True)
    print(start.stdout, end='', flush=True)
    pid = re.search(r'^started .*? pid=(\d+) ', start.stdout, re.M)
    if not pid:
        start.check_returncode()
        raise RuntimeError('np21w_ctl start did not report a PID')
    s = Session(int(pid[1]), base, mutant)
    try:
        start.check_returncode()
        s.wait(s.confirm_instance, 'NP21/W HTTP instance')
        # The key-edge build accepts one make/break per POST; the stock build
        # answers 400 to scan/down. Probe with ESC (scan 0) before the guest runs.
        try:
            s.post('/api/key', urlencode({'scan': 0, 'down': 1}))
            s.post('/api/key', urlencode({'scan': 0, 'down': 0}))
        except RuntimeError as err:
            raise RuntimeError('NP21/W lacks scan/down on /api/key; pass --exe with '
                               'the key-edge build') from err
        yield s
    finally:
        # If startup failed or the port changed owners, target only our PID.
        try:
            verified = s.confirm_instance()
        except RuntimeError:
            verified = False
        stop = (['stop'] if verified else ['stop', '--pid', pid[1]]) + exe_args
        subprocess.run([*ctl, *stop], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, epilog='NP21/W must support scan/down on /api/key '
        '(tools/ops/np21w_key_edge.patch).')
    parser.add_argument('--ini', required=True, type=Path, help='source slot ini (WSL path)')
    parser.add_argument('--images', type=Path, default=Path('build/out/pc98-in2-gui'))
    parser.add_argument('--mutant', choices=PC98_MUTATIONS)
    parser.add_argument('--exe', help='NP21/W exe name under NP21W_DIR with scan/down '
                        '(e.g. np21x64w-keyedge.exe); default is np21w_ctl\'s')
    args = parser.parse_args()
    paths = np21w_ctl.paths_from_env()
    original = args.ini.read_bytes()
    port = int(np21w_ctl.parse_ini(original, ['aidbport']).get('AIDBPORT', 8025))
    base = f'http://127.0.0.1:{port}'
    roles = [PC98_MUTATIONS[args.mutant][0]] if args.mutant else ['explicit', 'focus', 'exit']
    for role in roles:
        with tempfile.TemporaryDirectory(dir=paths.wsl, prefix='in2-') as tmp:
            work = Path(tmp)
            image = work / (role + '.nhd')
            shutil.copyfile(args.images / (args.mutant or '') / image.name, image)
            win_image = paths.win_of(str(PureWindowsPath(work.name, image.name)))
            raw, _ = transform(original, {'HDD1FILE': win_image, 'FDD1FILE': '',
                                          'FDD2FILE': '', 'E_RESUME': 'false'})
            # NP21/W cannot read an ini that WSL still holds open on drvfs; it then
            # boots with defaults and no aidebug. Write, close, and only then start.
            fd, name = tempfile.mkstemp(dir=paths.wsl, prefix='in2-', suffix='.ini')
            ini = Path(name)
            try:
                with os.fdopen(fd, 'wb') as f:
                    f.write(raw)
                with cold_boot(paths, ini, base, args.mutant, args.exe) as s:
                    try:
                        trial(s, role)
                    except ObservedMutation as exc:
                        print(f'RED {args.mutant}: {exc}', flush=True)
                    else:
                        if args.mutant:
                            raise RuntimeError(f'{args.mutant} survived')
            finally:
                ini.unlink(missing_ok=True)


PC98_PROMPT = 'OS32:/$'


def trial(s, role):
    s.seen('[boot] exec /bin/gshell.bin')
    mode = s.wait_match(r'D-GUI (?:relative|seamless (\d+) (\d+))\r?\n')
    if mode[1]:
        s.bounds = tuple(map(int, mode.groups()))
        s.x, s.y = (extent // 2 for extent in s.bounds)
        s.move(0, 0)
        # FRONT may already have arrived if NP21/W started centered.
        s.offset = 0
    run_trial(s, role)
    s.seen("C-GUI completed " + role)
    if s.mutant == 'drop-tab':
        trace = s.trace().partition('D-GUI explicit RESUMED')[2]
        if all(f'D-GUI RAW 15 {down}' not in trace for down in (1, 0)):
            raise ObservedMutation('WM received neither TAB edge')
    s.wait(lambda: 'Remote shell active (ESC to exit)' in s.screen(), 'CUI remote shell')
    s.post('/api/key', 'seq=ESC')
    # PC-98 returns to the resident CPL0 shell, whose prompt is the cwd form.
    s.wait(lambda: any(line.startswith(PC98_PROMPT) for line in s.screen()), 'cooked shell prompt')
    command = 'echo cooked'
    for i in range(0, len(command), 4):
        s.post('/api/key', urlencode({'text': command[i:i + 4]}))
        prefix = PC98_PROMPT + ' ' + command[:i + 4]
        s.wait(lambda: any(prefix in line for line in s.screen()), prefix)
    s.post('/api/key', 'seq=ENTER')
    s.wait(lambda: any(line == 'cooked' for line in s.screen()),
           'cooked shell response')
    print(role, 'DONE', flush=True)


if __name__ == '__main__':
    main()
