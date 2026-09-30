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
geo_da:     db  80h             ;; DA/UA

boot_main:
        ;; CS:IP 正規化 (BIOS が CS=0/IP=1FC00h で呼ぶ場合に対応)
        db      0EAh
        dw      boot_init
        dw      1FC0h

boot_init:
        cli
        mov     ax, 1FC0h
        mov     ds, ax
        xor     ax, ax
        mov     ss, ax
        mov     sp, 7C00h
        sti

        ;; 起動メッセージ表示
        mov     ax, 0A000h
        mov     es, ax
        xor     di, di
        mov     si, msg_boot
        call    print_msg

        ;; ローダーロード先: 0000:8000
        xor     ax, ax
        mov     es, ax
        mov     di, 8000h           ;; ES:BP 用バッファポインタ

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
        mov     bx, 512             ;; 転送バイト数

        mov     ah, 06h
        mov     al, [geo_da]
        int     1Bh
        jc      boot_error

        pop     cx
        pop     ax
        inc     ax
        add     di, 512
        dec     cx
        jnz     load_loop

        ;; ジオメトリ情報をレジスタに設定してローダーへジャンプ
        ;; AL = DA/UA, AH = heads, BL = SPT
        mov     al, [geo_da]
        mov     ah, [geo_heads]
        mov     bl, [geo_spt]
        db      0EAh
        dw      8000h, 0000h

boot_error:
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 160
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
        add     di, 2000h
        mov     byte es:[di], 0E1h
        pop     di
        add     di, 2
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
