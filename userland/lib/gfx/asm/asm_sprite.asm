; =========================================================================
; asm_sprite.asm — スプライト描画コア (libos32gfx)
; =========================================================================
; クリップ付きスプライト描画の高速アセンブリ実装。
; ビューポートの左右境界でのバイト単位クリッピングを含む。
;
; 呼び出し元: gfx_sprite.c
; =========================================================================

bits 32

%include "gfx_const.inc"

global asm_gfx_draw_sprite_core

section .text

; =========================================================================
; void __cdecl asm_gfx_draw_sprite_core(
;     int x, int y, int w, int h,
;     int src_pitch, const u8 **planes,
;     const u8 *mask, u8 **bb_array);
;
; スプライトのプレーンデータをマスク付きでバックバッファに合成する。
; 各ピクセルバイトについて:
;   BB[plane] = (BB[plane] & mask) | (sprite[plane] & draw_mask)
; draw_mask はビューポートクリッピングを反映。
; =========================================================================

%define ARG_X     [ebp+8]
%define ARG_Y     [ebp+12]
%define ARG_W     [ebp+16]
%define ARG_H     [ebp+20]
%define ARG_SRCP  [ebp+24]
%define ARG_PLN   [ebp+28]
%define ARG_MASK  [ebp+32]
%define ARG_BB    [ebp+36]

%define L_BB0    [ebp-4]
%define L_BB1    [ebp-8]
%define L_BB2    [ebp-12]
%define L_BB3    [ebp-16]
%define L_SPR0   [ebp-20]
%define L_SPR1   [ebp-24]
%define L_SPR2   [ebp-28]
%define L_SPR3   [ebp-32]
%define L_IY     [ebp-36]
%define L_BX     [ebp-40]
%define L_DMASK  [ebp-44]

asm_gfx_draw_sprite_core:
    push ebp
    mov ebp, esp
    sub esp, 44
    push ebx
    push esi
    push edi

    ; Null check mask
    mov esi, ARG_MASK
    test esi, esi
    jz .end_func

    ; Cache BB pointers
    mov eax, ARG_BB
    mov edx, [eax]
    mov dword L_BB0, edx
    mov edx, [eax+4]
    mov dword L_BB1, edx
    mov edx, [eax+8]
    mov dword L_BB2, edx
    mov edx, [eax+12]
    mov dword L_BB3, edx

    ; Cache Plane pointers
    mov eax, ARG_PLN
    mov edx, [eax]
    mov dword L_SPR0, edx
    mov edx, [eax+4]
    mov dword L_SPR1, edx
    mov edx, [eax+8]
    mov dword L_SPR2, edx
    mov edx, [eax+12]
    mov dword L_SPR3, edx

    ; iy = 0
    mov dword L_IY, 0
.loop_y:
    ; 終了判定: iy < h
    mov eax, L_IY
    cmp eax, ARG_H
    jge .end_func

    ; dy = y + iy
    mov eax, ARG_Y
    add eax, L_IY
    
    ; if (dy < 0) continue
    cmp eax, 0
    jl .next_y
    ; if (dy >= GFX_HEIGHT) continue
    cmp eax, GFX_HEIGHT
    jge .next_y

    ; src_row = iy * src_pitch
    mov eax, L_IY
    imul eax, ARG_SRCP
    mov ecx, eax  ; ecx = src_row

    ; dst_row = dy * GFX_BPL + (x >> 3)
    mov eax, ARG_Y
    add eax, L_IY
    imul eax, GFX_BPL
    mov edx, ARG_X
    sar edx, 3
    add eax, edx
    mov edi, eax  ; edi = dst_row (byte offset into BB)

    ; bx = 0
    mov dword L_BX, 0
.loop_x:
    mov eax, L_BX
    cmp eax, ARG_SRCP
    jge .next_y

    ; dx_byte = (x >> 3) + bx
    mov edx, ARG_X
    sar edx, 3
    add edx, eax
    ; if (dx_byte < 0 || dx_byte >= GFX_BPL) continue
    cmp edx, 0
    jl .next_x
    cmp edx, GFX_BPL
    jge .next_x

    ; ebx = src_row + bx
    mov ebx, ecx
    add ebx, eax

    ; Load mask byte
    mov esi, ARG_MASK
    mov al, [esi + ebx]  ; al = mask byte

    ; ==== Xピクセルクリップ ====
    mov byte L_DMASK, 0xFF

    ; px_start = x + (bx * 8)
    mov ecx, L_BX
    shl ecx, 3
    add ecx, ARG_X       ; ecx = px_start

    ; -- 左クリップ --
    mov edx, 0
    sub edx, ecx
    cmp edx, 0
    jle .chk_right
    cmp edx, 8
    jge .skip_byte
    
    ; L_DMASK の上位 edx ビットを 0 にする
    push eax
    mov cl, dl
    mov al, L_DMASK
    shr al, cl
    mov L_DMASK, al
    pop eax
    ; ecx を px_start に戻す
    mov ecx, L_BX
    shl ecx, 3
    add ecx, ARG_X

.chk_right:
    ; -- 右クリップ --
    mov edx, ecx
    add edx, 8           ; edx = px_start + 8
    
    push eax
    mov eax, GFX_WIDTH  ; eax = VP_XMAX
    sub edx, eax
    pop eax
    
    cmp edx, 0
    jle .apply_mask
    cmp edx, 8
    jge .skip_byte
    
    ; L_DMASK の下位 edx ビットを 0 にする
    push eax
    mov cl, dl
    mov al, L_DMASK
    shl al, cl
    mov L_DMASK, al
    pop eax

.apply_mask:
    mov dh, L_DMASK      ; dh = draw_mask
    test dh, dh
    jz .skip_byte

    ; al (背景維持マスク) の、ビューポート外(dh=0)のビットを 1 にする
    mov dl, dh           ; dl = draw_mask
    not dl               ; dl = not draw_mask
    or al, dl            ; al のビューポート外ビットが 1 になる

    ; 描画用のレジスタを再構成
    mov edx, edi
    add edx, L_BX

    ; Plane 0
    mov esi, L_SPR0
    mov cl, [esi + ebx]
    mov ch, L_DMASK
    and cl, ch           ; ビューポート外の色データを消す
    mov esi, L_BB0
    mov ah, [esi + edx]
    and ah, al
    or  ah, cl
    mov [esi + edx], ah

    ; Plane 1
    mov esi, L_SPR1
    mov cl, [esi + ebx]
    mov ch, L_DMASK
    and cl, ch
    mov esi, L_BB1
    mov ah, [esi + edx]
    and ah, al
    or  ah, cl
    mov [esi + edx], ah

    ; Plane 2
    mov esi, L_SPR2
    mov cl, [esi + ebx]
    mov ch, L_DMASK
    and cl, ch
    mov esi, L_BB2
    mov ah, [esi + edx]
    and ah, al
    or  ah, cl
    mov [esi + edx], ah

    ; Plane 3
    mov esi, L_SPR3
    mov cl, [esi + ebx]
    mov ch, L_DMASK
    and cl, ch
    mov esi, L_BB3
    mov ah, [esi + edx]
    and ah, al
    or  ah, cl
    mov [esi + edx], ah

    ; Restore ecx (src_row)
    mov eax, L_IY
    imul eax, ARG_SRCP
    mov ecx, eax

.skip_byte:
.next_x:
    inc dword L_BX
    jmp .loop_x

.next_y:
    inc dword L_IY
    jmp .loop_y

.end_func:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret
