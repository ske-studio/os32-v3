;; ============================================================
;; bootinfo_rm.inc — ブート情報域を書く 16 ビット実モードの手続き
;;
;; loader_fat_new.asm (FD、2HD / 1.44MB) と loader_hdd.asm (HDD) の
;; **両方がこの 1 つを %include する** (手続きを 2 か所に書かない)。
;; 呼ぶ側は `bits 16`、DS = 0、IF = 1 (INT 1Bh の HDD は割り込みを待つ)。
;; 形式と書く順序は include/bootinfo.h の冒頭。
;;
;;   bi_clear  AL = 取得元。域全体を 0 (= magic 0 = 無効) にし、
;;             version / source / ndrives を書く。**最初に必ず呼ぶ** (N2)。
;;   bi_sense  AL = DA/UA、SI = ドライブ記録の番地。INT 1Bh AH=84h を呼び
;;             CF / AH / BX / CX / DH / DL と valid を書く。
;;             壊す: AX BX CX DX (SI は保存)。
;;   bi_seal   sum → magic → check の順に書いて有効化する (check が最後)。
;; ============================================================

bi_clear:
        push    es
        push    di
        push    cx
        push    ax
        xor     di, di
        mov     es, di
        mov     di, MEM_BOOTINFO_BASE
        mov     cx, BOOTINFO_WIRE_SIZE / 2
        xor     ax, ax
        cld
        rep     stosw
        pop     ax
        mov     byte [MEM_BOOTINFO_BASE + BI_OFF_SOURCE], al
        mov     word [MEM_BOOTINFO_BASE + BI_OFF_VERSION], BOOTINFO_VERSION
        mov     byte [MEM_BOOTINFO_BASE + BI_OFF_NDRIVES], BOOTINFO_NDRIVES
        pop     cx
        pop     di
        pop     es
        ret

bi_sense:
        mov     [si + BI_DRV_DA], al
        mov     byte [si + BI_DRV_QUERIED], 1
        mov     ah, 84h
        xor     bx, bx          ;; 失敗時に前の値が残って見えないよう 0 から
        xor     cx, cx
        xor     dx, dx
        push    si
        push    bp
        push    es
        push    ds
        int     1Bh
        pop     ds              ;; pop はフラグを変えない (CF をこの後で読む)
        pop     es
        pop     bp
        pop     si
        setc    byte [si + BI_DRV_CF]
        mov     [si + BI_DRV_AH], ah
        mov     [si + BI_DRV_BX], bx
        mov     [si + BI_DRV_CX], cx
        mov     [si + BI_DRV_DH], dh
        mov     [si + BI_DRV_DL], dl
        ;; valid = CF 0 かつ BX = 512 かつ CX / DH / DL が 0 でない
        ;; (カーネルの bootinfo_drive_usable と同じ規則)
        cmp     byte [si + BI_DRV_CF], 0
        jne     .inval
        cmp     bx, BOOTINFO_SECLEN
        jne     .inval
        test    cx, cx
        jz      .inval
        test    dh, dh
        jz      .inval
        test    dl, dl
        jz      .inval
        mov     byte [si + BI_DRV_VALID], 1
.inval:
        ret

bi_seal:
        push    si
        push    cx
        push    dx
        push    ax
        cld
        mov     si, MEM_BOOTINFO_BASE + BI_OFF_DRIVE0
        mov     cx, BI_DRIVE_SIZE * BOOTINFO_NDRIVES
        xor     dx, dx
        xor     ax, ax
.sum:
        lodsb                   ;; AL だけ書く — AH は 0 のまま
        add     dx, ax
        loop    .sum
        mov     [MEM_BOOTINFO_BASE + BI_OFF_SUM], dx
        mov     dword [MEM_BOOTINFO_BASE + BI_OFF_MAGIC], BOOTINFO_MAGIC
        ;; **check は最後に書く** — ここまでで止まった域は無効に見える
        mov     dword [MEM_BOOTINFO_BASE + BI_OFF_CHECK], BOOTINFO_CHECK
        pop     ax
        pop     dx
        pop     cx
        pop     si
        ret
