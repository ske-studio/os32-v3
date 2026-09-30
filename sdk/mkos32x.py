#!/usr/bin/env python3
"""
mkos32x.py - フラットバイナリに OS32X ヘッダを付加する

使い方:
    python3 mkos32x.py input.bin output.bin [オプション]

オプション:
    --elf FILE     生成したELFファイルからBSSサイズを自動検出
    --heap SIZE    要求ヒープサイズ (バイト, デフォルト: 0=64KB)
    --bss SIZE     BSSサイズ (バイト, 手動指定。--elfと併用時はelfが優先)
    --api VER      最低APIバージョン (デフォルト: 1)
    --entry OFF    エントリポイントオフセット (デフォルト: 0)
    --gfx          GFXフラグを設定
    --ring3        CPL=3 (リング3) フラグを設定 (v2 M1)
    --cpl0         CPL=0 強制フラグを設定 (ring3 デフォルト化後のエスケープ, v2 M3)
    --shlib        共有ライブラリフラグを設定 (MEM_SHLIB_BASE 常駐, GUI v1.1 K3)
    --cui-only     CUI 専用フラグを設定 (GUI からの起動を断る, 票 T8-2)
    --launcher     起動要求 (launch_req) を出してよい宣言 (票 T9 D3)
    --load ADDR    リンク時のロードアドレス (--elf 指定時は ELF の .text から自動)

ヘッダ v3 (48 バイト、票 docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md):
    末尾に kapi_data_off (KernelAPI のデータ欄のオフセット) を焼く。値は ELF の
    非ロードのセクション .os32_kapi_layout (crt0 / os32api の刻印) から取る。
    **--elf は必須**で、刻印が無ければ失敗する。min_api_ver は 63 未満なら 63 に
    引き上げる (v3 の照合を持たない旧カーネルが受け入れないように)。
    ヘッダの組み立ては同じディレクトリの os32x_hdr.py (tools/mkshlib.py と共通)。
"""

import sys
import os

# 共通モジュール (sdk/os32x_hdr.py、SDK では bin/os32x_hdr.py)。
# このファイルと同じディレクトリに置く。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os32x_hdr as H  # noqa: E402

OS32X_FLAG_GFX = H.OS32X_FLAG_GFX
OS32X_FLAG_RING3 = H.OS32X_FLAG_RING3
OS32X_FLAG_FORCE_CPL0 = H.OS32X_FLAG_FORCE_CPL0
OS32X_FLAG_SHLIB = H.OS32X_FLAG_SHLIB
OS32X_FLAG_CUI_ONLY = H.OS32X_FLAG_CUI_ONLY
OS32X_FLAG_LAUNCHER = H.OS32X_FLAG_LAUNCHER


def main():
    if len(sys.argv) < 3:
        print("使い方: mkos32x.py <input.bin> <output.bin> [options]")
        print("  --elf FILE    ELFファイルからBSSサイズ自動検出")
        print("  --heap SIZE   要求ヒープサイズ (バイト)")
        print("  --bss SIZE    BSSサイズ (バイト)")
        print("  --api VER     最低APIバージョン")
        print("  --entry OFF   エントリポイントオフセット")
        print("  --gfx         GFXフラグ設定")
        print("  --ring3       CPL=3 リング3フラグ設定")
        print("  --cpl0        CPL=0 強制フラグ設定")
        print("  --shlib       共有ライブラリフラグ設定")
        print("  --cui-only    CUI 専用フラグ設定 (GUI から起動しない)")
        print("  --launcher    起動要求フラグ設定 (launch_req を呼べる, 票 T9)")
        print("  --load ADDR   ロードアドレス (--elf があれば自動検出)")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    # オプション解析
    heap_size = 0
    bss_size = 0
    min_api_ver = 1
    entry_offset = 0
    load_addr = 0
    flags = 0
    elf_path = None

    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == '--heap' and i + 1 < len(sys.argv):
            heap_size = int(sys.argv[i + 1], 0)
            i += 2
        elif sys.argv[i] == '--bss' and i + 1 < len(sys.argv):
            bss_size = int(sys.argv[i + 1], 0)
            i += 2
        elif sys.argv[i] == '--api' and i + 1 < len(sys.argv):
            min_api_ver = int(sys.argv[i + 1], 0)
            i += 2
        elif sys.argv[i] == '--entry' and i + 1 < len(sys.argv):
            entry_offset = int(sys.argv[i + 1], 0)
            i += 2
        elif sys.argv[i] == '--elf' and i + 1 < len(sys.argv):
            elf_path = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == '--gfx':
            flags |= OS32X_FLAG_GFX
            i += 1
        elif sys.argv[i] == '--ring3':
            flags |= OS32X_FLAG_RING3
            i += 1
        elif sys.argv[i] == '--cpl0':
            flags |= OS32X_FLAG_FORCE_CPL0
            i += 1
        elif sys.argv[i] == '--shlib':
            flags |= OS32X_FLAG_SHLIB
            i += 1
        elif sys.argv[i] == '--cui-only':
            flags |= OS32X_FLAG_CUI_ONLY
            i += 1
        elif sys.argv[i] == '--launcher':
            flags |= OS32X_FLAG_LAUNCHER
            i += 1
        elif sys.argv[i] == '--load' and i + 1 < len(sys.argv):
            load_addr = int(sys.argv[i + 1], 0)
            i += 2
        else:
            print(f"不明なオプション: {sys.argv[i]}")
            sys.exit(1)

    # ヘッダ v3 は ELF の刻印からしか作らない (票 TASK_KAPI_DATA_FIELDS)。
    if not elf_path:
        print("mkos32x: --elf <file.elf> が要る (ヘッダ v3 の kapi_data_off を "
              "ELF の .os32_kapi_layout から取る)", file=sys.stderr)
        sys.exit(1)

    try:
        elf = H.Elf32(elf_path)

        # ELF から BSS サイズ (--bss 手動指定より優先)
        if elf.bss_size() > 0:
            bss_size = elf.bss_size()

        # ELF からエントリポイントとロードアドレス。--entry / --load で
        # 手動指定されていなければ ELF の値を使う。
        text_addr = elf.text_addr()
        if text_addr is None:
            print(f"  警告: .text セクションが見つかりません ({elf_path})")
        else:
            elf_entry = elf.e_entry - text_addr
            if elf_entry < 0:
                print(f"  警告: エントリポイントが .text より前にあります ({elf_path})")
            else:
                if entry_offset == 0:
                    entry_offset = elf_entry
                    if entry_offset != 0:
                        print(f"  注意: ELFからエントリオフセット自動検出: 0x{entry_offset:X}")
                if load_addr == 0:
                    load_addr = text_addr

        # KAPI データ欄の配置 (刻印が無い・食い違うなら失敗)
        kapi_data_off = H.read_kapi_layout(elf)

        # 入力ファイル読み込み。ELF と世代が違えば止める。
        with open(input_path, 'rb') as f:
            code_data = f.read()
        H.check_raw_matches_elf(elf, code_data, input_path)

        text_size = len(code_data)
        eff_api = H.effective_min_api(min_api_ver)
        header = H.build_header(flags, entry_offset, text_size, bss_size,
                                heap_size, min_api_ver, load_addr, kapi_data_off)
    except H.HeaderError as e:
        print(f"mkos32x: {e}", file=sys.stderr)
        sys.exit(1)

    # 出力
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(code_data)

    total = len(header) + text_size
    print(f"  OS32X: {os.path.basename(output_path)} "
          f"(text={text_size}, bss={bss_size}, heap={heap_size}, "
          f"entry=0x{entry_offset:X}, load=0x{load_addr:X}, "
          f"api>={eff_api}, kapi_data=0x{kapi_data_off:X}, total={total})")

if __name__ == '__main__':
    main()
