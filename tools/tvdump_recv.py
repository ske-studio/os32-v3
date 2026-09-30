#!/usr/bin/env python3
"""
tvdump_recv.py — OS32テキストVRAMダンプ受信・表示

rcmdでtvdumpコマンドを送信し、バイナリデータを受信してANSI端末に表示する

使い方:
    python3 tools/tvdump_recv.py
"""

import subprocess
import sys
import struct

PIPE_NAME = 'np21w_com1'

# PC-98テキストVRAMアトリビュート → ANSIカラー変換
# PC-98 attr: bit 2-0 = color (GRB), bit 3 = 未使用, etc.
# アトリビュート構造: bit 7-5: 色(上位), bit 4: アンダーライン, etc.
# 実際のPC-98: 0xE1=白, 0xA1=シアン, 0xC1=黄, 0x81=緑, 0x41=赤

def pc98_attr_to_ansi(attr):
    """PC-98テキストアトリビュート→ANSIエスケープシーケンス"""
    # PC-98のカラーコード（上位3ビット）
    # C1=赤(bit6), A1=シアン(bit5+7), 81=緑(bit7), E1=白(bit7+6+5)
    color_bits = (attr >> 5) & 0x07
    
    ansi_map = {
        0: '30',   # 000 = 黒
        1: '34',   # 001 = 青
        2: '31',   # 010 = 赤
        3: '35',   # 011 = 紫
        4: '32',   # 100 = 緑
        5: '36',   # 101 = シアン
        6: '33',   # 110 = 黄
        7: '37',   # 111 = 白
    }
    return f"\033[{ansi_map.get(color_bits, '37')}m"

def receive_tvdump():
    """tvdumpコマンドを送信してバイナリデータを受信"""
    
    # コマンド送信 + バイナリデータ受信
    # tvdumpの出力: "TVDM" + cols(1) + rows(1) + data(cols*rows*2) + palette(48) + テキスト出力 + EOT
    # 合計: 6 + 4000 + 48 = 4054 バイト (バイナリ部分)
    
    cmd_bytes = b'tvdump\x0A'
    byte_array = ','.join(str(b) for b in cmd_bytes)
    
    ps = (
        "$ErrorActionPreference = 'Stop'; "
        "try { "
        "  $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', "
        f"'{PIPE_NAME}', [System.IO.Pipes.PipeDirection]::InOut); "
        "  $pipe.Connect(5000); "
        f"  [byte[]]$data = @({byte_array}); "
        "  $pipe.Write($data, 0, $data.Length); "
        "  $pipe.Flush(); "
        # バイナリデータ受信 (最大8192バイト)
        "  $buf = New-Object byte[] 8192; "
        "  $total = 0; "
        "  $timeout = [DateTime]::Now.AddSeconds(10); "
        "  while ($total -lt 8192 -and [DateTime]::Now -lt $timeout) { "
        "    $n = $pipe.Read($buf, $total, 8192 - $total); "
        "    if ($n -le 0) { Start-Sleep -Milliseconds 10; continue; } "
        "    $total += $n; "
        # EOT(0x04)を見つけたら終了
        "    $found = $false; "
        "    for ($i = 0; $i -lt $total; $i++) { "
        "      if ($buf[$i] -eq 4) { $found = $true; break; } "
        "    }; "
        "    if ($found) { break; } "
        "    $timeout = [DateTime]::Now.AddSeconds(5); "
        "  }; "
        # Base64でエンコードして出力
        "  $result = [Convert]::ToBase64String($buf, 0, $total); "
        "  Write-Host $result; "
        "  $pipe.Close(); "
        "} catch { "
        "  Write-Host ('ERROR: ' + $_); "
        "}"
    )
    
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command', ps],
        capture_output=True, timeout=30
    )
    
    stdout = result.stdout.decode('utf-8', errors='replace').strip()
    
    if stdout.startswith('ERROR:'):
        print(f"エラー: {stdout}", file=sys.stderr)
        return None
    
    # Base64デコード
    import base64
    try:
        raw = base64.b64decode(stdout)
    except Exception as e:
        print(f"Base64デコードエラー: {e}", file=sys.stderr)
        print(f"受信データ先頭: {stdout[:100]}", file=sys.stderr)
        return None
    
    return raw

def parse_and_display(raw):
    """受信データを解析してANSI端末に表示"""
    
    # rshellのエコー "> tvdump\n" をスキップしてTVDMヘッダを探す
    tvdm_pos = -1
    for i in range(len(raw) - 4):
        if raw[i:i+4] == b'TVDM':
            tvdm_pos = i
            break
    
    if tvdm_pos < 0:
        print("エラー: TVDMヘッダが見つかりません", file=sys.stderr)
        print(f"受信データ ({len(raw)} bytes):", file=sys.stderr)
        print(raw[:100], file=sys.stderr)
        return
    
    pos = tvdm_pos + 4
    cols = raw[pos]; pos += 1
    rows = raw[pos]; pos += 1
    
    print(f"テキストVRAMダンプ: {cols}×{rows}")
    print("=" * cols)
    
    # テキストデータ表示
    for row in range(rows):
        line = ""
        prev_attr = -1
        for col in range(cols):
            idx = pos + (row * cols + col) * 2
            if idx + 1 >= len(raw):
                break
            ch = raw[idx]
            at = raw[idx + 1]
            
            # ANSIカラー
            if at != prev_attr:
                line += pc98_attr_to_ansi(at)
                prev_attr = at
            
            # 表示可能文字のみ
            if 0x20 <= ch < 0x7F:
                line += chr(ch)
            else:
                line += ' '
        
        line += "\033[0m"  # リセット
        print(line.rstrip())
    
    # パレットデータ
    pal_pos = pos + cols * rows * 2
    if pal_pos + 48 <= len(raw):
        print("\n" + "=" * 40)
        print("パレット (4bit RGB):")
        for i in range(16):
            r = raw[pal_pos + i * 3]
            g = raw[pal_pos + i * 3 + 1]
            b = raw[pal_pos + i * 3 + 2]
            # 4bit→8bit変換してANSI表示
            r8 = r * 17
            g8 = g * 17
            b8 = b * 17
            color_block = f"\033[48;2;{r8};{g8};{b8}m  \033[0m"
            print(f"  [{i:2d}] R={r:2d} G={g:2d} B={b:2d}  {color_block}")

if __name__ == '__main__':
    print("OS32 テキストVRAMダンプ受信中...")
    raw = receive_tvdump()
    if raw:
        parse_and_display(raw)
