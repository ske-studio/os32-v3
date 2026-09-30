;; tools/tests/test_vk32_crc.py の変異 asm_old_lz4 が使う旧版の pm_lz4_decode。
;; os32 (v2.x) の 3e22825:boot/loader_fat_new.asm から切り出した写し。os32-v3 の
;; 履歴には 3e22825 が無い (fork で履歴を切った) ので git show に頼らず手元に置く。
;; 本文 (この 3 行の後) は変えない。ビルドには使わない。
pm_lz4_decode:
        push    ebp
        mov     ebp, esp
        push    ebx
        push    ecx
        push    edx
        push    esi
        push    edi

        mov     esi, [ebp+8]    ;; src (ip)
        mov     ecx, [ebp+12]   ;; compressed_size
        lea     ebx, [esi+ecx]  ;; ip_end
        mov     edi, [ebp+16]   ;; dst (op)
        mov     ecx, [ebp+20]   ;; cap
        lea     edx, [edi+ecx]  ;; op_end
        push    edi             ;; 保存: dst_start

.lz4_loop:
        cmp     esi, ebx
        jge     .lz4_end

        ;; トークン
        movzx   eax, byte [esi]
        inc     esi
        push    eax             ;; 保存: token

        ;; リテラル長
        shr     eax, 4
        and     eax, 0Fh
        cmp     eax, 15
        jne     .lz4_lit_copy
.lz4_lit_ext:
        cmp     esi, ebx
        jge     .lz4_err
        movzx   ecx, byte [esi]
        inc     esi
        add     eax, ecx
        cmp     ecx, 255
        je      .lz4_lit_ext

.lz4_lit_copy:
        ;; eax = lit_len
        mov     ecx, eax
        ;; 境界チェック省略 (ブートローダーなので信頼できるデータ)
        rep     movsb

        ;; 入力終端?
        cmp     esi, ebx
        jge     .lz4_end_pop

        ;; オフセット (2B LE)
        movzx   eax, word [esi]
        add     esi, 2
        test    eax, eax
        jz      .lz4_err_pop
        push    eax             ;; 保存: offset

        ;; マッチ長
        pop     eax             ;; offset 復元 → 後で使う
        push    eax             ;; 再保存

        ;; token の下位4bit
        mov     ecx, [esp+4]    ;; token (スタック上)
        and     ecx, 0Fh
        add     ecx, LZ4_MINMATCH
        cmp     ecx, 15 + LZ4_MINMATCH
        jne     .lz4_match_copy
.lz4_match_ext:
        cmp     esi, ebx
        jge     .lz4_err_pop2
        movzx   eax, byte [esi]
        inc     esi
        add     ecx, eax
        cmp     eax, 255
        je      .lz4_match_ext

.lz4_match_copy:
        ;; ecx = match_len, [esp] = offset
        pop     eax             ;; offset
        push    esi             ;; src 保存
        mov     esi, edi
        sub     esi, eax        ;; match_src = op - offset
        rep     movsb
        pop     esi             ;; src 復元

        ;; token 除去
        add     esp, 4
        jmp     .lz4_loop

.lz4_end_pop:
        add     esp, 4          ;; token 除去
.lz4_end:
        pop     eax             ;; dst_start
        sub     edi, eax        ;; 展開バイト数
        mov     eax, edi

        pop     edi
        pop     esi
        pop     edx
        pop     ecx
        pop     ebx
        pop     ebp
        ret

.lz4_err_pop2:
        add     esp, 4          ;; offset
.lz4_err_pop:
        add     esp, 4          ;; token
.lz4_err:
        pop     eax             ;; dst_start (捨て)
        mov     eax, -1

        pop     edi
        pop     esi
        pop     edx
        pop     ecx
        pop     ebx
        pop     ebp
        ret


