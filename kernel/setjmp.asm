;; ======================================================================
;;  SETJMP.ASM — 簡易 setjmp/longjmp (OS32カーネル用)
;;
;;  exec_setjmp(buf):  ESP,EBP,EBX,ESI,EDI,EIPを保存。戻り値=0
;;  exec_longjmp(buf): 保存したレジスタを復元。setjmpの呼び出し元に
;;                     戻り値=1で復帰する。
;;
;;  バッファレイアウト (u32 buf[6]):
;;    [0] = ESP  [1] = EBP  [2] = EBX
;;    [3] = ESI  [4] = EDI  [5] = EIP (return address)
;; ======================================================================

cpu 386

section .text

;; ============================================================
;; exec_setjmp — レジスタ保存 (戻り値=0)
;;
;; 引数: [ESP+4] = buf (スタック経由)
;; ============================================================
global exec_setjmp
exec_setjmp:
        mov     eax, [esp+4]    ;; buf ポインタをスタックから取得
        mov     [eax+0],  esp
        mov     [eax+4],  ebp
        mov     [eax+8],  ebx
        mov     [eax+12], esi
        mov     [eax+16], edi
        ;; return address は [ESP] にある
        mov     ecx, [esp]
        mov     [eax+20], ecx
        ;; return 0
        xor     eax, eax
        ret

;; ============================================================
;; exec_longjmp — レジスタ復元 (setjmpの戻り値=1)
;;
;; 引数: [ESP+4] = buf (スタック経由)
;; ============================================================
global exec_longjmp
exec_longjmp:
        mov     eax, [esp+4]    ;; buf ポインタをスタックから取得
        mov     esi, [eax+12]
        mov     edi, [eax+16]
        mov     ebx, [eax+8]
        mov     ebp, [eax+4]
        mov     esp, [eax+0]    ;; ESP復元: 以降のスタック参照はsetjmp時のスタック
        ;; return address を復元
        mov     ecx, [eax+20]
        mov     [esp], ecx      ;; スタック上のreturn addressを書き換え
        ;; return 1 (setjmpの戻り値として)
        mov     eax, 1
        ret
