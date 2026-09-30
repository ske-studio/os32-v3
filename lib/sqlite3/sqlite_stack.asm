;; ============================================================
;; sqlite_stack.asm — SQLite用スタック切り替えトランポリン
;;
;; カーネルスタック(64KB)では不足するSQLite B-tree操作のため、
;; 拡張メモリ上の代替スタックに切り替えて関数を呼び出す。
;; ============================================================

cpu 386
bits 32

section .text

;; int sqlite_call_on_alt_stack(int (*func)(void), u32 stack_top)
;; cdecl呼び出し規約:
;;   [esp+4]  = func    — 呼び出す関数ポインタ
;;   [esp+8]  = stack_top — 代替スタックの先頭アドレス (16バイトアライン推奨)
;; 戻り値: func() の戻り値 (eax)
global sqlite_call_on_alt_stack
sqlite_call_on_alt_stack:
        push    ebp
        mov     ebp, esp
        push    ebx
        push    esi

        mov     eax, [ebp+8]    ;; func ポインタ
        mov     ebx, [ebp+12]   ;; stack_top

        mov     esi, esp        ;; 現在のESPを退避
        mov     esp, ebx        ;; 代替スタックに切り替え

        push    esi             ;; 代替スタック上に旧ESPを保存

        call    eax             ;; func() を呼び出し — 戻り値はeaxに入る

        pop     esi             ;; 旧ESPを復元
        mov     esp, esi

        pop     esi
        pop     ebx
        pop     ebp
        ret
