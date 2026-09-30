#!/usr/bin/env python3
"""
np21w_restart.py — NP21/Wを停止→INI書き換え→再起動

使い方:
    python3 np21w_restart.py [fdd1_path]
    
fdd1_pathが指定されればFDD1FILEを更新。
省略時はINIのFDD1FILEはそのまま。

**非推奨 (2026-09-25)**: 停止・起動は tools/np21w_ctl.py を使う。こちらは
停止後にプロセスの消滅も媒体のロック解除も待たずに起動するので、起動が
途中で止まることがある (docs/POLICY_DEBUG.md §4-60)。また FD を渡すと ini の
FDD1FILE を書き換える ([D2] の承認対象)。np21w_ctl.py は FD を引数で渡し、
ini は書き換えない。
"""

import sys
import os
import subprocess
import time

# WSLパス変換
def to_win_path(wsl_path):
    """WSLパスをWindowsパスに変換"""
    if wsl_path.startswith('/mnt/'):
        drive = wsl_path[5].upper()
        rest = wsl_path[6:].replace('/', '\\')
        return f'{drive}:{rest}'
    return wsl_path


# NP21W_DIR: 環境変数 → .env → .env.sample の順で解決 (nhd_deploy.py と同じ規約)
def _resolve_np21w_dir():
    """NP21W_DIR(WSLパス)を解決する"""
    if os.environ.get('NP21W_DIR'):
        return os.environ['NP21W_DIR']
    proj_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for env_file in ['.env', '.env.sample']:
        env_path = os.path.join(proj_dir, env_file)
        if os.path.isfile(env_path):
            with open(env_path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('NP21W_DIR='):
                        return line.split('=', 1)[1].strip()
    return "/tmp/np21w"


NP21W_DIR_WSL = _resolve_np21w_dir()
# PowerShellに渡すのでWindowsパス形式が必要。WIN_NP21W_DIRで直接上書きも可能。
NP21W_DIR = os.environ.get('WIN_NP21W_DIR') or to_win_path(NP21W_DIR_WSL)
NP21W_EXE = NP21W_DIR + '\\np21x64w.exe'
NP21W_INI = NP21W_DIR + '\\np21x64w.ini'

def stop_np21w():
    """NP21/Wプロセスを停止"""
    print("  NP21/W停止...", end='', flush=True)
    subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command',
         "Stop-Process -Name 'np21x64w' -Force -ErrorAction SilentlyContinue"],
        capture_output=True, timeout=10
    )
    time.sleep(2)
    print(" OK")

def update_ini_fdd1(fdd1_path):
    """INIファイルのFDD1FILEを更新"""
    win_path = to_win_path(fdd1_path) if fdd1_path.startswith('/') else fdd1_path
    
    # 書き換えはWSL側から行うのでWSLパスを使う
    ini_wsl = os.path.join(NP21W_DIR_WSL, 'np21x64w.ini')

    try:
        with open(ini_wsl, 'rb') as f:
            content = f.read()
        
        # FDD1FILEの行を書き換え (Shift-JIS混在のバイナリ処理)
        old_pattern = b'FDD1FILE='
        idx = content.find(old_pattern)
        if idx >= 0:
            # 行末を探す
            end_idx = content.find(b'\r\n', idx)
            if end_idx < 0:
                end_idx = content.find(b'\n', idx)
            if end_idx < 0:
                end_idx = len(content)
            
            new_line = b'FDD1FILE=' + win_path.encode('ascii', errors='replace')
            content = content[:idx] + new_line + content[end_idx:]
            
            with open(ini_wsl, 'wb') as f:
                f.write(content)
            
            print(f"  FDD1FILE={win_path}")
        else:
            print("  WARNING: FDD1FILE not found in INI")
    except Exception as e:
        print(f"  INI更新エラー: {e}")

def start_np21w():
    """NP21/Wを起動"""
    print("  NP21/W起動...", end='', flush=True)
    subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command',
         f"Start-Process -FilePath '{NP21W_EXE}' -WorkingDirectory '{NP21W_DIR}'"],
        capture_output=True, timeout=10
    )
    print(" OK")

def main():
    fdd1 = None
    if len(sys.argv) > 1:
        fdd1 = sys.argv[1]
    
    print("=== NP21/W 再起動 ===")
    stop_np21w()
    
    if fdd1:
        update_ini_fdd1(fdd1)
    
    start_np21w()
    print("=== 完了 ===")

if __name__ == '__main__':
    main()
