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

%include "arch/x86/kernel/entry_layout.inc"

cpu 386

extern kernel_tss                       ;; arch/x86/kernel/tss.c

section .text

;; ------------------------------------------------------------
;; void v86_enter(const struct v86_context *ctx);
;;
;; struct v86_context のオフセット (kernel/v86/v86.h と 1:1):
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
        mov     [kernel_tss + TSS_OFF_ESP0], eax

        mov     esi, [esp + 4]          ;; ctx

        ;; iretd 用フレームを積む (高位→低位の順に push)
        push    dword [esi + V86_CTX_GS]  ;; GS
        push    dword [esi + V86_CTX_FS]  ;; FS
        push    dword [esi + V86_CTX_DS]  ;; DS
        push    dword [esi + V86_CTX_ES]  ;; ES
        push    dword [esi + V86_CTX_SS]  ;; SS
        push    dword [esi + V86_CTX_ESP]  ;; ESP
        push    dword [esi + V86_CTX_EFLAGS]  ;; EFLAGS (VM=1, IOPL=3)
        push    dword [esi + V86_CTX_CS]  ;; CS
        push    dword [esi + V86_CTX_EIP]  ;; EIP

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
