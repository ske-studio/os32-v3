bits 32
section .text

global kmemcpy
global memcpy
global kmemset
global memset
global kstrlen
global strlen
global kstrcmp
global strcmp
global kstrncmp
global strncmp
global kstrcpy
global kstrncpy
global memcmp

; ========================================================================
; void *kmemcpy(void *dst, const void *src, u32 n)
; void *memcpy(void *dst, const void *src, u32 n)
; ========================================================================
kmemcpy:
memcpy:
    push ebp
    mov ebp, esp
    push edi
    push esi
    cld
    
    mov edi, [ebp+8]    ; dst
    mov esi, [ebp+12]   ; src
    mov ecx, [ebp+16]   ; n
    
    mov eax, edi        ; 戻り値 eax = dst

    cmp ecx, 4
    jb .do_bytes        ; n は u32 なので符号なし比較 (jl だと 2GB 超で誤判定)

    ; dst アライメント (4バイト境界になるまで1バイトずつ)
    test edi, 3
    jz .aligned
.align_loop:
    movsb
    dec ecx
    test edi, 3
    jnz .align_loop

.aligned:
    ; DWORD単位の転送
    mov edx, ecx
    shr ecx, 2
    jz .do_bytes_recover
    rep movsd

.do_bytes_recover:
    mov ecx, edx
    and ecx, 3
    jz .done

.do_bytes:
    rep movsb

.done:
    pop esi
    pop edi
    pop ebp
    ret

; ========================================================================
; void *kmemset(void *dst, int val, u32 n)
; void *memset(void *dst, int val, u32 n)
; ========================================================================
kmemset:
memset:
    push ebp
    mov ebp, esp
    push edi
    cld
    
    mov edi, [ebp+8]    ; dst
    mov eax, [ebp+12]   ; val
    mov ecx, [ebp+16]   ; n
    
    mov edx, edi        ; 戻り値のために保存

    ; valueを4バイト(DWORD)に拡張 (imul は 80386以降で有効)
    and eax, 0xFF
    imul eax, eax, 0x01010101
    
    cmp ecx, 4
    jb .do_bytes        ; n は u32 なので符号なし比較 (jl だと 2GB 超で誤判定)

    ; dst アライメント
    test edi, 3
    jz .aligned
.align_loop:
    stosb
    dec ecx
    test edi, 3
    jnz .align_loop

.aligned:
    ; DWORD単位の書き込み
    push ecx
    shr ecx, 2
    jz .ms_pop_recover      ; ECX>>2==0 でも pop ecx を必ず通過させる
    rep stosd

.ms_pop_recover:
    pop ecx                 ; push ecx と対になる pop (常に実行)
    and ecx, 3
    jz .done

.do_bytes:
    rep stosb

.done:
    mov eax, edx        ; 戻り値 eax = dst
    pop edi
    pop ebp
    ret

; ========================================================================
; u32 kstrlen(const char *s)
; u32 strlen(const char *s)
;
; repne scasb でNUL終端を高速スキャン。
; 最大探索長: 0xFFFFFFFF バイト (事実上無制限)
; ========================================================================
kstrlen:
strlen:
    push ebp
    mov ebp, esp
    push edi
    cld

    mov edi, [ebp+8]    ; s
    xor eax, eax        ; AL = 0 (NUL文字を探す)
    mov ecx, 0xFFFFFFFF ; 最大カウント
    repne scasb          ; EDI を進めつつ AL と比較

    ; ECX = 0xFFFFFFFF - 長さ - 1
    not ecx
    dec ecx
    mov eax, ecx        ; 戻り値 = 文字列長

    pop edi
    pop ebp
    ret

; ========================================================================
; int kstrcmp(const char *a, const char *b)
;
; 2つの文字列を1バイトずつ比較し、差分を返す。
; NUL終端の扱いが必要なため repe cmpsb 単体では不十分。
; 1バイトずつ比較するループを使用 (NUL チェック込み)。
; ========================================================================
strcmp:
kstrcmp:
    push ebp
    mov ebp, esp
    push esi
    push edi

    mov esi, [ebp+8]    ; a
    mov edi, [ebp+12]   ; b

.sc_loop:
    lodsb               ; AL = *a++
    mov cl, [edi]       ; CL = *b
    inc edi
    cmp al, cl
    jne .sc_diff
    test al, al         ; NUL到達?
    jnz .sc_loop

    ; 両方NUL → 等しい
    xor eax, eax
    jmp .sc_done

.sc_diff:
    movzx eax, al
    movzx ecx, cl
    sub eax, ecx

.sc_done:
    pop edi
    pop esi
    pop ebp
    ret

; ========================================================================
; int kstrncmp(const char *a, const char *b, u32 n)
;
; 最大nバイトまで比較。n==0 なら常に 0 を返す。
; ========================================================================
strncmp:
kstrncmp:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx

    mov esi, [ebp+8]    ; a
    mov edi, [ebp+12]   ; b
    mov ebx, [ebp+16]   ; n

    test ebx, ebx
    jz .snc_equal       ; n==0: 常に等しい

.snc_loop:
    lodsb               ; AL = *a++
    mov cl, [edi]       ; CL = *b
    inc edi
    cmp al, cl
    jne .snc_diff
    test al, al         ; NUL到達?
    jz .snc_equal
    dec ebx
    jnz .snc_loop

.snc_equal:
    xor eax, eax
    jmp .snc_done

.snc_diff:
    movzx eax, al
    movzx ecx, cl
    sub eax, ecx

.snc_done:
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret

; ========================================================================
; char *kstrcpy(char *dst, const char *src)
;
; 文字列コピー (NUL終端含む)。戻り値は dst。
; repne scasb で長さ取得 → rep movsb でバルクコピー。
; ========================================================================
kstrcpy:
    push ebp
    mov ebp, esp
    push edi
    push esi
    cld

    ; まず src の長さを計測 (NUL含む)
    mov edi, [ebp+12]   ; src
    xor eax, eax
    mov ecx, 0xFFFFFFFF
    repne scasb
    not ecx             ; ECX = 長さ + 1 (NUL含む)

    ; コピー実行
    mov esi, [ebp+12]   ; src
    mov edi, [ebp+8]    ; dst
    ; ECX は既にNUL含む長さ
    rep movsb

    mov eax, [ebp+8]    ; 戻り値 = dst

    pop esi
    pop edi
    pop ebp
    ret

; ========================================================================
; char *kstrncpy(char *dst, const char *src, u32 n)
;
; 最大 n バイトコピー、常に NUL 終端を保証。
; n == 0 の場合は何もしない。
; src の長さが n-1 未満なら src の長さ分だけコピーして NUL 終端。
; ========================================================================
kstrncpy:
    push ebp
    mov ebp, esp
    push edi
    push esi
    push ebx
    cld

    mov edi, [ebp+8]    ; dst
    mov esi, [ebp+12]   ; src
    mov ebx, [ebp+16]   ; n

    test ebx, ebx
    jz .sncpy_done      ; n==0: 何もしない

    ; src の長さを計測 (最大 n-1 バイト)
    push edi
    mov edi, esi        ; scasb 用に EDI = src
    xor eax, eax
    mov ecx, ebx        ; 最大 n バイトスキャン
    repne scasb
    pop edi

    jne .sncpy_no_nul
    ; NUL が見つかった: コピー長 = n - ECX - 1
    mov eax, ebx
    sub eax, ecx
    dec eax             ; NUL の前までの長さ
    jmp .sncpy_copy

.sncpy_no_nul:
    ; NUL が見つからなかった: n-1 バイトコピー
    mov eax, ebx
    dec eax

.sncpy_copy:
    ; EAX = コピーするバイト数
    mov ecx, eax
    rep movsb

    ; NUL 終端
    mov byte [edi], 0

.sncpy_done:
    mov eax, [ebp+8]    ; 戻り値 = dst
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret

; ========================================================================
; int memcmp(const void *a, const void *b, u32 n)
;
; 2つのメモリ領域をnバイト比較。等しければ0、a<bなら負、a>bなら正。
; ========================================================================
memcmp:
    push ebp
    mov ebp, esp
    push esi
    push edi
    cld

    mov esi, [ebp+8]    ; a
    mov edi, [ebp+12]   ; b
    mov ecx, [ebp+16]   ; n

    test ecx, ecx
    jz .mc_equal        ; n==0: 常に等しい

    ; DWORD単位で高速比較
    mov edx, ecx
    shr ecx, 2
    jz .mc_bytes
    repe cmpsd
    jne .mc_diff_dword

.mc_bytes:
    mov ecx, edx
    and ecx, 3
    jz .mc_equal
    repe cmpsb
    jne .mc_diff_byte

.mc_equal:
    xor eax, eax
    jmp .mc_done

.mc_diff_dword:
    ; 差異がDWORD内にあるので、4バイト戻してバイト単位で再比較
    sub esi, 4
    sub edi, 4
    mov ecx, 4
    repe cmpsb
    ; フォールスルー

.mc_diff_byte:
    movzx eax, byte [esi-1]
    movzx ecx, byte [edi-1]
    sub eax, ecx

.mc_done:
    pop edi
    pop esi
    pop ebp
    ret
