;; ======================================================================
;;  SETJMP.ASM — 簡易 setjmp/longjmp (OS32カーネル用)
;;
;;  exec_setjmp(buf):  ESP,EBP,EBX,ESI,EDI,EIP と文脈の深さを保存。戻り値=0
;;  exec_longjmp(buf): 保存したレジスタと深さを復元。setjmpの呼び出し元に
;;                     戻り値=1で復帰する。
;;
;;  バッファレイアウト (u32 buf[KSETJMP_BUF_LEN] = 8 語、include/ksetjmp.h):
;;    [0] = ESP  [1] = EBP  [2] = EBX
;;    [3] = ESI  [4] = EDI  [5] = EIP (return address)
;;    [6] = kctx_irq_depth  [7] = kctx_exc_depth
;;
;;  深さ (TASK_T1_LEDGER §3-5): 割り込み / 例外のフレームから longjmp で
;;  抜ける経路 (CTRL+STOP の畳み、fault kill、V86 の脱出、park) は、対応する
;;  IRQ_LEAVE / EXC_LEAVE を通らない。setjmp の時点の深さへ戻さないと、以後の
;;  通常文脈が全部「割り込み中」に数えられる。longjmp 点を個別に直すのでは
;;  なく控えを jmpbuf に入れるので、longjmp 点が増えても漏れない (T1-R3)。
;; ======================================================================

cpu 386

extern kctx_irq_depth
extern kctx_exc_depth

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
        ;; 文脈の深さの控え (語 6・7)
        mov     ecx, [kctx_irq_depth]
        mov     [eax+24], ecx
        mov     ecx, [kctx_exc_depth]
        mov     [eax+28], ecx
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
        ;; 文脈の深さを setjmp の時点へ戻す (語 6・7)
        mov     ecx, [eax+24]
        mov     [kctx_irq_depth], ecx
        mov     ecx, [eax+28]
        mov     [kctx_exc_depth], ecx
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
