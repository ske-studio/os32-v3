;; ============================================================
;; v86_entry.asm — 仮想8086モードへの遷移
;;
;; プロテクトモードから V86 に入る唯一の方法は、EFLAGS.VM を立てた
;; スタックフレームを積んで IRETD することである。CPU はこのとき
;; CS/SS/DS/ES/FS/GS をリアルモード形式 (base = seg << 4) として扱い、
;; CPL を 3 に落とす。
;;
;; 戻りは #GP 経由で isr_stub_13 の V86 パスに入る。TSS.ESP0 が指す
;; カーネルスタックに CPU がフレームを積むので、呼び出し前に ESP0 が
;; 正しい値であることが必須。ここで取り違えるとカーネルスタックを
;; 踏み荒らして原因不明のクラッシュになる — 前回の実装で「最も疑わしいが
;; 未調査」のまま終わった箇所なので、本実装では ESP0 の設定を
;; この関数の中に閉じ込めてある。
;; ============================================================

cpu 386

extern kernel_tss                       ;; kernel/tss.c

section .text

;; ------------------------------------------------------------
;; void v86_enter(const struct v86_context *ctx);
;;
;; struct v86_context のオフセット (kernel/v86.h と 1:1):
;;   0:eip  4:cs  8:eflags  12:esp  16:ss  20:es  24:ds  28:fs  32:gs
;; ------------------------------------------------------------
global v86_enter
v86_enter:
        cli

        ;; TSS.ESP0 を「今の ESP」に設定する。
        ;;
        ;; V86 から #GP が入ると CPU は SS0:ESP0 からフレームを積む。
        ;; ここを現在位置にしておけば、フレームは呼び出し元のスタック
        ;; フレームの真下に積まれる。v86_enter は正常復帰しない
        ;; (終了は longjmp) ので、この領域が上書きされても困らない。
        ;; struct tss_entry の esp0 はオフセット 4。
        mov     eax, esp
        mov     [kernel_tss + 4], eax

        mov     esi, [esp + 4]          ;; ctx

        ;; iretd 用フレームを積む (高位→低位の順に push)
        push    dword [esi + 32]        ;; GS
        push    dword [esi + 28]        ;; FS
        push    dword [esi + 24]        ;; DS
        push    dword [esi + 20]        ;; ES
        push    dword [esi + 16]        ;; SS
        push    dword [esi + 12]        ;; ESP
        push    dword [esi + 8]         ;; EFLAGS (VM=1, IOPL=3)
        push    dword [esi + 4]         ;; CS
        push    dword [esi + 0]         ;; EIP

        iretd                           ;; → V86 モードへ

;; ------------------------------------------------------------
;; void v86_exit_to_kernel(void);
;;
;; #GP ハンドラがセッション終了を決めたときの脱出口。
;; 戻り先は exec_setjmp で保存済みなので、ここからは戻らない。
;; ------------------------------------------------------------
extern v86_longjmp_out

global v86_exit_to_kernel
v86_exit_to_kernel:
        call    v86_longjmp_out
        ;; 戻ってこない。念のため停止させる。
.hang:
        cli
        hlt
        jmp     .hang
