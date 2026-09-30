#!/usr/bin/env python3
"""
OS32 HTTP & シリアルコンソールサーバー (os32_server.py)

⚠️ 非推奨 (DEPRECATED, 2026-08-08):
   本サーバの全機能は NP21/W ai-debug 版の内蔵 HTTP API (127.0.0.1:8025) に
   移行済み。/cmd → /api/cmd, /key → /api/key, /screenshot → /api/screenshot。
   内蔵 API は名前付きパイプも pywin32/pyautogui も不要。
   詳細: ~/np21w-src/docs/04-os32-integration.md (NP21/W フォーク、環境変数 NP21W_SRC_DIR で場所を変えられる)
   新規デバッグは tools/np21w_mcp/ の MCP サーバ、または curl で 8025 を使うこと。
   本ファイルは互換のため当面残すが、将来削除する。

Windows上のNP21Wの名前付きパイプ (\\.\pipe\np21w_com1) と常時接続し、
WSLなど外部からの HTTPリクエスト を受け取ってOS32と通信する。

機能:
  - POST /cmd  — OS32のrshellにコマンドを送信し結果を取得
  - POST /key  — NP21/Wウィンドウにキーイベントを送信 (IME等の対話操作用)
  - GET /screenshot — NP21/Wのスクリーンショットを撮影

実行:
    C:\Python313\python.exe os32_server.py
"""

import sys
import os
import time
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
import urllib.parse
import queue
import subprocess
import ctypes
import ctypes.wintypes

try:
    import win32pipe
    import win32file
    import pywintypes
except ImportError:
    print("Error: pywin32 module is required.")
    sys.exit(1)

PIPE_NAME = r'\\.\pipe\np21w_com1'
HTTP_PORT = 8032

pipe_handle = None
pipe_write_lock = threading.Lock()
rx_queue = queue.Queue()

# ==================================================================
# キー送信機能 — PostMessage で NP21/W にキーイベントを送信
#
# シリアル経由の rshell (/cmd) ではコマンド行しか送れないため、
# FEP/IME やゲームのように「キー入力そのもの」を必要とする対話操作は
# エミュレータのウィンドウへ直接キーイベントを投げて検証する。
# ==================================================================

# キー名 → (VKコード, スキャンコード, 拡張キーフラグ) マッピング
KEY_MAP = {
    "F1":  (0x70, 0x3B, 0), "F2":  (0x71, 0x3C, 0), "F3":  (0x72, 0x3D, 0),
    "F4":  (0x73, 0x3E, 0), "F5":  (0x74, 0x3F, 0), "F6":  (0x75, 0x40, 0),
    "F7":  (0x76, 0x41, 0), "F8":  (0x77, 0x42, 0), "F9":  (0x78, 0x43, 0),
    "F10": (0x79, 0x44, 0), "F11": (0x7A, 0x57, 0), "F12": (0x7B, 0x58, 0),
    "ENTER": (0x0D, 0x1C, 0), "ESC": (0x1B, 0x01, 0),
    "SPACE": (0x20, 0x39, 0), "TAB": (0x09, 0x0F, 0),
    "BACKSPACE": (0x08, 0x0E, 0),
    "UP":    (0x26, 0x48, 1), "DOWN":  (0x28, 0x50, 1),
    "LEFT":  (0x25, 0x4B, 1), "RIGHT": (0x27, 0x4D, 1),
    "INSERT":   (0x2D, 0x52, 1), "DELETE":   (0x2E, 0x53, 1),
    "HOME":     (0x24, 0x47, 1), "END":      (0x23, 0x4F, 1),
    "PAGEUP":   (0x21, 0x49, 1), "PAGEDOWN": (0x22, 0x51, 1),
}

# 英数字キー (ローマ字入力・候補番号選択に必要)
KEY_MAP.update({
    "A": (0x41, 0x1E, 0), "B": (0x42, 0x30, 0), "C": (0x43, 0x2E, 0), "D": (0x44, 0x20, 0),
    "E": (0x45, 0x12, 0), "F": (0x46, 0x21, 0), "G": (0x47, 0x22, 0), "H": (0x48, 0x23, 0),
    "I": (0x49, 0x17, 0), "J": (0x4A, 0x24, 0), "K": (0x4B, 0x25, 0), "L": (0x4C, 0x26, 0),
    "M": (0x4D, 0x32, 0), "N": (0x4E, 0x31, 0), "O": (0x4F, 0x18, 0), "P": (0x50, 0x19, 0),
    "Q": (0x51, 0x10, 0), "R": (0x52, 0x13, 0), "S": (0x53, 0x1F, 0), "T": (0x54, 0x14, 0),
    "U": (0x55, 0x16, 0), "V": (0x56, 0x2F, 0), "W": (0x57, 0x11, 0), "X": (0x58, 0x2D, 0),
    "Y": (0x59, 0x15, 0), "Z": (0x5A, 0x2C, 0),
    "1": (0x31, 0x02, 0), "2": (0x32, 0x03, 0), "3": (0x33, 0x04, 0), "4": (0x34, 0x05, 0),
    "5": (0x35, 0x06, 0), "6": (0x36, 0x07, 0), "7": (0x37, 0x08, 0), "8": (0x38, 0x09, 0),
    "9": (0x39, 0x0A, 0), "0": (0x30, 0x0B, 0),
})

def _find_np21w_window():
    """NP21/W ウィンドウハンドルを検索して (hwnd, title) を返す"""
    user32 = ctypes.windll.user32
    found = []
    WNDENUMPROC = ctypes.WINFUNCTYPE(
        ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM
    )
    def callback(hwnd, lparam):
        length = user32.GetWindowTextLengthW(hwnd)
        if length > 0:
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            title = buf.value
            if 'Neko Project' in title or 'np21' in title.lower():
                if user32.IsWindowVisible(hwnd):
                    found.append((hwnd, title))
        return True
    user32.EnumWindows(WNDENUMPROC(callback), 0)
    return found[0] if found else None

def _make_key_lparam(scan_code, extended, is_keyup):
    """WM_KEYDOWN/WM_KEYUP の lParam を構築"""
    lparam = 1  # repeat count = 1
    lparam |= (scan_code & 0xFF) << 16
    if extended:
        lparam |= 1 << 24
    if is_keyup:
        lparam |= 1 << 30  # previous key state
        lparam |= 1 << 31  # transition state
    # lParam は符号付き32bitとして渡す必要がある
    if lparam >= 0x80000000:
        lparam -= 0x100000000
    return lparam

def send_key_to_np21w(key_name):
    """NP21/Wウィンドウにキーイベントを PostMessage で送信"""
    key_name = key_name.upper().strip()

    result = _find_np21w_window()
    if not result:
        return False, "NP21/W window not found."

    hwnd, title = result
    user32 = ctypes.windll.user32

    # NP21/W ウィンドウをアクティブ化してからキーを流す
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.05)

    WM_KEYDOWN = 0x0100
    WM_KEYUP   = 0x0101

    if key_name == "SHIFT_SPACE":
        # Shift + Space 同時押し (FEP の ON/OFF トグル)
        vk_shift, scan_shift, ext_shift = (0x10, 0x70, 0)  # LSHIFT (PC-9801 scancode 0x70)
        lp_shift_down = _make_key_lparam(scan_shift, ext_shift, False)
        lp_shift_up   = _make_key_lparam(scan_shift, ext_shift, True)

        vk_space, scan_space, ext_space = (0x20, 0x39, 0)
        lp_space_down = _make_key_lparam(scan_space, ext_space, False)
        lp_space_up   = _make_key_lparam(scan_space, ext_space, True)

        user32.PostMessageW(hwnd, WM_KEYDOWN, vk_shift, lp_shift_down)
        time.sleep(0.02)
        user32.PostMessageW(hwnd, WM_KEYDOWN, vk_space, lp_space_down)
        time.sleep(0.05)
        user32.PostMessageW(hwnd, WM_KEYUP, vk_space, lp_space_up)
        time.sleep(0.02)
        user32.PostMessageW(hwnd, WM_KEYUP, vk_shift, lp_shift_up)
        return True, "Sent SHIFT_SPACE to '{}'".format(title)

    if key_name not in KEY_MAP:
        return False, "Unknown key: {}".format(key_name)

    vk_code, scan_code, extended = KEY_MAP[key_name]
    lp_down = _make_key_lparam(scan_code, extended, False)
    lp_up   = _make_key_lparam(scan_code, extended, True)

    user32.PostMessageW(hwnd, WM_KEYDOWN, vk_code, lp_down)
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_KEYUP, vk_code, lp_up)

    return True, "Sent {} (VK=0x{:02X}) to '{}'".format(key_name, vk_code, title)


def connect_to_pipe():
    global pipe_handle
    if pipe_handle is not None:
        try: win32file.CloseHandle(pipe_handle)
        except: pass
        pipe_handle = None
    
    print(f"Connecting to {PIPE_NAME}...")
    while True:
        try:
            handle = win32file.CreateFile(
                PIPE_NAME,
                win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                0, None,
                win32file.OPEN_EXISTING,
                0, None
            )
            win32pipe.SetNamedPipeHandleState(
                handle,
                win32pipe.PIPE_READMODE_BYTE | win32pipe.PIPE_WAIT,
                None, None
            )
            print("Connected to NP21/W pipe.")
            pipe_handle = handle
            return True
        except pywintypes.error as e:
            if e.winerror == 2:
                time.sleep(2)
            elif e.winerror == 231:
                try:
                    win32pipe.WaitNamedPipe(PIPE_NAME, 2000)
                except pywintypes.error:
                    time.sleep(1)
            else:
                print(f"Pipe connect error: {e}")
                time.sleep(2)

def _pipe_write_bytes(b_data):
    """パイプへバイナリを書き込む（ロック付き、ディレイ付き）"""
    global pipe_handle
    with pipe_write_lock:
        if not pipe_handle: return False
        try:
            for i in range(len(b_data)):
                win32file.WriteFile(pipe_handle, bytes([b_data[i]]))
                if (i % 64) == 63:
                    win32file.FlushFileBuffers(pipe_handle)
                    time.sleep(0.01) # 64文字ごとに10msウェイト
            win32file.FlushFileBuffers(pipe_handle)
            return True
        except pywintypes.error as e:
            print(f"Pipe write error: {e}")
            pipe_handle = None
            return False

def pipe_reader_thread():
    """パイプ受信監視スレッド — 全バイトをrx_queueに転送"""
    global pipe_handle
    connect_to_pipe()
    
    while True:
        if pipe_handle is None:
            connect_to_pipe()
            continue
            
        try:
            # PeekNamedPipeでデータ有無を確認してブロックを防ぐ
            try:
                hr, avail, message_avail = win32pipe.PeekNamedPipe(pipe_handle, 0)
            except pywintypes.error as e:
                # ERROR_BROKEN_PIPE
                if e.winerror == 109:
                    pipe_handle = None
                    continue
                raise
                
            if avail > 0:
                hr, data = win32file.ReadFile(pipe_handle, 1)
            else:
                time.sleep(0.01)
                continue
                
            if not data: continue
            b = data[0]
            rx_queue.put(b)
                
        except pywintypes.error as e:
            if e.winerror == 109: pipe_handle = None
            elif e.winerror == 232: time.sleep(0.01)
            else:
                print(f"Pipe read error: {e}")
                pipe_handle = None
                time.sleep(1)


def send_and_wait_eot(cmd_str, timeout=30):
    """HTTP要求から呼ばれる。rshell向けにコマンドを送信して結果の文字列を収集する。"""
    # clear rx queue
    while not rx_queue.empty():
        try: rx_queue.get_nowait()
        except: pass
        
    cmd_bytes = cmd_str.encode('ascii', errors='replace') + b'\n'
    if not _pipe_write_bytes(cmd_bytes):
        return "Error: Cannot write to pipe."
        
    response = bytearray()
    start_time = time.time()
    
    while time.time() - start_time < timeout:
        try:
            b = rx_queue.get(timeout=0.1)
            if b == 0x04: # EOT
                return response.decode('utf-8', errors='replace')
            response.append(b)
        except queue.Empty:
            continue
            
    return "Error: Timeout waiting for EOT.\n" + response.decode('utf-8', errors='replace')


class OS32RequestHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length)
        parsed_path = urllib.parse.urlparse(self.path)
        
        if parsed_path.path == '/cmd':
            cmd = post_data.decode('utf-8', errors='replace').strip()
            output = send_and_wait_eot(cmd)
            self.send_response(200)
            self.send_header('Content-type', 'text/plain; charset=utf-8')
            self.end_headers()
            self.wfile.write(output.encode('utf-8', errors='replace'))
        elif parsed_path.path == '/key':
            key_name = post_data.decode('utf-8', errors='replace').strip()
            ok, msg = send_key_to_np21w(key_name)
            self.send_response(200 if ok else 500)
            self.send_header('Content-type', 'text/plain; charset=utf-8')
            self.end_headers()
            self.wfile.write(msg.encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Endpoint not found.")

    def do_GET(self):
        parsed_path = urllib.parse.urlparse(self.path)
        if parsed_path.path == '/screenshot':
            # Run screenshot.py to capture
            script_dir = os.path.dirname(os.path.abspath(__file__))
            screenshot_script = os.path.join(script_dir, "screenshot.py")
            output_png = os.path.join(script_dir, "temp_screenshot.png")
            
            try:
                subprocess.run([sys.executable, screenshot_script, output_png], check=False)
                if os.path.exists(output_png):
                    with open(output_png, "rb") as f:
                        img_data = f.read()
                    
                    self.send_response(200)
                    self.send_header('Content-type', 'image/png')
                    self.send_header('Content-Length', str(len(img_data)))
                    self.end_headers()
                    self.wfile.write(img_data)
                    
                    try:
                        os.remove(output_png)
                    except:
                        pass
                else:
                    self.send_response(500)
                    self.end_headers()
                    self.wfile.write(b"Failed to capture screenshot.")
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Screenshot error: {e}".encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Endpoint not found.")
            
    def log_message(self, format, *args):
        pass # Suppress default HTTP logging

def run_server():
    server_address = ('', HTTP_PORT)
    httpd = HTTPServer(server_address, OS32RequestHandler)
    print(f"OS32 HTTP & Serial Console Server running on port {HTTP_PORT}...")
    
    t = threading.Thread(target=pipe_reader_thread, daemon=True)
    t.start()
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.server_close()

if __name__ == '__main__':
    run_server()
