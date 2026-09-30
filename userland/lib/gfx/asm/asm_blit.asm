; =========================================================================
; asm_blit.asm — 高速透過ブリット (libos32gfx)
; =========================================================================
; colorkey=0 専用の透過合成を NASM で実装。
; 32bit → 16bit → 8bit の段階的処理で幅に応じた最適転送を行う。
;
; 呼び出し元: gfx_surface.c (gfx_blit_transparent)
; =========================================================================

bits 32

global asm_blit_transparent_core

section .text

; =========================================================================
; void __cdecl asm_blit_transparent_core(
;     u8 **dst_planes, int doff, int dst_pitch,
;     const u8 **src_planes, int soff, int src_pitch,
;     int width_bytes, int lines);
;
; スタック引数:
; [ebp+8]  : dst_planes  (BBのプレーン配列 u8*[4])
; [ebp+12] : doff        (初期オフセット: dy * pitch + dx_byte)
; [ebp+16] : dst_pitch   (BBのピッチ)
; [ebp+20] : src_planes  (転送元のプレーン配列 u8*[4])
; [ebp+24] : soff        (初期オフセット: sy * pitch + sx_byte)
; [ebp+28] : src_pitch   (転送元のピッチ)
; [ebp+32] : width_bytes (描画幅バイト数)
; [ebp+36] : lines       (描画行数)
; =========================================================================

%define ARG_DST_PLN  [ebp+8]
%define ARG_DOFF     [ebp+12]
%define ARG_DPITCH   [ebp+16]
%define ARG_SRC_PLN  [ebp+20]
%define ARG_SOFF     [ebp+24]
%define ARG_SPITCH   [ebp+28]
%define ARG_WB       [ebp+32]
%define ARG_LINES    [ebp+36]

; ローカル変数 (プレーンポインタキャッシュ + ループ状態)
%define L_D0   [ebp-4]
%define L_D1   [ebp-8]
%define L_D2   [ebp-12]
%define L_D3   [ebp-16]
%define L_S0   [ebp-20]
%define L_S1   [ebp-24]
%define L_S2   [ebp-28]
%define L_S3   [ebp-32]
%define L_Y    [ebp-36]
%define L_DOFF [ebp-40]
%define L_SOFF [ebp-44]

asm_blit_transparent_core:
    push ebp
    mov ebp, esp
    sub esp, 44
    push ebx
    push esi
    push edi

    ; ---- プレーンポインタをキャッシュ ----
    mov eax, ARG_DST_PLN
    mov edx, [eax]
    mov dword L_D0, edx
    mov edx, [eax+4]
    mov dword L_D1, edx
    mov edx, [eax+8]
    mov dword L_D2, edx
    mov edx, [eax+12]
    mov dword L_D3, edx

    mov eax, ARG_SRC_PLN
    mov edx, [eax]
    mov dword L_S0, edx
    mov edx, [eax+4]
    mov dword L_S1, edx
    mov edx, [eax+8]
    mov dword L_S2, edx
    mov edx, [eax+12]
    mov dword L_S3, edx

    ; ---- Yループ ----
    mov dword L_Y, 0
.loop_y:
    mov eax, L_Y
    cmp eax, ARG_LINES
    jge .end_func

    ; 行オフセットを計算してローカルに保存
    mov ecx, eax
    imul ecx, ARG_DPITCH
    add ecx, ARG_DOFF
    mov dword L_DOFF, ecx

    mov edx, eax
    imul edx, ARG_SPITCH
    add edx, ARG_SOFF
    mov dword L_SOFF, edx

    xor ebx, ebx           ; ebx = バイトインデックス (i)

    ; ================================================================
    ; 32bit (4バイト = 32ピクセル) ループ
    ; ================================================================
.loop_x_32:
    lea eax, [ebx + 4]
    cmp eax, ARG_WB
    jg .loop_x_16

    ; esi = src offset, edi = dst offset
    mov esi, L_SOFF
    add esi, ebx
    mov edi, L_DOFF
    add edi, ebx

    ; mask = s0 | s1 | s2 | s3  (ソースデータをORして透明判定)
    mov eax, L_S0
    mov edx, [eax + esi]
    mov eax, L_S1
    or  edx, [eax + esi]
    mov eax, L_S2
    or  edx, [eax + esi]
    mov eax, L_S3
    or  edx, [eax + esi]

    test edx, edx
    jz .next_32             ; 全透明 → スキップ

    cmp edx, 0xFFFFFFFF
    je .full_32

    ; ---- 部分透明: ビット合成 ----
    ; edx = keep = ~mask
    not edx

    ; Plane 0: dst = (dst & keep) | src
    mov eax, L_D0
    mov ecx, [eax + edi]
    and ecx, edx
    mov eax, L_S0
    or  ecx, [eax + esi]
    mov eax, L_D0
    mov [eax + edi], ecx

    ; Plane 1
    mov eax, L_D1
    mov ecx, [eax + edi]
    and ecx, edx
    mov eax, L_S1
    or  ecx, [eax + esi]
    mov eax, L_D1
    mov [eax + edi], ecx

    ; Plane 2
    mov eax, L_D2
    mov ecx, [eax + edi]
    and ecx, edx
    mov eax, L_S2
    or  ecx, [eax + esi]
    mov eax, L_D2
    mov [eax + edi], ecx

    ; Plane 3
    mov eax, L_D3
    mov ecx, [eax + edi]
    and ecx, edx
    mov eax, L_S3
    or  ecx, [eax + esi]
    mov eax, L_D3
    mov [eax + edi], ecx
    jmp .next_32

.full_32:
    ; ---- 全不透明: 無条件コピー ----
    mov eax, L_S0
    mov ecx, [eax + esi]
    mov eax, L_D0
    mov [eax + edi], ecx

    mov eax, L_S1
    mov ecx, [eax + esi]
    mov eax, L_D1
    mov [eax + edi], ecx

    mov eax, L_S2
    mov ecx, [eax + esi]
    mov eax, L_D2
    mov [eax + edi], ecx

    mov eax, L_S3
    mov ecx, [eax + esi]
    mov eax, L_D3
    mov [eax + edi], ecx

.next_32:
    add ebx, 4
    jmp .loop_x_32

    ; ================================================================
    ; 16bit (2バイト = 16ピクセル) ループ
    ; ================================================================
.loop_x_16:
    lea eax, [ebx + 2]
    cmp eax, ARG_WB
    jg .loop_x_8

    mov esi, L_SOFF
    add esi, ebx
    mov edi, L_DOFF
    add edi, ebx

    ; mask (16bit)
    mov eax, L_S0
    movzx edx, word [eax + esi]
    mov eax, L_S1
    movzx ecx, word [eax + esi]
    or  edx, ecx
    mov eax, L_S2
    movzx ecx, word [eax + esi]
    or  edx, ecx
    mov eax, L_S3
    movzx ecx, word [eax + esi]
    or  edx, ecx

    test dx, dx
    jz .next_16

    cmp dx, 0xFFFF
    je .full_16

    ; ---- 部分透明 (16bit) ----
    not dx                  ; 16bit NOT でキープマスク生成

    ; Plane 0
    mov eax, L_D0
    movzx ecx, word [eax + edi]
    and cx, dx
    mov eax, L_S0
    or  cx, word [eax + esi]
    mov eax, L_D0
    mov word [eax + edi], cx

    ; Plane 1
    mov eax, L_D1
    movzx ecx, word [eax + edi]
    and cx, dx
    mov eax, L_S1
    or  cx, word [eax + esi]
    mov eax, L_D1
    mov word [eax + edi], cx

    ; Plane 2
    mov eax, L_D2
    movzx ecx, word [eax + edi]
    and cx, dx
    mov eax, L_S2
    or  cx, word [eax + esi]
    mov eax, L_D2
    mov word [eax + edi], cx

    ; Plane 3
    mov eax, L_D3
    movzx ecx, word [eax + edi]
    and cx, dx
    mov eax, L_S3
    or  cx, word [eax + esi]
    mov eax, L_D3
    mov word [eax + edi], cx
    jmp .next_16

.full_16:
    ; ---- 全不透明 (16bit) ----
    mov eax, L_S0
    mov cx, word [eax + esi]
    mov eax, L_D0
    mov word [eax + edi], cx

    mov eax, L_S1
    mov cx, word [eax + esi]
    mov eax, L_D1
    mov word [eax + edi], cx

    mov eax, L_S2
    mov cx, word [eax + esi]
    mov eax, L_D2
    mov word [eax + edi], cx

    mov eax, L_S3
    mov cx, word [eax + esi]
    mov eax, L_D3
    mov word [eax + edi], cx

.next_16:
    add ebx, 2
    jmp .loop_x_16

    ; ================================================================
    ; 8bit (1バイト = 8ピクセル) ループ
    ; ================================================================
.loop_x_8:
    cmp ebx, ARG_WB
    jge .next_y

    mov esi, L_SOFF
    add esi, ebx
    mov edi, L_DOFF
    add edi, ebx

    ; mask (8bit)
    mov eax, L_S0
    movzx edx, byte [eax + esi]
    mov eax, L_S1
    movzx ecx, byte [eax + esi]
    or  dl, cl
    mov eax, L_S2
    movzx ecx, byte [eax + esi]
    or  dl, cl
    mov eax, L_S3
    movzx ecx, byte [eax + esi]
    or  dl, cl

    test dl, dl
    jz .next_8

    cmp dl, 0xFF
    je .full_8

    ; ---- 部分透明 (8bit) ----
    not dl                  ; 8bit NOT

    ; Plane 0
    mov eax, L_D0
    mov cl, byte [eax + edi]
    and cl, dl
    mov eax, L_S0
    or  cl, byte [eax + esi]
    mov eax, L_D0
    mov byte [eax + edi], cl

    ; Plane 1
    mov eax, L_D1
    mov cl, byte [eax + edi]
    and cl, dl
    mov eax, L_S1
    or  cl, byte [eax + esi]
    mov eax, L_D1
    mov byte [eax + edi], cl

    ; Plane 2
    mov eax, L_D2
    mov cl, byte [eax + edi]
    and cl, dl
    mov eax, L_S2
    or  cl, byte [eax + esi]
    mov eax, L_D2
    mov byte [eax + edi], cl

    ; Plane 3
    mov eax, L_D3
    mov cl, byte [eax + edi]
    and cl, dl
    mov eax, L_S3
    or  cl, byte [eax + esi]
    mov eax, L_D3
    mov byte [eax + edi], cl
    jmp .next_8

.full_8:
    ; ---- 全不透明 (8bit) ----
    mov eax, L_S0
    mov cl, byte [eax + esi]
    mov eax, L_D0
    mov byte [eax + edi], cl

    mov eax, L_S1
    mov cl, byte [eax + esi]
    mov eax, L_D1
    mov byte [eax + edi], cl

    mov eax, L_S2
    mov cl, byte [eax + esi]
    mov eax, L_D2
    mov byte [eax + edi], cl

    mov eax, L_S3
    mov cl, byte [eax + esi]
    mov eax, L_D3
    mov byte [eax + edi], cl

.next_8:
    inc ebx
    jmp .loop_x_8

.next_y:
    inc dword L_Y
    jmp .loop_y

.end_func:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret
