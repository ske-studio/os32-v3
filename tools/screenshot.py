"""
screenshot.py - NP21/W window screenshot (non-intrusive)

Win32 PrintWindow API で NP21/W ウィンドウをキャプチャ。
ウィンドウが裏に隠れていてもフォーカスを奪わずに撮影可能。

使い方:
    screenshot.py [出力パス]           NP21/Wウィンドウ (なければデスクトップ)
    screenshot.py [出力パス] --full    デスクトップ全体
"""

import sys
import os
import ctypes
import ctypes.wintypes

def find_np21w_window():
    """NP21/W ウィンドウハンドルを検索"""
    user32 = ctypes.windll.user32
    found = []

    # EnumWindows コールバック
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)

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

def capture_window(hwnd, output_path):
    """PrintWindow API でウィンドウをキャプチャ (フォーカス不要)"""
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32

    # ウィンドウの矩形取得
    rect = ctypes.wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width = rect.right - rect.left
    height = rect.bottom - rect.top

    if width <= 0 or height <= 0:
        return False

    # デバイスコンテキスト取得
    hwndDC = user32.GetWindowDC(hwnd)
    mfcDC = gdi32.CreateCompatibleDC(hwndDC)
    bitmap = gdi32.CreateCompatibleBitmap(hwndDC, width, height)
    gdi32.SelectObject(mfcDC, bitmap)

    # PrintWindow でキャプチャ (PW_RENDERFULLCONTENT = 2)
    result = user32.PrintWindow(hwnd, mfcDC, 2)
    if not result:
        # フォールバック: PW_CLIENTONLY なし
        user32.PrintWindow(hwnd, mfcDC, 0)

    # BITMAPINFOHEADER
    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ('biSize', ctypes.c_uint32),
            ('biWidth', ctypes.c_int32),
            ('biHeight', ctypes.c_int32),
            ('biPlanes', ctypes.c_uint16),
            ('biBitCount', ctypes.c_uint16),
            ('biCompression', ctypes.c_uint32),
            ('biSizeImage', ctypes.c_uint32),
            ('biXPelsPerMeter', ctypes.c_int32),
            ('biYPelsPerMeter', ctypes.c_int32),
            ('biClrUsed', ctypes.c_uint32),
            ('biClrImportant', ctypes.c_uint32),
        ]

    bmi = BITMAPINFOHEADER()
    bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.biWidth = width
    bmi.biHeight = -height  # top-down
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0  # BI_RGB

    # ピクセルデータ取得
    buf_size = width * height * 4
    buf = ctypes.create_string_buffer(buf_size)
    gdi32.GetDIBits(mfcDC, bitmap, 0, height, buf, ctypes.byref(bmi), 0)

    # 解放
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mfcDC)
    user32.ReleaseDC(hwnd, hwndDC)

    # BMP → PNG 変換 (PILが使えればPNG、なければBMP)
    try:
        from PIL import Image
        # BGRA → RGBA
        import array
        pixels = bytearray(buf.raw)
        for i in range(0, len(pixels), 4):
            pixels[i], pixels[i+2] = pixels[i+2], pixels[i]  # swap B,R
        img = Image.frombytes('RGBA', (width, height), bytes(pixels))
        img = img.convert('RGB')
        img.save(output_path)
    except ImportError:
        # PIL がない場合は BMP で保存
        bmp_path = output_path.rsplit('.', 1)[0] + '.bmp'
        with open(bmp_path, 'wb') as f:
            # BMP file header
            import struct
            file_size = 54 + buf_size
            f.write(b'BM')
            f.write(struct.pack('<I', file_size))
            f.write(struct.pack('<HH', 0, 0))
            f.write(struct.pack('<I', 54))
            # DIB header (top-down なので height を正に戻す)
            bmi.biHeight = height
            f.write(bytes(bmi))
            # pixel data (上下反転して書き出し)
            row_bytes = width * 4
            for y in range(height - 1, -1, -1):
                f.write(buf.raw[y * row_bytes:(y + 1) * row_bytes])
        output_path = bmp_path

    print("OK: {} ({}x{}, window)".format(output_path, width, height))
    return True

def capture_fullscreen(output_path):
    """pyautogui でデスクトップ全体をキャプチャ"""
    import pyautogui
    img = pyautogui.screenshot()
    img.save(output_path)
    w, h = img.size
    print("OK: {} ({}x{}, full)".format(output_path, w, h))
    return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        output = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'screenshot.png')
    else:
        output = sys.argv[1]

    full_screen = '--full' in sys.argv

    try:
        if not full_screen:
            result = find_np21w_window()
            if result:
                hwnd, title = result
                if capture_window(hwnd, output):
                    sys.exit(0)
                else:
                    print("WARN: PrintWindow failed, falling back to full screen")

        capture_fullscreen(output)
    except Exception as e:
        print("ERROR: {}".format(e), file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
