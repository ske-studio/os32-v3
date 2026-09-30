;; ============================================================
;; loader_fat_new.asm — FAT12対応 新ローダー (vmkernel.lz4)
;;
;; boot_fat.asmによって0000:8000hにロードされる
;;
;; 処理:
;;   1. FAT12からVMKRNL.LZ4を検索・メモリにロード
;;   2. A20ゲート有効化
;;   3. GDT設定 → プロテクトモード遷移
;;   4. PM後: VK32 v2 の検査 (長さ・範囲・CRC32) + LZ4展開 → entry[i].load_addr
;;      (pm_vk32_boot。HDD ローダの boot/vk32_boot.c と同じ順序・同じ VK32_ERR_*)
;;   5. 起動したイメージの CRC をブート情報域 (0x7E00) のイメージ欄へ
;;   6. メモリプローブ → カーネルにジャンプ
;;
;; FAT チェーンは「ファイル長から決まるクラスタ数」だけ読み、その先が EOC で
;; あることを確かめる (早期終端・範囲外・循環 = 長すぎる を止める)。
;; 票: docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-4 / §1-v3「FD ローダ」
;;
;; VMKRNL.LZ4のメモリ配置 (リアルモード読み込み):
;;   1000:0000h〜 (物理 0x10000〜0x8EFFF、MAX_IMAGE_SIZE まで) へ直接読む。
;;   64KB ごとにセグメントを切り替える。PM へ移った後の再配置は無い
;;   (以前の Phase 1 = 0:C000h への読み込みと PM での統合は廃止済み)。
;;
;; ジオメトリ (下の %ifdef FD144 の表、正典は tools/mkfat12.py の GEOMETRIES):
;;   2HD 1232KB: FAT 1-2 / ルートDir 5-10 / データ 11〜 (1024B/セクタ)
;;   1.44MB    : FAT 1-9 / ルートDir 19-30 / データ 31〜 (512B/セクタ)
;; ============================================================

cpu 386


;; ジオメトリ — `-DFD144` で 1.44MB 版。正典は tools/mkfat12.py の GEOMETRIES。
;; **boot/boot_fat.asm と必ず同じ値にすること** (片方だけ直すと静かにずれる)。
%ifdef FD144
SECT_SZ     EQU     0200h
SECT_SHIFT  EQU     9           ;; log2(SECT_SZ)
SECT_N      EQU     02h
SPT         EQU     18
DA_UA       EQU     030h        ;; 1.44MB 対応両用 I/F ユニット0
ROOT_START  EQU     19
ROOT_SECTS  EQU     12
FAT_SECTS   EQU     9
DATA_START  EQU     31
TOTAL_SECTS EQU     2880
%else
SECT_SZ     EQU     0400h
SECT_SHIFT  EQU     10          ;; log2(SECT_SZ)
SECT_N      EQU     03h
SPT         EQU     8
DA_UA       EQU     090h
ROOT_START  EQU     5
ROOT_SECTS  EQU     6
FAT_SECTS   EQU     2
DATA_START  EQU     11
TOTAL_SECTS EQU     1232
%endif
;; 有効なクラスタ番号は [2, MAX_CLUSTER)。1 クラスタ = 1 セクタ (mkfat12 の spc=1)
MAX_CLUSTER EQU     TOTAL_SECTS - DATA_START + 2

ROOT_ENTS   EQU     192
FAT_BUF     EQU     6000h
FAT_START   EQU     1

;; ブート情報域 (0x7E00) の番地とオフセット。正典は include/bootinfo.h。
%include "boot/bootinfo.inc"

section .text


        org 8000h

global loader_start
loader_start:
        xor     ax, ax
        mov     ds, ax
        mov     ss, ax
        mov     sp, 7C00h

        ;; TVRAM表示
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 160
        mov     si, msg_loader
        call    print16

        ;; ES = 0 に戻す
        xor     ax, ax
        mov     es, ax

        ;; ============================================================
        ;; ブート情報域 (0x7E00〜0x7E3F、v2) — HDD の BIOS 幾何を測る
        ;; (票 TASK_HDD_INSTALL 段 0)。**起動のたびにまず無効にしてから**
        ;; DA=80h / 81h に INT 1Bh AH=84h (新センス)。失敗 (CF=1) も
        ;; cf / ah として残す。最後に magic と反転チェック語を書く。
        ;; 0x7E00 はスタック (0x7C00 から下)、FAT/ルートDir の
        ;; バッファ (0x6000〜0x77FF)、ローダ本体 (0x8000〜) のどれとも
        ;; 重ならない。
        ;; ============================================================
        sti
        mov     al, BOOTINFO_SRC_FD
        call    bi_clear
        mov     al, 80h
        mov     si, MEM_BOOTINFO_BASE + BI_OFF_DRIVE0
        call    bi_sense
        mov     al, 81h
        mov     si, MEM_BOOTINFO_BASE + BI_OFF_DRIVE0 + BI_DRIVE_SIZE
        call    bi_sense
        call    bi_seal
        xor     ax, ax
        mov     es, ax

        ;; ============================================================
        ;; ルートDirを0:6000にロード
        ;; ============================================================
        mov     cx, ROOT_SECTS
        mov     ax, ROOT_START
        mov     bp, FAT_BUF
.rd_loop:
        push    cx
        push    ax
        call    read_sect16
        pop     ax
        pop     cx
        inc     ax
        add     bp, SECT_SZ
        loop    .rd_loop

        ;; ============================================================
        ;; VMKRNL.LZ4を検索 (8.3形式: "VMKRNL  LZ4")
        ;; ============================================================
        mov     di, FAT_BUF
        mov     cx, ROOT_ENTS
.scan_kern:
        mov     al, es:[di]
        cmp     al, 0
        je      .no_kernel
        cmp     al, 0E5h
        je      .scan_knext

        push    cx
        push    di
        mov     si, kern_name
        mov     cx, 11
        repe    cmpsb
        pop     di
        pop     cx
        je      .found_kern

.scan_knext:
        add     di, 32
        loop    .scan_kern

.no_kernel:
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320
        mov     si, msg_nokernel
        call    print16
.halt16:
        hlt
        jmp     .halt16

.found_kern:
        ;; 開始クラスタとサイズ取得
        mov     ax, es:[di + 1Ah]
        mov     word [var_cluster], ax
        mov     eax, es:[di + 1Ch]      ;; ファイル長 (u32、ディレクトリの 1Ch〜1Fh)
        mov     dword [var_size], eax

        ;; ============================================================
        ;; FATテーブルを0:6000にロード (FAT_SECTS セクタ)
        ;; 1.44MB は 9 セクタある。2 セクタ決め打ちだと後ろのクラスタで化ける。
        ;; (ルートDir のバッファを上書きする — 開始クラスタと長さは写し済み)
        ;; ============================================================
        xor     ax, ax
        mov     es, ax
        mov     cx, FAT_SECTS
        mov     ax, FAT_START
        mov     bp, FAT_BUF
.fat_loop:
        push    cx
        push    ax
        call    read_sect16
        pop     ax
        pop     cx
        inc     ax
        add     bp, SECT_SZ
        loop    .fat_loop

        ;; ============================================================
        ;; 読む前にチェーン全体を検査する (fat_chain_check)。
        ;; 長さ 0 < size <= MAX_IMAGE_SIZE、クラスタ数 = ceil(size / SECT_SZ) だけ
        ;; 辿って全部 [2, MAX_CLUSTER) にあり、その次が EOC であること。
        ;; ============================================================
        movzx   eax, word [var_cluster]
        mov     edx, dword [var_size]
        mov     esi, FAT_BUF
        call    fat_chain_check
        test    eax, eax
        jnz     .fat_bad
        mov     word [var_left], cx

        ;; ============================================================
        ;; VMKRNL.LZ4をメモリにロード (検査済みのチェーンを同じ fat12_next32 で辿る)
        ;; 1000:0000h〜 (物理0x10000以降, 64KB毎にセグメント切替)
        ;; ============================================================
        movzx   eax, word [var_cluster]
        mov     word [var_load_seg], 1000h
        xor     bp, bp

.load_kern:
        push    ax
        sub     ax, 2
        add     ax, DATA_START

        push    es
        mov     bx, word [var_load_seg]
        mov     es, bx
        call    read_sect16
        pop     es

        pop     ax

        add     bp, SECT_SZ

        ;; 64KB境界チェック (0x200 も 0x400 も 0x10000 を割り切るので
        ;; bp が 0 に戻ったところがちょうど境界)
        or      bp, bp
        jnz     .seg_ok

        ;; セグメント切替
        mov     bx, word [var_load_seg]
        add     bx, 1000h       ;; +64KB
        mov     word [var_load_seg], bx

.seg_ok:
        ;; INT 1Bh が 32 ビットレジスタの上半分を保つとは限らないので揃え直す
        movzx   eax, ax
        mov     esi, FAT_BUF
        call    fat12_next32
        dec     word [var_left]
        jnz     .load_kern
        jmp     .load_done

.fat_bad:
        ;; EAX = FATCHK_* (1〜4)
        mov     si, msg_badsize
        cmp     ax, FATCHK_SIZE
        je      .fat_err
        mov     si, msg_fatstart
        cmp     ax, FATCHK_START
        je      .fat_err
        mov     si, msg_fatshort
        cmp     ax, FATCHK_SHORT
        je      .fat_err
        mov     si, msg_fatrange
        cmp     ax, FATCHK_RANGE
        je      .fat_err
        mov     si, msg_fatlong
.fat_err:
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320
        call    print16
.fat_halt:
        hlt
        jmp     .fat_halt

.load_done:
        ;; ============================================================
        ;; PM遷移
        ;; ============================================================
        push    es
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320
        mov     si, msg_pm
        call    print16
        pop     es

        ;; A20ゲート有効化
        mov     al, 3
        out     0F2h, al

        cli
        xor     al, al
        out     50h, al

        ;; GDTロード
        xor     eax, eax
        mov     ax, cs
        shl     eax, 4
        add     eax, gdt
        mov     dword [gdtr_base], eax
        lgdt    [gdtr]

        mov     eax, cr0
        or      al, 1
        mov     cr0, eax

        ;; far jmp → PM
        db      066h
        db      0EAh
        dd      pm_entry32
        dw      0008h


;; ============================================================
;; 32bit PM コード
;; ============================================================

bits 32

pm_entry32:
        mov     eax, 10h
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, 0009FFFCh

        ;; ファイルは 0x10000 (1000:0000h) 以降に直接配置されている。
        ;; === VK32 v2 の検査 + LZ4展開 (pm_vk32_boot) ===
        push    dword vk32_img_crc              ;; out_crc
        push    dword VK32_LOAD_MIN             ;; window = 帯の先頭そのもの
        push    dword [var_size]                ;; ファイル長 (ディレクトリの値)
        push    dword 10000h                    ;; file
        call    pm_vk32_boot
        add     esp, 16
        test    eax, eax
        jnz     .vk32_fail

        ;; 起動したイメージの CRC をブート情報域のイメージ欄へ
        ;; (include/bootinfo.h)。チェック語を**最後に**書く。
        mov     eax, [vk32_img_crc]
        mov     [MEM_BOOTINFO_BASE + BI_OFF_IMG_CRC], eax
        mov     edx, [var_size]
        mov     [MEM_BOOTINFO_BASE + BI_OFF_IMG_SIZE], edx
        xor     eax, edx
        xor     eax, BOOTINFO_IMG_KEY
        mov     [MEM_BOOTINFO_BASE + BI_OFF_IMG_CHECK], eax

        ;; TVRAM: デコード完了
        mov     edi, 0A0000h + 480
        mov     esi, msg_ok32
        call    pm_print32

        ;; === メモリプロービング ===
        mov     esi, 00100000h
        mov     ecx, 1024

.probe_loop:
        mov     eax, [esi]
        mov     dword [esi], 0AA55AA55h
        cmp     dword [esi], 0AA55AA55h
        jne     .probe_done
        mov     dword [esi], 055AA55AAh
        cmp     dword [esi], 055AA55AAh
        jne     .probe_done
        mov     [esi], eax
        add     ecx, 512
        add     esi, 00080000h
        cmp     esi, 01000000h
        jb      .probe_loop

.probe_done:
        ;; === カーネルにジャンプ ===
        mov     eax, DA_UA
        push    eax             ;; boot_drive
        push    ecx             ;; mem_kb
        push    dword 0         ;; ダミーリターンアドレス

        db      0EAh
        dd      00100000h
        dw      0008h

.vk32_fail:
        ;; EAX = VK32_ERR_* (-1〜VK32_ERR_MIN)。表の外は汎用の文言
        neg     eax
        cmp     eax, -VK32_ERR_MIN
        jbe     .vk32_msg
        xor     eax, eax
.vk32_msg:
        mov     esi, [vk32_msgs + eax * 4]
        mov     edi, 0A0000h + 480
        call    pm_print32
        jmp     pm_halt


;; >>> VK32_HOST_BEGIN
;; ============================================================
;; ここから VK32_HOST_END までを tools/tests/test_vk32_crc.py (と
;; test_vmkernel_lz4.py) が**そのまま切り出して** nasm -f elf32 で組み、
;; HDD 側の C (boot/vk32_boot.c) と同じ壊れたイメージを渡す。
;; 外の番地・EQU に頼らないこと (必要な値はこの中に置く)。
;; ============================================================
bits 32

;; VK32 v2 (正典は boot/boot_defs.h。名前ごとの一致は test_vk32_crc.py)
VK32_MAGIC          EQU 32334B56h
VK32_VERSION        EQU 2
VK32_MAX_ENTRIES    EQU 4
VK32_LOAD_MIN       EQU 0x100000
VK32_LOAD_END       EQU 0x2E8000
MAX_IMAGE_SIZE      EQU 7F000h          ;; (508*1024)
VK32_ERR_SIZE       EQU -1
VK32_ERR_MAGIC      EQU -2
VK32_ERR_VERSION    EQU -3
VK32_ERR_COUNT      EQU -4
VK32_ERR_HEADER     EQU -5
VK32_ERR_LENGTH     EQU -6
VK32_ERR_FILE_CRC   EQU -7
VK32_ERR_SRC        EQU -8
VK32_ERR_DST        EQU -9
VK32_ERR_DECODE     EQU -10
VK32_ERR_RAW_SIZE   EQU -11
VK32_ERR_ENTRY_CRC  EQU -12
VK32_ERR_MIN        EQU -12

;; LZ4デコーダ定数
LZ4_MINMATCH        EQU 4

;; ============================================================
;; pm_vk32_boot — VK32 v2 の検査と展開 (32bit PM, cdecl)
;;
;; int pm_vk32_boot(const u8 *file, u32 size, u8 *window, u32 *out_crc)
;;   エントリ i は window + (load_addr - VK32_LOAD_MIN) へ展開する
;;   (ローダでは window = VK32_LOAD_MIN)。
;; 戻り値: 0 = 成功 (*out_crc = image_crc)、負 = VK32_ERR_*
;; 検査の順序は boot/vk32_boot.c の頭と同じ。
;; 局所: [ebp-4] n, [ebp-8] hsz, [ebp-12] crc_off, [ebp-16] i,
;;       [ebp-20] addr / dst, [ebp-24] raw
;; ============================================================
pm_vk32_boot:
        push    ebp
        mov     ebp, esp
        sub     esp, 24
        push    ebx
        push    esi
        push    edi
        cld

        mov     esi, [ebp+8]            ;; file
        mov     ecx, [ebp+12]           ;; size
        ;; 1. 長さ
        cmp     ecx, 16
        jb      .e_size
        cmp     ecx, MAX_IMAGE_SIZE
        ja      .e_size
        ;; 2. 共通部
        cmp     dword [esi], VK32_MAGIC
        jne     .e_magic
        cmp     dword [esi + 8], VK32_VERSION
        jne     .e_version
        mov     edx, [esi + 12]
        test    edx, edx
        jz      .e_count
        cmp     edx, VK32_MAX_ENTRIES
        ja      .e_count
        mov     [ebp-4], edx
        ;; 3. header_size = 16 + 20n + 8
        lea     ebx, [edx + edx * 4]    ;; 5n
        lea     ebx, [ebx * 4 + 24]     ;; 20n + 24
        cmp     [esi + 4], ebx
        jne     .e_header
        cmp     ebx, ecx
        ja      .e_header
        mov     [ebp-8], ebx
        ;; 4. 完全長 (image_size は hsz - 8)
        cmp     [esi + ebx - 8], ecx
        jne     .e_length
        ;; 5. ファイル全体の CRC32 (image_crc = hsz - 4 の欄を 0 として)
        lea     edi, [ebx - 4]
        mov     [ebp-12], edi
        push    edi
        push    esi
        push    dword 0FFFFFFFFh
        call    pm_crc32_update
        add     esp, 12
        push    dword 4
        push    dword vk32_zero4
        push    eax
        call    pm_crc32_update
        add     esp, 12
        mov     edx, [ebp+12]
        sub     edx, edi
        sub     edx, 4
        lea     ecx, [esi + edi + 4]
        push    edx
        push    ecx
        push    eax
        call    pm_crc32_update
        add     esp, 12
        not     eax
        cmp     eax, [esi + edi]
        jne     .e_file_crc

        ;; 6. 全エントリの範囲 (展開の前に)
        mov     dword [ebp-16], 0
.rng_loop:
        mov     ebx, [ebp-16]
        cmp     ebx, [ebp-4]
        jae     .rng_done
        shl     ebx, 4
        lea     edi, [esi + ebx + 16]   ;; &entry[i]
        ;; 入力: hsz <= off <= size かつ csz <= size - off
        mov     edx, [edi + 8]
        cmp     edx, [ebp-8]
        jb      .e_src
        mov     ecx, [ebp+12]
        cmp     edx, ecx
        ja      .e_src
        sub     ecx, edx
        cmp     [edi + 12], ecx
        ja      .e_src
        ;; 展開先: raw != 0、MIN <= addr < END、raw <= END - addr
        mov     edx, [edi]
        mov     ecx, [edi + 4]
        test    ecx, ecx
        jz      .e_dst
        cmp     edx, VK32_LOAD_MIN
        jb      .e_dst
        cmp     edx, VK32_LOAD_END
        jae     .e_dst
        mov     eax, VK32_LOAD_END
        sub     eax, edx
        cmp     ecx, eax
        ja      .e_dst
        mov     [ebp-20], edx
        mov     [ebp-24], ecx
        ;; 前のエントリと重ならない: addr < a2 + r2 かつ a2 < addr + raw なら重なる
        xor     eax, eax
.ovl_loop:
        cmp     eax, [ebp-16]
        jae     .ovl_done
        mov     ecx, eax
        shl     ecx, 4
        lea     ecx, [esi + ecx + 16]
        mov     edx, [ecx]
        add     edx, [ecx + 4]
        cmp     [ebp-20], edx
        jae     .ovl_next
        mov     edx, [ebp-20]
        add     edx, [ebp-24]
        cmp     [ecx], edx
        jb      .e_dst
.ovl_next:
        inc     eax
        jmp     .ovl_loop
.ovl_done:
        inc     dword [ebp-16]
        jmp     .rng_loop
.rng_done:

        ;; 7. 展開 → decoded == raw_size → 展開後の CRC32
        mov     dword [ebp-16], 0
.dec_loop:
        mov     ebx, [ebp-16]
        cmp     ebx, [ebp-4]
        jae     .dec_done
        shl     ebx, 4
        lea     edi, [esi + ebx + 16]   ;; &entry[i]
        mov     edx, [edi]
        sub     edx, VK32_LOAD_MIN
        add     edx, [ebp+16]           ;; dst = window + (addr - MIN)
        mov     [ebp-20], edx
        mov     eax, [edi + 8]
        add     eax, esi                ;; src = file + data_offset
        push    dword [edi + 4]         ;; cap = raw_size
        push    edx
        push    dword [edi + 12]
        push    eax
        call    pm_lz4_decode
        add     esp, 16
        test    eax, eax
        js      .e_decode
        cmp     eax, [edi + 4]
        jne     .e_raw_size
        push    dword [edi + 4]
        push    dword [ebp-20]
        push    dword 0FFFFFFFFh
        call    pm_crc32_update
        add     esp, 12
        not     eax
        mov     ebx, [ebp-16]
        mov     edx, [ebp-4]
        shl     edx, 4
        add     edx, esi                ;; edx + 16 = &entry_crc[0]
        cmp     eax, [edx + ebx * 4 + 16]
        jne     .e_entry_crc
        inc     dword [ebp-16]
        jmp     .dec_loop
.dec_done:
        mov     edi, [ebp-12]
        mov     eax, [esi + edi]
        mov     edx, [ebp+20]
        mov     [edx], eax
        xor     eax, eax
        jmp     .ret

.e_size:        mov     eax, VK32_ERR_SIZE
                jmp     .ret
.e_magic:       mov     eax, VK32_ERR_MAGIC
                jmp     .ret
.e_version:     mov     eax, VK32_ERR_VERSION
                jmp     .ret
.e_count:       mov     eax, VK32_ERR_COUNT
                jmp     .ret
.e_header:      mov     eax, VK32_ERR_HEADER
                jmp     .ret
.e_length:      mov     eax, VK32_ERR_LENGTH
                jmp     .ret
.e_file_crc:    mov     eax, VK32_ERR_FILE_CRC
                jmp     .ret
.e_src:         mov     eax, VK32_ERR_SRC
                jmp     .ret
.e_dst:         mov     eax, VK32_ERR_DST
                jmp     .ret
.e_decode:      mov     eax, VK32_ERR_DECODE
                jmp     .ret
.e_raw_size:    mov     eax, VK32_ERR_RAW_SIZE
                jmp     .ret
.e_entry_crc:   mov     eax, VK32_ERR_ENTRY_CRC
.ret:
        ;; どの出口でも ebp から戻す (途中の push が残っていても正しい)
        lea     esp, [ebp - 24 - 12]
        pop     edi
        pop     esi
        pop     ebx
        mov     esp, ebp
        pop     ebp
        ret


;; ============================================================
;; pm_crc32_update — CRC-32 (IEEE 802.3) の未確定状態を進める (cdecl)
;;
;; u32 pm_crc32_update(u32 state, const u8 *data, u32 len)
;;   lib/crc32_core.inc と同じ nibble 表。最初は 0FFFFFFFFh、最後に not。
;;   壊す: EAX ECX EDX (EBX ESI EDI は保存)
;; ============================================================
pm_crc32_update:
        push    ebp
        mov     ebp, esp
        push    ebx
        push    esi
        mov     eax, [ebp+8]
        mov     esi, [ebp+12]
        mov     ecx, [ebp+16]
        test    ecx, ecx
        jz      .done
.lp:
        xor     al, [esi]
        inc     esi
        mov     ebx, eax
        and     ebx, 0Fh
        shr     eax, 4
        xor     eax, [crc32_nib + ebx * 4]
        mov     ebx, eax
        and     ebx, 0Fh
        shr     eax, 4
        xor     eax, [crc32_nib + ebx * 4]
        dec     ecx
        jnz     .lp
.done:
        pop     esi
        pop     ebx
        pop     ebp
        ret


;; ============================================================
;; pm_lz4_decode — LZ4ブロックデコーダ (32bit PM, cdecl)
;;
;; int pm_lz4_decode(u8 *src, int csz, u8 *dst, int cap)
;; スタック: [ebp+8]=src, [ebp+12]=csz, [ebp+16]=dst, [ebp+20]=cap
;; 戻り値: EAX = 展開バイト数、-1 = 入力の異常、-2 = 出力が容量を超える
;; (boot/lz4_mini.c と同じ境界検査・同じ戻り値)。
;; 局所: [ebp-24] token, [ebp-28] offset。**どの出口も .lz4_ret で ebp から
;; 戻す** — 以前は延長読みで入力の終端に達すると token を積んだまま
;; .lz4_err へ飛び、dst_start の代わりに token を捨てて戻り番地を壊していた。
;; ============================================================
pm_lz4_decode:
        push    ebp
        mov     ebp, esp
        push    ebx
        push    ecx
        push    edx
        push    esi
        push    edi
        sub     esp, 8
        cld

        mov     esi, [ebp+8]    ;; src (ip)
        mov     ecx, [ebp+12]   ;; compressed_size
        test    ecx, ecx
        js      .lz4_err_in
        lea     ebx, [esi+ecx]  ;; ip_end
        mov     edi, [ebp+16]   ;; dst (op)
        mov     ecx, [ebp+20]   ;; cap
        test    ecx, ecx
        js      .lz4_err_in
        lea     edx, [edi+ecx]  ;; op_end

.lz4_loop:
        cmp     esi, ebx
        jae     .lz4_end

        ;; トークン
        movzx   eax, byte [esi]
        inc     esi
        mov     [ebp-24], eax

        ;; リテラル長
        shr     eax, 4
        cmp     eax, 15
        jne     .lz4_lit_copy
.lz4_lit_ext:
        cmp     esi, ebx
        jae     .lz4_err_in
        movzx   ecx, byte [esi]
        inc     esi
        add     eax, ecx
        cmp     eax, [ebp+20]   ;; 長さの累積が容量を超えたら正当な流れではない
        ja      .lz4_err_in
        cmp     ecx, 255
        je      .lz4_lit_ext

.lz4_lit_copy:
        ;; eax = lit_len。入力の残りと出力の残りの両方に収まること
        mov     ecx, ebx
        sub     ecx, esi
        cmp     eax, ecx
        ja      .lz4_err_in
        mov     ecx, edx
        sub     ecx, edi
        cmp     eax, ecx
        ja      .lz4_err_out
        mov     ecx, eax
        rep     movsb

        ;; 入力終端? (最後の系列はリテラルだけ)
        cmp     esi, ebx
        jae     .lz4_end

        ;; オフセット (2B LE)。0 と、出力済みより遠いものは不正
        mov     ecx, ebx
        sub     ecx, esi
        cmp     ecx, 2
        jb      .lz4_err_in
        movzx   eax, word [esi]
        add     esi, 2
        test    eax, eax
        jz      .lz4_err_in
        mov     ecx, edi
        sub     ecx, [ebp+16]
        cmp     eax, ecx
        ja      .lz4_err_in
        mov     [ebp-28], eax

        ;; マッチ長 = token の下位 4bit + MINMATCH
        mov     ecx, [ebp-24]
        and     ecx, 0Fh
        add     ecx, LZ4_MINMATCH
        cmp     ecx, 15 + LZ4_MINMATCH
        jne     .lz4_match_copy
.lz4_match_ext:
        cmp     esi, ebx
        jae     .lz4_err_in
        movzx   eax, byte [esi]
        inc     esi
        add     ecx, eax
        cmp     ecx, [ebp+20]
        ja      .lz4_err_in
        cmp     eax, 255
        je      .lz4_match_ext

.lz4_match_copy:
        ;; ecx = match_len。出力の残りに収まること
        mov     eax, edx
        sub     eax, edi
        cmp     ecx, eax
        ja      .lz4_err_out
        mov     eax, esi        ;; ip 保存
        mov     esi, edi
        sub     esi, [ebp-28]   ;; match_src = op - offset
        rep     movsb
        mov     esi, eax
        jmp     .lz4_loop

.lz4_end:
        mov     eax, edi
        sub     eax, [ebp+16]   ;; 展開バイト数
        jmp     .lz4_ret
.lz4_err_out:
        mov     eax, -2
        jmp     .lz4_ret
.lz4_err_in:
        mov     eax, -1
.lz4_ret:
        lea     esp, [ebp-20]
        pop     edi
        pop     esi
        pop     edx
        pop     ecx
        pop     ebx
        pop     ebp
        ret

;; CRC-32 nibble 表 (lib/crc32_core.inc の crc32_core_nib と同じ値)
crc32_nib:
        dd      000000000h, 01DB71064h, 03B6E20C8h, 026D930ACh
        dd      076DC4190h, 06B6B51F4h, 04DB26158h, 05005713Ch
        dd      0EDB88320h, 0F00F9344h, 0D6D6A3E8h, 0CB61B38Ch
        dd      09B64C2B0h, 086D3D2D4h, 0A00AE278h, 0BDBDF21Ch
vk32_zero4:
        dd      0
;; <<< VK32_HOST_END


;; ============================================================
;; pm_print32 — TVRAM表示 (32bit PM)
;; ============================================================

pm_print32:
        push    eax
.loop:
        mov     al, [esi]
        inc     esi
        or      al, al
        jz      .done
        mov     ah, 0
        mov     [edi], ax
        push    edi
        add     edi, 2000h
        mov     byte [edi], 0E1h
        pop     edi
        add     edi, 2
        jmp     .loop
.done:
        pop     eax
        ret

pm_halt:
        hlt
        jmp     pm_halt


bits 16

;; ============================================================
;; read_sect16 — 16ビットモード1セクタ読み込み
;; ============================================================
;; **SPT で実際に割る** (1.44MB の spt=18 は 2 の冪でない)。
;;   sector = LBA % SPT + 1 ; track = LBA / SPT ; head = track&1 ; cyl = track>>1
read_sect16:
        push    ax
        xor     dx, dx
        mov     cx, SPT
        div     cx              ;; AX = track, DX = LBA % SPT
        inc     dl              ;; DL = sector (1-based)
        mov     dh, al
        and     dh, 1           ;; DH = head
        shr     ax, 1           ;; AX = cylinder
        mov     cl, al
        mov     ah, 76h
        mov     al, DA_UA
        mov     bx, SECT_SZ
        mov     ch, SECT_N
        int     1Bh
        pop     ax
        jc      disk_err16
        ret

disk_err16:
        push    cs
        pop     ds
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320
        mov     si, msg_diskerr
        call    print16
.halt:
        hlt
        jmp     .halt

;; >>> FAT_HOST_BEGIN
;; ============================================================
;; FAT12 チェーンの検査と次クラスタ。**32 ビットのレジスタとアドレッシング
;; だけ**で書く — ローダは実モード (bits 16、386 の 66h/67h 前置で同じ意味)、
;; tools/tests/test_vk32_crc.py はここを切り出して bits 32 で組み、同じ
;; 手続きをホストで回す (16 ビットの実モードはホストで動かせないため)。
;; 実モードでは ESI + EBX < 64KB (FAT_BUF 6000h + 最大 4.3KB) なので 67h の
;; 32 ビット番地でも #GP にならない。
;; ============================================================
FATCHK_OK       EQU 0
FATCHK_SIZE     EQU 1           ;; 長さが 0 か MAX_IMAGE_SIZE 超
FATCHK_SHORT    EQU 2           ;; 必要な数より前に EOC (早期終端)
FATCHK_RANGE    EQU 3           ;; [2, MAX_CLUSTER) の外 (0/1、予約・不良 0FF0h〜0FF7h)
FATCHK_LONG     EQU 4           ;; 必要な数を辿った次が EOC でない (循環・長すぎる)
FATCHK_START    EQU 5           ;; 開始クラスタが [2, MAX_CLUSTER) の外 (EOC 0FF8h〜 を含む)

;; fat12_next32 — EAX = クラスタ、ESI = FAT の先頭 → EAX = 次のクラスタ
;;   壊す: EBX EDX
fat12_next32:
        mov     ebx, eax
        shr     ebx, 1
        add     ebx, eax                ;; cluster * 1.5
        movzx   edx, word [esi + ebx]
        test    al, 1
        jz      .even
        shr     edx, 4
.even:
        and     edx, 0FFFh
        mov     eax, edx
        ret

;; fat_chain_check — EAX = 開始クラスタ、EDX = ファイル長、ESI = FAT の先頭
;;   → EAX = FATCHK_*、ECX = 読むクラスタ数 (= ceil(長さ / SECT_SZ))
;;   メモリは読むだけ。壊す: なし (EBX EDX ESI は保存)
fat_chain_check:
        push    ebx
        push    edx
        push    ebp
        mov     ebp, FATCHK_SIZE
        test    edx, edx
        jz      .out
        cmp     edx, MAX_IMAGE_SIZE
        ja      .out
        lea     ecx, [edx + SECT_SZ - 1]
        shr     ecx, SECT_SHIFT
        ;; 開始クラスタはディレクトリの値。EOC や 0/1 はチェーンの途中の
        ;; 「早期終端・範囲外」とは別の壊れ方なので別の文言にする
        mov     ebp, FATCHK_START
        cmp     eax, 2
        jb      .out
        cmp     eax, MAX_CLUSTER
        jae     .out
.walk:
        mov     ebp, FATCHK_SHORT
        cmp     eax, 0FF8h
        jae     .out
        mov     ebp, FATCHK_RANGE
        cmp     eax, 2
        jb      .out
        cmp     eax, MAX_CLUSTER
        jae     .out
        call    fat12_next32
        dec     ecx
        jnz     .walk
        mov     ebp, FATCHK_LONG
        cmp     eax, 0FF8h
        jb      .out
        mov     ebp, FATCHK_OK
.out:
        pop     eax                     ;; = ebp の旧値 (捨てずに下で戻す)
        xchg    eax, ebp                ;; EAX = 結果、EBP = 旧値
        pop     edx
        lea     ecx, [edx + SECT_SZ - 1]
        shr     ecx, SECT_SHIFT
        pop     ebx
        ret
;; <<< FAT_HOST_END

;; ============================================================
;; print16 — 16ビットTVRAM表示
;; ============================================================
print16:
        lodsb
        or      al, al
        jz      .done
        mov     ah, 0
        mov     es:[di], ax
        push    di
        add     di, 2000h
        mov     byte es:[di], 0E1h
        pop     di
        add     di, 2
        jmp     print16
.done:
        ret

;; ============================================================
;; ブート情報域を書く手続き (bi_clear / bi_sense / bi_seal)。
;; HDD ローダと同じ 1 つのファイル。
;; ============================================================
%include "boot/bootinfo_rm.inc"

;; ============================================================
;; GDT
;; ============================================================
gdt:
        dq      0
        dw      0FFFFh
        dw      0
        db      0
        db      09Ah
        db      0CFh
        db      0
        dw      0FFFFh
        dw      0
        db      0
        db      092h
        db      0CFh
        db      0
gdt_end:

gdtr:
        dw      gdt_end - gdt - 1
gdtr_base:
        dd      0

;; ============================================================
;; データ
;; ============================================================
kern_name:      db 'VMKRNL  LZ4'
msg_loader:     db 'Loading vmkernel.lz4 from FAT12...', 0
msg_nokernel:   db 'VMKRNL.LZ4 not found!', 0
msg_pm:         db 'Entering PM...', 0
msg_diskerr:    db 'Disk Error!', 0
msg_ok32:       db 'Kernel loaded. Booting...', 0
msg_badsize:    db 'VMKRNL.LZ4: bad size (0 or > 508KiB)', 0
msg_fatshort:   db 'VMKRNL.LZ4: FAT chain ends early', 0
msg_fatstart:   db 'VMKRNL.LZ4: bad start cluster', 0
msg_fatrange:   db 'VMKRNL.LZ4: FAT cluster out of range', 0
msg_fatlong:    db 'VMKRNL.LZ4: FAT chain too long/loop', 0
;; VK32_ERR_* の文言 (boot/vk32_boot.c の vk32_strerror と同じ)。添字 = -rc
vk32_msgs:
        dd      m_vk_err, m_vk_1, m_vk_2, m_vk_3, m_vk_4, m_vk_5, m_vk_6
        dd      m_vk_7, m_vk_8, m_vk_9, m_vk_10, m_vk_11, m_vk_12
m_vk_err:       db 'VK32: error', 0
m_vk_1:         db 'VK32: bad file size', 0
m_vk_2:         db 'VK32: bad magic', 0
m_vk_3:         db 'VK32: unknown version (need 2)', 0
m_vk_4:         db 'VK32: bad entry count', 0
m_vk_5:         db 'VK32: bad header size', 0
m_vk_6:         db 'VK32: file truncated/length mismatch', 0
m_vk_7:         db 'VK32: image CRC mismatch', 0
m_vk_8:         db 'VK32: entry data outside file', 0
m_vk_9:         db 'VK32: load address out of range', 0
m_vk_10:        db 'VK32: LZ4 decode FAIL', 0
m_vk_11:        db 'VK32: decoded size mismatch', 0
m_vk_12:        db 'VK32: entry CRC mismatch', 0

var_cluster:    dw 0
var_size:       dd 0            ;; VMKRNL.LZ4 の長さ (u32。2 つの dw の隣接に頼らない)
var_load_seg:   dw 0
var_left:       dw 0            ;; まだ読むクラスタ数
vk32_img_crc:   dd 0
