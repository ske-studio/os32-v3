;; V86 ゲスト: `v86 -g` (票 TASK_PEGC480_REALHW §3 段 1)
;;
;; 正本はこのファイル。kernel/v86_gcap.c の v86_gcap_code[] はこれを
;;   nasm -f bin kernel/v86_test16_gcap.asm -o /tmp/g.bin
;; で組んだバイト列そのもの (tools/tests/test_v86_gcap.py が組み直して照合する)。
;;
;; 配置: CS = 0x8A00 (linear 0x8A000)。入口は 0x40 刻みの固定オフセット
;; (v86_gcap.c の GCAP_ENTRY_*)。データは 0x8C00:0000 (V86_TEST_MAGIC_ADDR)。
;;   [0..7]   AX / BX / CX / DX の入力 (rom_call)
;;   [8..15]  INT 18h の後の AX / BX / CX / DX
;;   [16]     終わりの印 0xC0DE (ここまで来た = 打ち切られていない)
;;   [18]     INT 18h の後の FLAGS
;;   [0x20]   st_seq: IN AL の値 / [0x22] IN AX の値
;;   [0x24]   st_hang: INT FFh の先で見た FLAGS (IF が立っているか)

cpu 386
bits 16
org 0

DATA_SEG    equ 0x8C00
DONE        equ 0xC0DE

;; ---------------------------------------------------------------- 0x000
;; rom_call — 入力のレジスタで INT 18h を 1 回呼び、結果を残して HLT。
;; INT 18h は HLE しない (v86_bios.c の hle_vectors は 1Bh だけ) ので、
;; #GP → ゲストの IVT (起動時に保存した実機の値 = ROM) へ流れる。
rom_call:
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     ax, [0]
        mov     bx, [2]
        mov     cx, [4]
        mov     dx, [6]
        push    ds
        int     0x18
        pop     ds
        mov     [8], ax
        mov     [10], bx
        mov     [12], cx
        mov     [14], dx
        pushf
        pop     ax
        mov     [18], ax
        mov     word [16], DONE
        hlt

        times 0x40-($-$$) db 0xF4

;; ---------------------------------------------------------------- 0x040
;; st_seq — 決まった I/O 列 (自己試験。捕まえたポートは実機へ通さない)。
;; 期待する OUT 列 (v86_gcap.c の gcap_expect[]):
;;   62h  8bit 11h   (E6 imm)
;;   9A8h 8bit 01h   (EE DX)
;;   A2h  16bit 2233h (E7 imm)
;;   60h  16bit 4455h (EF DX)
;;   00h  8bit 0Bh   (仮想 PIC へ = 通さない)
;; IN は A0h (8bit) と 6Ah (16bit) を 1 回ずつ。5Fh は素通し (記録されない)。
st_seq:
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     al, 0x11
        out     0x62, al
        mov     dx, 0x09A8
        mov     al, 0x01
        out     dx, al
        mov     ax, 0x2233
        out     0xA2, ax
        mov     dx, 0x0060
        mov     ax, 0x4455
        out     dx, ax
        mov     al, 0x0B
        out     0x00, al
        in      al, 0xA0
        mov     [0x20], al
        mov     dx, 0x006A
        in      ax, dx
        mov     [0x22], ax
        out     0x5F, al
        mov     word [16], DONE
        hlt

        times 0x80-($-$$) db 0xF4

;; ---------------------------------------------------------------- 0x080
;; st_ovf — 68h へ 513 回 (V86G_OUT_MAX + 1)。溢れても打ち切らずに最後まで走る。
st_ovf:
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     cx, 513
.l:     mov     al, cl
        out     0x68, al
        loop    .l
        mov     word [16], DONE
        hlt

        times 0xC0-($-$$) db 0xF4

;; ---------------------------------------------------------------- 0x0C0
;; st_outs — OUTSB (6Eh) で打ち切られる。印まで来たら不合格。
st_outs:
        mov     ax, DATA_SEG
        mov     ds, ax
        xor     si, si
        mov     dx, 0x0062
        outsb
        mov     word [16], DONE
        hlt

        times 0x100-($-$$) db 0xF4

;; ---------------------------------------------------------------- 0x100
;; st_io32 — OUT DX, EAX (66h EFh) で打ち切られる。印まで来たら不合格。
st_io32:
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     dx, 0x00A0
        xor     eax, eax
        out     dx, eax
        mov     word [16], DONE
        hlt

        times 0x140-($-$$) db 0xF4

;; ---------------------------------------------------------------- 0x140
;; st_hang — ROM の呼び出しと同じ形 (IF を立てて始め、INT で入る) で、
;; I/O も #GP も出さずに回り続ける。v86_gcap_keep_if() が効いていれば
;; タイマ IRQ が来て時間の見切り (V86_EXIT_TIMEOUT) で戻る。効いていなければ
;; 実 IF が落ちたまま回り、戻らない (= カーネルごと止まる。自己試験の中では
;; 起きてはいけない)。IVT[FFh] は実機のページ 0 だが、v86_mem_teardown が
;; 退避した IVT/BDA を書き戻す。
st_hang:
        mov     ax, DATA_SEG
        mov     ds, ax
        xor     ax, ax
        mov     es, ax
        mov     word [es:0x3FC], hang_isr
        mov     word [es:0x3FE], 0x8A00
        int     0xFF
        mov     word [16], DONE
        hlt
hang_isr:
        pushf
        pop     ax
        mov     [0x24], ax
.spin:  jmp     .spin

        times 0x180-($-$$) db 0xF4
