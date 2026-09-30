; =========================================================================
; asm_draw.asm — 描画プリミティブ (libos32gfx)
; =========================================================================
; 画面クリア、フォント描画、矩形塗りつぶし/コピー、水平線、直線
; の高速アセンブリ実装。
;
; 呼び出し元: gfx_draw.c, gfx_surface.c, gfx_blt.c
; =========================================================================

bits 32

%include "gfx_const.inc"

global asm_gfx_clear
global asm_fill_plane_rect
global asm_copy_plane_rect
global asm_gfx_hline
global asm_gfx_line
global asm_kcg_draw_font

section .text

; =========================================================================
; void __cdecl asm_gfx_clear(unsigned char color, unsigned char *bb[4]);
;
; 全4プレーンを指定色でクリア (rep stosd による高速fill)。
; =========================================================================
asm_gfx_clear:
    push ebp
    mov ebp, esp
    push edi
    push esi

    mov dl, [ebp+8]      ; color
    mov esi, [ebp+12]    ; bb array pointer

    ; プレーン 0 (B)
    mov edi, [esi]       ; bb[0]
    mov ecx, GFX_PLANE_SZ / 4
    xor eax, eax
    test dl, 1
    jz .p0_fill
    not eax
.p0_fill:
    rep stosd

    ; プレーン 1 (R)
    mov edi, [esi+4]     ; bb[1]
    mov ecx, GFX_PLANE_SZ / 4
    xor eax, eax
    test dl, 2
    jz .p1_fill
    not eax
.p1_fill:
    rep stosd

    ; プレーン 2 (G)
    mov edi, [esi+8]     ; bb[2]
    mov ecx, GFX_PLANE_SZ / 4
    xor eax, eax
    test dl, 4
    jz .p2_fill
    not eax
.p2_fill:
    rep stosd

    ; プレーン 3 (I)
    mov edi, [esi+12]    ; bb[3]
    mov ecx, GFX_PLANE_SZ / 4
    xor eax, eax
    test dl, 8
    jz .p3_fill
    not eax
.p3_fill:
    rep stosd

    pop esi
    pop edi
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_kcg_draw_font(int x, int y, const unsigned char *pat,
;                                int w_bytes, int h_lines,
;                                unsigned char fg, unsigned char *bb[4]);
;
; KCGフォントパターンをバックバッファに描画する。
; 前景色 fg のビットに応じて各プレーンに OR / AND でパターンを合成。
; =========================================================================
%define ARG_X    [ebp+8]
%define ARG_Y    [ebp+12]
%define ARG_PAT  [ebp+16]
%define ARG_WB   [ebp+20]
%define ARG_HL   [ebp+24]
%define ARG_FG   [ebp+28]
%define ARG_BB   [ebp+32]

%define L_BB0    [ebp-4]
%define L_BB1    [ebp-8]
%define L_BB2    [ebp-12]
%define L_BB3    [ebp-16]
%define L_IY     [ebp-20]
%define L_BX     [ebp-24]

asm_kcg_draw_font:
    push ebp
    mov ebp, esp
    sub esp, 24
    push ebx
    push esi
    push edi

    ; Null check pat
    mov esi, ARG_PAT
    test esi, esi
    jz .kcg_end

    ; Cache pointers
    mov eax, ARG_BB
    mov edx, [eax]
    mov dword L_BB0, edx
    mov edx, [eax+4]
    mov dword L_BB1, edx
    mov edx, [eax+8]
    mov dword L_BB2, edx
    mov edx, [eax+12]
    mov dword L_BB3, edx

    ; iy = 0
    mov dword L_IY, 0
.kcg_loop_y:
    mov esi, ARG_PAT
    mov eax, L_IY
    cmp eax, ARG_HL
    jge .kcg_end

    ; dy = y + iy
    mov eax, ARG_Y
    add eax, L_IY
    
    ; if (dy < 0 || dy >= GFX_HEIGHT) continue
    cmp eax, 0
    jl .kcg_next_y
    cmp eax, GFX_HEIGHT
    jge .kcg_next_y

    ; src_row = iy * w_bytes
    mov eax, L_IY
    imul eax, ARG_WB
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
.kcg_loop_x:
    mov eax, L_BX
    cmp eax, ARG_WB
    jge .kcg_next_y

    ; dx_byte = (x >> 3) + bx
    mov edx, ARG_X
    sar edx, 3
    add edx, eax
    ; if (dx_byte < 0 || dx_byte >= GFX_BPL) continue
    cmp edx, 0
    jl .kcg_next_x
    cmp edx, GFX_BPL
    jge .kcg_next_x

    ; ebx = src pointer offset
    mov ebx, ecx  ; src_row
    add ebx, eax  ; src_row + bx

    ; Load pat byte
    mov esi, ARG_PAT
    mov al, [esi + ebx]  ; al = pat byte
    test al, al
    jz .kcg_next_x       ; if pat byte is 0, nothing to draw

    ; edx = dst pointer offset
    mov edx, edi  ; dst_row
    mov ebx, L_BX
    add edx, ebx  ; dst_row + bx
    
    mov bl, ARG_FG   ; bl = fg color

    ; --- Plane 0 ---
    mov esi, L_BB0
    mov ah, [esi + edx]
    test bl, 1
    jz .k_clr0
    or ah, al
    jmp .k_wr0
.k_clr0:
    mov bh, al
    not bh
    and ah, bh
.k_wr0:
    mov [esi + edx], ah

    ; --- Plane 1 ---
    mov esi, L_BB1
    mov ah, [esi + edx]
    test bl, 2
    jz .k_clr1
    or ah, al
    jmp .k_wr1
.k_clr1:
    mov bh, al
    not bh
    and ah, bh
.k_wr1:
    mov [esi + edx], ah

    ; --- Plane 2 ---
    mov esi, L_BB2
    mov ah, [esi + edx]
    test bl, 4
    jz .k_clr2
    or ah, al
    jmp .k_wr2
.k_clr2:
    mov bh, al
    not bh
    and ah, bh
.k_wr2:
    mov [esi + edx], ah

    ; --- Plane 3 ---
    mov esi, L_BB3
    mov ah, [esi + edx]
    test bl, 8
    jz .k_clr3
    or ah, al
    jmp .k_wr3
.k_clr3:
    mov bh, al
    not bh
    and ah, bh
.k_wr3:
    mov [esi + edx], ah

.kcg_next_x:
    inc dword L_BX
    jmp .kcg_loop_x

.kcg_next_y:
    inc dword L_IY
    jmp .kcg_loop_y

.kcg_end:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_fill_plane_rect(u8 *start, int pitch, int rows,
;                                  int width_bytes, u8 fill_val);
;
; 1プレーンの矩形領域を fill_val で塗りつぶす。
; start を起点に width_bytes バイト分を fill し、pitch バイト進めて
; rows 行繰り返す。rep stosd + rep stosb による高速 fill。
; =========================================================================
asm_fill_plane_rect:
    push ebp
    mov ebp, esp
    push edi
    push esi
    push ebx

    mov edi, [ebp+8]       ; start
    mov esi, [ebp+16]      ; rows
    mov ebx, [ebp+20]      ; width_bytes

    test esi, esi
    jle .fpr_done
    test ebx, ebx
    jle .fpr_done

    ; fill_val を 32bit に展開 (例: 0xFF → 0xFFFFFFFF)
    movzx eax, byte [ebp+24]
    mov ah, al
    mov ecx, eax
    shl ecx, 16
    or eax, ecx

    mov edx, [ebp+12]      ; pitch

.fpr_row:
    push edi

    mov ecx, ebx
    shr ecx, 2
    rep stosd

    mov ecx, ebx
    and ecx, 3
    rep stosb

    pop edi
    add edi, edx
    dec esi
    jnz .fpr_row

.fpr_done:
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_copy_plane_rect(u8 *dst, int dst_pitch,
;                                  const u8 *src, int src_pitch,
;                                  int rows, int width_bytes);
;
; src から dst へ矩形領域をコピー。
; rep movsd + rep movsb による高速コピー。
; dst_pitch と src_pitch が異なる場合もサポート。
; =========================================================================
asm_copy_plane_rect:
    push ebp
    mov ebp, esp
    sub esp, 4              ; ローカル変数: 行カウンタ
    push edi
    push esi
    push ebx

    mov edi, [ebp+8]       ; dst
    mov esi, [ebp+16]      ; src
    mov eax, [ebp+24]      ; rows
    mov ebx, [ebp+28]      ; width_bytes

    mov [ebp-4], eax        ; 行カウンタ保存

    test eax, eax
    jle .cpr_done
    test ebx, ebx
    jle .cpr_done

.cpr_row:
    push edi
    push esi

    mov ecx, ebx
    shr ecx, 2
    rep movsd

    mov ecx, ebx
    and ecx, 3
    rep movsb

    pop esi
    pop edi
    add edi, [ebp+12]      ; dst += dst_pitch
    add esi, [ebp+20]      ; src += src_pitch
    dec dword [ebp-4]
    jnz .cpr_row

.cpr_done:
    pop ebx
    pop esi
    pop edi
    mov esp, ebp
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_gfx_hline(u8 **planes, int base, int x, int x2,
;                            u8 color);
;
; バックバッファ上に水平線を描画する。
; planes: 4プレーンのポインタ配列
; base:   y * pitch (呼び出し側で事前計算)
; x:      左端X座標 (クリップ済み)
; x2:     右端X座標 (クリップ済み, x <= x2)
; color:  パレット番号 (0-15)
;
; バックバッファはメインRAM上にあるため、rep stosd (32bit) が有効。
; =========================================================================

%define HL_PLANES [ebp+8]
%define HL_BASE   [ebp+12]
%define HL_X      [ebp+16]
%define HL_X2     [ebp+20]
%define HL_COLOR  [ebp+24]

asm_gfx_hline:
    push ebp
    mov ebp, esp
    sub esp, 32
    push ebx
    push esi
    push edi

    ; ローカル変数:
    ;   [ebp-4]  = plane0 + base
    ;   [ebp-8]  = plane1 + base
    ;   [ebp-12] = plane2 + base
    ;   [ebp-16] = plane3 + base
    ;   [ebp-20] = bx1 (中間開始X, バイト境界)
    ;   [ebp-24] = bx2 (中間終了X, バイト境界)

    ; 4プレーンの base ポインタをキャッシュ
    mov esi, HL_PLANES
    mov eax, HL_BASE
    mov edx, [esi]
    add edx, eax
    mov [ebp-4], edx
    mov edx, [esi+4]
    add edx, eax
    mov [ebp-8], edx
    mov edx, [esi+8]
    add edx, eax
    mov [ebp-12], edx
    mov edx, [esi+12]
    add edx, eax
    mov [ebp-16], edx

    mov eax, HL_X
    mov edx, HL_X2

    ; ---- 同一バイト内チェック ----
    mov ecx, eax
    shr ecx, 3
    mov ebx, edx
    shr ebx, 3
    cmp ecx, ebx
    jne .hl_multi_byte

    ; 同一バイト: mask = (0xFF >> (x&7)) & (0xFF << (7-(x2&7)))
    push edx              ; x2 を保存

    mov ecx, eax
    and ecx, 7
    mov bl, 0xFF
    shr bl, cl

    pop edx
    mov ecx, edx
    and ecx, 7
    mov cl, 7
    sub cl, dl
    and cl, 7
    mov bh, 0xFF
    shl bh, cl
    and bl, bh            ; bl = mask

    ; バイトオフセット
    mov eax, HL_X
    shr eax, 3

    mov cl, HL_COLOR

    ; Plane 0
    mov edi, [ebp-4]
    test cl, 1
    jz .hl_same_c0
    or [edi + eax], bl
    jmp .hl_same_p1
.hl_same_c0:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_same_p1:
    mov edi, [ebp-8]
    test cl, 2
    jz .hl_same_c1
    or [edi + eax], bl
    jmp .hl_same_p2
.hl_same_c1:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_same_p2:
    mov edi, [ebp-12]
    test cl, 4
    jz .hl_same_c2
    or [edi + eax], bl
    jmp .hl_same_p3
.hl_same_c2:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_same_p3:
    mov edi, [ebp-16]
    test cl, 8
    jz .hl_same_c3
    or [edi + eax], bl
    jmp .hl_done
.hl_same_c3:
    mov bh, bl
    not bh
    and [edi + eax], bh
    jmp .hl_done

.hl_multi_byte:
    ; ---- 左端パーシャルバイト ----
    mov eax, HL_X
    test eax, 7
    jz .hl_no_left

    mov ecx, eax
    and ecx, 7
    mov bl, 0xFF
    shr bl, cl            ; left_mask

    mov eax, HL_X
    shr eax, 3            ; byte offset

    mov cl, HL_COLOR

    mov edi, [ebp-4]
    test cl, 1
    jz .hl_left_c0
    or [edi + eax], bl
    jmp .hl_left_d1
.hl_left_c0:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_left_d1:
    mov edi, [ebp-8]
    test cl, 2
    jz .hl_left_c1
    or [edi + eax], bl
    jmp .hl_left_d2
.hl_left_c1:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_left_d2:
    mov edi, [ebp-12]
    test cl, 4
    jz .hl_left_c2
    or [edi + eax], bl
    jmp .hl_left_d3
.hl_left_c2:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_left_d3:
    mov edi, [ebp-16]
    test cl, 8
    jz .hl_left_c3
    or [edi + eax], bl
    jmp .hl_left_done
.hl_left_c3:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_left_done:
    ; bx1 = (x | 7) + 1
    mov eax, HL_X
    or eax, 7
    inc eax
    mov [ebp-20], eax
    jmp .hl_right

.hl_no_left:
    mov eax, HL_X
    mov [ebp-20], eax

.hl_right:
    ; ---- 右端パーシャルバイト ----
    mov edx, HL_X2
    mov eax, edx
    and eax, 7
    cmp eax, 7
    je .hl_no_right

    mov ecx, 7
    sub ecx, eax
    mov bl, 0xFF
    shl bl, cl            ; right_mask

    mov eax, edx
    shr eax, 3            ; byte offset

    mov cl, HL_COLOR

    mov edi, [ebp-4]
    test cl, 1
    jz .hl_right_c0
    or [edi + eax], bl
    jmp .hl_right_d1
.hl_right_c0:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_right_d1:
    mov edi, [ebp-8]
    test cl, 2
    jz .hl_right_c1
    or [edi + eax], bl
    jmp .hl_right_d2
.hl_right_c1:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_right_d2:
    mov edi, [ebp-12]
    test cl, 4
    jz .hl_right_c2
    or [edi + eax], bl
    jmp .hl_right_d3
.hl_right_c2:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_right_d3:
    mov edi, [ebp-16]
    test cl, 8
    jz .hl_right_c3
    or [edi + eax], bl
    jmp .hl_right_done
.hl_right_c3:
    mov bh, bl
    not bh
    and [edi + eax], bh
.hl_right_done:
    ; bx2 = x2 & ~7
    mov eax, HL_X2
    and eax, ~7
    mov [ebp-24], eax
    jmp .hl_middle

.hl_no_right:
    ; bx2 = x2 + 1
    mov eax, HL_X2
    inc eax
    mov [ebp-24], eax

.hl_middle:
    ; ---- 中間フルバイト: rep stosd + rep stosb ----
    mov eax, [ebp-20]     ; bx1
    mov edx, [ebp-24]     ; bx2
    cmp eax, edx
    jge .hl_done

    ; start_byte = bx1 >> 3, count = (bx2 - bx1) >> 3
    mov ebx, eax
    shr ebx, 3            ; start_byte
    mov ecx, edx
    sub ecx, eax
    shr ecx, 3            ; count (bytes)
    test ecx, ecx
    jle .hl_done

    ; count をスタックに保存
    push ecx              ; [esp] = count

    mov dl, HL_COLOR

    ; --- Plane 0 ---
    xor eax, eax
    test dl, 1
    jz .hl_mid_p0
    not eax
.hl_mid_p0:
    mov edi, [ebp-4]
    add edi, ebx
    mov ecx, [esp]
    push edi
    push ecx
    shr ecx, 2
    rep stosd
    pop ecx
    and ecx, 3
    rep stosb
    pop edi

    ; --- Plane 1 ---
    xor eax, eax
    test dl, 2
    jz .hl_mid_p1
    not eax
.hl_mid_p1:
    mov edi, [ebp-8]
    add edi, ebx
    mov ecx, [esp]
    push edi
    push ecx
    shr ecx, 2
    rep stosd
    pop ecx
    and ecx, 3
    rep stosb
    pop edi

    ; --- Plane 2 ---
    xor eax, eax
    test dl, 4
    jz .hl_mid_p2
    not eax
.hl_mid_p2:
    mov edi, [ebp-12]
    add edi, ebx
    mov ecx, [esp]
    push edi
    push ecx
    shr ecx, 2
    rep stosd
    pop ecx
    and ecx, 3
    rep stosb
    pop edi

    ; --- Plane 3 ---
    xor eax, eax
    test dl, 8
    jz .hl_mid_p3
    not eax
.hl_mid_p3:
    mov edi, [ebp-16]
    add edi, ebx
    mov ecx, [esp]
    push ecx
    shr ecx, 2
    rep stosd
    pop ecx
    and ecx, 3
    rep stosb

    pop ecx               ; count を片付け

.hl_done:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_gfx_line(u8 **planes, int pitch,
;                           int x0, int y0, int x1, int y1, u8 color);
;
; Bresenhamアルゴリズムによる直線描画。
; pixel描画をインラインで実行し、関数呼び出しオーバーヘッドを排除。
; 境界チェックはループ内で行う (クリッピングは呼び出し側非依存)。
;
; バックバッファはメインRAM上のプレーナー形式。
; =========================================================================

%define LN_PLANES [ebp+8]
%define LN_PITCH  [ebp+12]
%define LN_X0     [ebp+16]
%define LN_Y0     [ebp+20]
%define LN_X1     [ebp+24]
%define LN_Y1     [ebp+28]
%define LN_COLOR  [ebp+32]

asm_gfx_line:
    push ebp
    mov ebp, esp
    sub esp, 48
    push ebx
    push esi
    push edi

    ; ローカル変数:
    ;   [ebp-4]  = plane0
    ;   [ebp-8]  = plane1
    ;   [ebp-12] = plane2
    ;   [ebp-16] = plane3
    ;   [ebp-20] = dx (abs)
    ;   [ebp-24] = dy (abs)
    ;   [ebp-28] = sx (+1 or -1)
    ;   [ebp-32] = sy (+1 or -1)
    ;   [ebp-36] = err
    ;   [ebp-40] = x1 (終点)
    ;   [ebp-44] = y1 (終点)

    ; プレーンポインタをキャッシュ
    mov esi, LN_PLANES
    mov eax, [esi]
    mov [ebp-4], eax
    mov eax, [esi+4]
    mov [ebp-8], eax
    mov eax, [esi+8]
    mov [ebp-12], eax
    mov eax, [esi+12]
    mov [ebp-16], eax

    ; dx = abs(x1 - x0), sx = sign
    mov eax, LN_X1
    sub eax, LN_X0
    mov dword [ebp-28], 1
    cmp eax, 0
    jge .ln_dx_pos
    neg eax
    mov dword [ebp-28], -1
.ln_dx_pos:
    mov [ebp-20], eax

    ; dy = abs(y1 - y0), sy = sign
    mov eax, LN_Y1
    sub eax, LN_Y0
    mov dword [ebp-32], 1
    cmp eax, 0
    jge .ln_dy_pos
    neg eax
    mov dword [ebp-32], -1
.ln_dy_pos:
    mov [ebp-24], eax

    ; err = dx - dy
    mov eax, [ebp-20]
    sub eax, [ebp-24]
    mov [ebp-36], eax

    ; 終点を保存
    mov eax, LN_X1
    mov [ebp-40], eax
    mov eax, LN_Y1
    mov [ebp-44], eax

    ; 現在の座標をレジスタに (esi = x, ebx = y)
    mov esi, LN_X0
    mov ebx, LN_Y0

.ln_loop:
    ; ---- インライン pixel 描画 ----
    ; 境界チェック
    cmp esi, 0
    jl .ln_skip_pixel
    cmp esi, 640
    jge .ln_skip_pixel
    cmp ebx, 0
    jl .ln_skip_pixel
    cmp ebx, GFX_HEIGHT
    jge .ln_skip_pixel

    ; offset = y * pitch + (x >> 3)
    mov eax, ebx
    imul eax, dword LN_PITCH
    mov edi, esi
    shr edi, 3
    add edi, eax           ; edi = offset

    ; bit = 0x80 >> (x & 7)
    mov ecx, esi
    and ecx, 7
    mov ah, 0x80
    shr ah, cl             ; ah = bit mask

    mov al, LN_COLOR

    ; Plane 0
    mov ecx, [ebp-4]
    test al, 1
    jz .ln_px_c0
    or [ecx + edi], ah
    jmp .ln_px_d1
.ln_px_c0:
    mov dl, ah
    not dl
    and [ecx + edi], dl
.ln_px_d1:
    ; Plane 1
    mov ecx, [ebp-8]
    test al, 2
    jz .ln_px_c1
    or [ecx + edi], ah
    jmp .ln_px_d2
.ln_px_c1:
    mov dl, ah
    not dl
    and [ecx + edi], dl
.ln_px_d2:
    ; Plane 2
    mov ecx, [ebp-12]
    test al, 4
    jz .ln_px_c2
    or [ecx + edi], ah
    jmp .ln_px_d3
.ln_px_c2:
    mov dl, ah
    not dl
    and [ecx + edi], dl
.ln_px_d3:
    ; Plane 3
    mov ecx, [ebp-16]
    test al, 8
    jz .ln_px_c3
    or [ecx + edi], ah
    jmp .ln_skip_pixel
.ln_px_c3:
    mov dl, ah
    not dl
    and [ecx + edi], dl

.ln_skip_pixel:
    ; ---- 終点判定 ----
    cmp esi, [ebp-40]
    jne .ln_step
    cmp ebx, [ebp-44]
    je .ln_end

.ln_step:
    ; e2 = err * 2
    mov eax, [ebp-36]
    lea eax, [eax + eax]  ; e2

    ; if (e2 > -dy) { err -= dy; x += sx; }
    mov ecx, [ebp-24]
    neg ecx                ; -dy
    cmp eax, ecx
    jle .ln_no_xstep
    mov ecx, [ebp-24]
    sub dword [ebp-36], ecx
    add esi, [ebp-28]
.ln_no_xstep:

    ; if (e2 < dx) { err += dx; y += sy; }
    cmp eax, [ebp-20]
    jge .ln_no_ystep
    mov ecx, [ebp-20]
    add [ebp-36], ecx
    add ebx, [ebp-32]
.ln_no_ystep:
    jmp .ln_loop

.ln_end:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret
