;; ============================================================
;; IRETD_USER — CPL=3 (リング3) 対応の iretd。全復帰点で iretd の代わりに使う。
;;
;; isr_stub は入口で RESTORE_KSEG により DS/ES/FS/GS を KERNEL_DS にするが、
;; popad/iretd は汎用レジスタと CS/SS/EIP/ESP/EFLAGS しか復元しない。
;; CPL=0 中断や V86 (CPU がフレームからセグメントを復元) では無害だが、
;; **保護モード CPL=3 (v2 リング3) を中断した場合**、復帰後の DS/ES/FS/GS が
;; KERNEL_DS のまま残り、CPL=3 が DS 経由でメモリに触れた瞬間に
;; 「CPL=3 が DPL=0 セグメント使用」で #GP する
;; (V3 実測: PIT 割り込みを跨いだ後の movl が EIP=0x40003D, CS=0x23 で #GP)。
;;
;; 対策: iretd 直前 (スタックが [EIP][CS][EFLAGS](+[ESP][SS]) に正規化された
;; 時点。全スタブで esp+4=CS, esp+8=EFLAGS) に復帰先を判定し、保護モード
;; CPL=3 へ戻るときだけユーザデータセグメント USER_DS を再ロードする。
;; システムのユーザデータセレクタは USER_DS ただ 1 本 (フラット, GDT 固定)
;; なので、割り込みごとに元値を保存せずとも USER_DS を入れれば正しい。
;;   - V86 (EFLAGS.VM=1): iretd がフレームからセグメントを復元 → 触らない
;;   - CPL=0 (CS.RPL=0):  KERNEL_DS のまま (再ロードしない)
;;   - CPL=3 (CS.RPL=3):  USER_DS を DS/ES/FS/GS に再ロード
;;
;; V86 経路 (ISR_*_V86 / V86_REFLECT) は EFLAGS.VM=1 で必ず素通しになるので
;; 壊さない。CPL=0 割り込みも RPL=0 で素通し。追加コストは分岐 2 つのみ。
;; ============================================================
%macro IRETD_USER 0
        test    dword [esp + IRET_EFLAGS_OFF], EFLAGS_VM  ;; EFLAGS.VM?
        jnz     %%do_iret                      ;; V86 → iretd がフレームから復元
        test    dword [esp + IRET_CS_OFF], X86_SEL_RPL_MASK  ;; CS.RPL == 3 (CPL=3 へ戻る)?
        jz      %%do_iret                      ;; CPL=0 → KERNEL_DS のまま
        push    eax
        mov     ax, USER_DS
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        pop     eax
%%do_iret:
        iretd
%endmacro

