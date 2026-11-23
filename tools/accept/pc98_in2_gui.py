"""Cold-boot the three PC-98 disks on Windows with NP21/W input and a COM1 log."""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode
from urllib.request import urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from np21w_ini import transform
from np21w_mcp import np21w_client as emu
from in2_gui_trial import run_trial


def window(pid):
    handles = []
    user = ctypes.windll.user32
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback
    def collect(hwnd, unused):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd) and user.GetWindowTextLengthW(hwnd):
            handles.append(hwnd)
        return True

    user.EnumWindows(collect, 0)
    return handles[0] if handles else None


class Session:
    def __init__(self, process, serial, previous, port):
        self.base = f"http://127.0.0.1:{port}"
        self.proc = process
        self.serial = serial
        self.previous = previous
        self.offset = 0
        self.bounds = None

    def confirm_instance(self):
        try:
            info = json.loads(emu.get('/api/instance', base=self.base, urlopen=urlopen, timeout=2))
        except emu.EmuError:
            return False
        if int(info['pid']) != self.proc.pid:
            raise RuntimeError(f'{self.base} belongs to PID {info["pid"]}, expected {self.proc.pid}')
        return True

    def post(self, path, body):
        return emu.post(path, body, base=self.base, urlopen=urlopen)

    def screen(self):
        data = emu.get('/api/tvram', base=self.base, urlopen=urlopen)
        return [line.strip() for line in json.loads(data)['lines']]

    def trace(self):
        data = self.serial.read_bytes() if self.serial.exists() else b''
        if self.previous and not data.startswith(self.previous):
            self.previous = b''
        return data[len(self.previous):].decode('latin1')

    def wait(self, predicate, label):
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError('NP21/W exited before ' + label)
            result = predicate()
            if result:
                return result
            time.sleep(0.025)
        raise TimeoutError(label)

    def wait_match(self, pattern):
        return self.wait(lambda: re.search(pattern, self.trace()[self.offset:]), pattern)

    def seen(self, marker):
        self.wait_match(re.escape(marker))

    def key(self, name, down):
        # Windows scan codes; the probe checks the translated PC-98 scan.
        vk, scan = {'a': (0x41, 0x1e), 'pause': (0x13, 0x45), 'ctrl': (0x11, 0x1d),
                    'alt': (0x12, 0x38), 'tab': (9, 0x0f)}[name]
        hwnd = self.wait(lambda: window(self.proc.pid), 'NP21/W window')
        self.offset = len(self.trace())
        param = 1 | (scan << 16) | (0 if down else 3 << 30)
        if not ctypes.windll.user32.PostMessageW(wintypes.HWND(hwnd), 0x100 if down else 0x101,
                                                  vk, wintypes.LPARAM(param)):
            raise ctypes.WinError()

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['exe', 'ini', 'serial']:
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--images', type=Path, default=Path('build/out/pc98-in2-gui'))
    args = parser.parse_args()
    exe = args.exe.resolve()
    for role in ['explicit', 'focus', 'exit']:
        with tempfile.TemporaryDirectory(dir=exe.parent) as tmp:
            work = Path(tmp)
            image = work / (role + '.nhd')
            shutil.copyfile(args.images / image.name, image)
            ini = work / 'in2.ini'
            raw, _ = transform(args.ini.read_bytes(), {'HDD1FILE': str(image), 'FDD1FILE': '',
                                                      'FDD2FILE': '', 'E_RESUME': 'false'})
            ini.write_bytes(raw)
            port = ctypes.windll.kernel32.GetPrivateProfileIntW(
                "NekoProject21", "aidbport", 8025, str(ini))
            previous = args.serial.read_bytes() if args.serial.exists() else b''
            process = subprocess.Popen([str(exe), '/i' + str(ini)], cwd=exe.parent)
            s = Session(process, args.serial, previous, port)
            try:
                s.wait(s.confirm_instance, 'NP21/W HTTP instance')
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
                s.wait(lambda: 'Remote shell active (ESC to exit)' in s.screen(), 'CUI remote shell')
                s.post('/api/key', 'seq=ESC')
                s.wait(lambda: 'sh>' in s.screen(), 'cooked shell prompt')
                command = 'echo cooked'
                for i in range(0, len(command), 4):
                    s.post('/api/key', urlencode({'text': command[i:i + 4]}))
                    prefix = 'sh> ' + command[:i + 4]
                    s.wait(lambda: any(prefix in line for line in s.screen()), prefix)
                s.post('/api/key', 'seq=ENTER')
                s.wait(lambda: 'cooked' in s.screen(),
                       'cooked shell response')
                print(role, 'DONE', flush=True)
            finally:
                hwnd = window(process.pid)
                if hwnd:
                    ctypes.windll.user32.PostMessageW(wintypes.HWND(hwnd), 0x10, 0, 0)
                process.wait(timeout=30)


if __name__ == '__main__':
    main()
