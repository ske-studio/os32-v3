;; ============================================================
;; ne2000_io.asm — NE2000 Remote DMA データポートの 16bit PIO
;;
;; System V i386 ABI (cdecl)。callee-saved の EBP/ESI/EDI を保存し、
;; string 命令の前に cld で DF=0 を保証する。386 命令のみ。
;; ポート番号は引数で受け取るのでレジスタ定数の複製は持たない。
;;
;; 奇数末尾の扱い (docs/tasks/network/PLAN.md §4):
;;   read : 最後の word を AX に読み、下位 1 バイトだけ格納する
;;          (呼び出し元バッファの外に 1 バイトも書かない)
;;   write: 最後の 1 バイトを上位ゼロの word として送る
;;          (呼び出し元バッファを 1 バイトも読み越さない)
;; ============================================================

cpu 386

section .text

;; void ne2k_pio_read(unsigned int port, void *buf, unsigned int nbytes)
global ne2k_pio_read
ne2k_pio_read:
        push    ebp
        mov     ebp, esp
        push    edi
        cld
        mov     edx, [ebp + 8]          ;; port
        mov     edi, [ebp + 12]         ;; buf
        mov     ecx, [ebp + 16]         ;; nbytes
        shr     ecx, 1                  ;; word 数
        rep     insw
        test    dword [ebp + 16], 1
        jz      .done
        in      ax, dx                  ;; 奇数末尾: word を読んで
        mov     [edi], al               ;; 下位 1 バイトだけ格納
.done:
        pop     edi
        pop     ebp
        ret

;; void ne2k_pio_write(unsigned int port, const void *buf, unsigned int nbytes)
global ne2k_pio_write
ne2k_pio_write:
        push    ebp
        mov     ebp, esp
        push    esi
        cld
        mov     edx, [ebp + 8]          ;; port
        mov     esi, [ebp + 12]         ;; buf
        mov     ecx, [ebp + 16]         ;; nbytes
        shr     ecx, 1
        rep     outsw
        test    dword [ebp + 16], 1
        jz      .done
        mov     al, [esi]               ;; 奇数末尾: 1 バイトだけ読み
        xor     ah, ah                  ;; 上位ゼロの word で送る
        out     dx, ax
.done:
        pop     esi
        pop     ebp
        ret
