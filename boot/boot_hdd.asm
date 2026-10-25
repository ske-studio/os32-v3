%include "boot/memmap_defs.inc"
%include "boot/pc98_defs.inc"
%include "boot/realmode_defs.inc"
%include "boot/bootinfo.inc"

;; ========================================================================
;;  boot_hdd.asm — OS32 HDD IPL (512B, BIOS INT 1Bh)
;;
;;  PC-9800 IDE HDD セクタ0 に格納。
;;  BIOS により 1FC0:0000 にロードされ実行開始。
;;
;;  処理:
;;    1. BIOS INT 1Bh でローダー (LBA 2 から16セクタ=8KB) を 0000:8000 にロード
;;    2. ジオメトリ情報をレジスタに設定
;;    3. far jmp 0000:8000 でローダーに制御移譲
;;
;;  ジオメトリパッチ領域 (offset 8-10):
;;    インストーラがディスクジオメトリを書き込む
;;
;;  ローダーへのレジスタ渡し:
;;    AL = DA/UA, AH = ヘッド数, BL = セクタ/トラック
;;
;;  INT 1Bh レジスタ仕様 (PC-9800 Bible §2-9-4):
;;    AH = 06h (READ), AL = DA/UA
;;    BX = 転送バイト数, CX = シリンダ番号 (16bit)
;;    DH = ヘッド, DL = セクタ (0ベース), ES:BP = バッファ
;; ========================================================================

        cpu 286

section .text
        org 0000h

boot_entry:
        jmp     SHORT boot_main
        nop
        nop
        db      'IPL1'

        ;; --- ジオメトリパッチ領域 (offset 8-10) ---
geo_heads:  db  8               ;; ヘッド数
geo_spt:    db  17              ;; セクタ/トラック
geo_da:     db  BOOT_DRIVE_HDD  ;; DA/UA

boot_main:
        ;; CS:IP 正規化 (BIOS が CS=0/IP=1FC00h で呼ぶ場合に対応)
        db      0EAh
        dw      boot_init
        dw      (MEM_HDD_IPL_BASE >> RM_SEGMENT_SHIFT)

boot_init:
        cli
        mov     ax, (MEM_HDD_IPL_BASE >> RM_SEGMENT_SHIFT)
        mov     ds, ax
        xor     ax, ax
        mov     ss, ax
        mov     sp, MEM_BOOT_RM_STACK_TOP
        sti

        ;; 起動メッセージ表示
        mov     ax, (TVRAM_CHAR_BASE >> RM_SEGMENT_SHIFT)
        mov     es, ax
        xor     di, di
        mov     si, msg_boot
        call    print_msg

        ;; ローダーロード先: 0000:8000
        xor     ax, ax
        mov     es, ax
        mov     di, MEM_LOADER_START  ;; ES:BP 用バッファポインタ

        ;; LBA 2 から 16セクタ (8KB) をロード
        mov     ax, 2               ;; 開始LBA
        mov     cx, 16              ;; セクタ数

load_loop:
        push    ax
        push    cx

        ;; LBA (AX) → CHS 変換
        xor     dx, dx
        xor     cx, cx
        mov     cl, [geo_spt]
        div     cx                  ;; AX = LBA/SPT, DX = sector (0ベース)
        push    dx

        xor     dx, dx
        xor     cx, cx
        mov     cl, [geo_heads]
        div     cx                  ;; AX = cylinder, DX = head

        mov     cx, ax              ;; CX = シリンダ番号
        mov     dh, dl              ;; DH = ヘッド
        pop     ax
        mov     dl, al              ;; DL = セクタ (0ベース)

        mov     bp, di              ;; ES:BP = バッファ
        mov     bx, BOOTINFO_SECLEN  ;; 転送バイト数

        mov     ah, BIOS_DISK_READ
        mov     al, [geo_da]
        int     BIOS_INT_DISK
        jc      boot_error

        pop     cx
        pop     ax
        inc     ax
        add     di, BOOTINFO_SECLEN
        dec     cx
        jnz     load_loop

        ;; ジオメトリ情報をレジスタに設定してローダーへジャンプ
        ;; AL = DA/UA, AH = heads, BL = SPT
        mov     al, [geo_da]
        mov     ah, [geo_heads]
        mov     bl, [geo_spt]
        db      0EAh
        dw      MEM_LOADER_START, 0000h

boot_error:
        mov     ax, (TVRAM_CHAR_BASE >> RM_SEGMENT_SHIFT)
        mov     es, ax
        mov     di, TVRAM_BPR
        mov     si, msg_err
        call    print_msg
boot_halt:
        hlt
        jmp     boot_halt

;; --- TVRAM 文字列表示 ---
print_msg:
        push    ax
        push    di
pr_loop:
        lodsb
        or      al, al
        jz      pr_done
        mov     ah, 0
        mov     es:[di], ax
        push    di
        add     di, (TVRAM_ATTR_BASE - TVRAM_CHAR_BASE)
        mov     byte es:[di], TATTR_WHITE
        pop     di
        add     di, TVRAM_CELL_BYTES
        jmp     pr_loop
pr_done:
        pop     di
        pop     ax
        ret

msg_boot    db  'OS32 HDD Boot', 0
msg_err     db  'Disk Error', 0

        ;; 256バイト目のシグネチャ (PC-98 BIOS対策)
        times   254 - ($ - $$) db 0
        db      055h, 0AAh

        ;; 512バイト目のシグネチャ
        times   510 - ($ - $$) db 0
        db      055h, 0AAh
