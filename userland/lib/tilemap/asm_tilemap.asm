; =========================================================================
; asm_tilemap.asm — タイルマップエンジン NASM 最適化ルーチン
; =========================================================================
; libtilemap の描画ホットパスを NASM で最適化。
;
; 1. asm_tile_pair_opaque  : 2タイルDWORD同時転送 (btf_fast用)
; 2. asm_tile_pair_masked  : カバレッジマスク付き1行描画 (FtB用)
; 3. asm_byte_reverse_lut  : 256バイトLUT (H-flip用)
; 4. asm_bb_shift_horiz    : BB水平バイトシフト (rep movsd)
; 5. asm_bb_shift_vert     : BB垂直ラインシフト (rep movsd)
; =========================================================================

bits 32

global asm_tile_pair_opaque
global asm_tile_pair_masked
global _asm_byte_reverse_lut
global asm_bb_shift_horiz
global asm_bb_shift_vert

section .data

; =========================================================================
; 256バイト ビットリバースLUT (H-flip用)
; byte_reverse_lut[i] = ビット反転値 (例: 0b10110000 → 0b00001101)
; =========================================================================
_asm_byte_reverse_lut:
    ; 0x00-0x0F
    db 0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0
    db 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0
    ; 0x10-0x1F
    db 0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8
    db 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8
    ; 0x20-0x2F
    db 0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4
    db 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4
    ; 0x30-0x3F
    db 0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC
    db 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC
    ; 0x40-0x4F
    db 0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2
    db 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2
    ; 0x50-0x5F
    db 0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA
    db 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA
    ; 0x60-0x6F
    db 0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6
    db 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6
    ; 0x70-0x7F
    db 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE
    db 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE
    ; 0x80-0x8F
    db 0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1
    db 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1
    ; 0x90-0x9F
    db 0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9
    db 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9
    ; 0xA0-0xAF
    db 0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5
    db 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5
    ; 0xB0-0xBF
    db 0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED
    db 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD
    ; 0xC0-0xCF
    db 0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3
    db 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3
    ; 0xD0-0xDF
    db 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB
    db 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB
    ; 0xE0-0xEF
    db 0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7
    db 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7
    ; 0xF0-0xFF
    db 0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF
    db 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF


section .text

; =========================================================================
; void __cdecl asm_tile_pair_opaque(
;     u8 **bb_planes,   [ebp+8]   BB プレーンポインタ配列
;     int  dst_off,     [ebp+12]  初期バイトオフセット (dy * pitch + dx/8)
;     int  bb_pitch,    [ebp+16]  BBピッチ (80)
;     const u8 *tile_l, [ebp+20]  左タイル planes[0] の先頭
;     const u8 *tile_r, [ebp+24]  右タイル planes[0] の先頭
;     int  tile_psz);   [ebp+28]  TILE_PLANE_SZ (32)
;
; 2タイル (32px = 4bytes/row) を4プレーン × 16行 = 64 DWORD でBBに書き込む。
; タイルデータのレイアウト: planes[p] は先頭から p * tile_psz のオフセット。
; =========================================================================
asm_tile_pair_opaque:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov edi, [ebp+8]       ; bb_planes

    xor ecx, ecx           ; ecx = plane (0-3)
.plane_loop:
    cmp ecx, 4
    jge .done

    ; edi = bb_planes, edx = bb_planes[ecx] + dst_off
    mov edx, [edi + ecx*4]
    add edx, [ebp+12]      ; + dst_off

    ; esi_l = tile_l + ecx * tile_psz
    mov eax, ecx
    imul eax, [ebp+28]     ; * TILE_PLANE_SZ
    mov esi, [ebp+20]      ; tile_l
    add esi, eax           ; esi = tile_l + plane_offset

    ; ebx = tile_r + ecx * tile_psz
    mov ebx, [ebp+24]      ; tile_r
    add ebx, eax           ; ebx = tile_r + plane_offset

    ; 16行ループ (行インデックスは eax)
    push ecx               ; plane を退避
    xor ecx, ecx           ; row = 0
.row_loop:
    cmp ecx, 16
    jge .next_plane

    ; DWORD = (left[row*2] << 24) | (left[row*2+1] << 16)
    ;       | (right[row*2] << 8) | right[row*2+1]
    movzx eax, byte [esi]      ; left byte 0
    shl eax, 24
    movzx edi, byte [esi+1]    ; left byte 1
    shl edi, 16
    or eax, edi
    movzx edi, byte [ebx]      ; right byte 0
    shl edi, 8
    or eax, edi
    movzx edi, byte [ebx+1]    ; right byte 1
    or eax, edi

    mov [edx], eax              ; BB に書き込み

    ; 次の行: src += TILE_PITCH(2), dst += bb_pitch
    add esi, 2
    add ebx, 2
    add edx, [ebp+16]          ; + bb_pitch

    inc ecx
    jmp .row_loop

.next_plane:
    pop ecx                 ; plane 復帰
    mov edi, [ebp+8]        ; bb_planes 復帰
    inc ecx
    jmp .plane_loop

.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_tile_pair_masked(
;     u8 **bb_planes,     [ebp+8]
;     int  dst_off,       [ebp+12]  初期バイトオフセット
;     int  bb_pitch,      [ebp+16]  BBピッチ (80)
;     const u8 *tile_l,   [ebp+20]  左タイル planes[0] 先頭
;     const u8 *tile_r,   [ebp+24]  右タイル planes[0] 先頭
;     int  tile_psz,      [ebp+28]  TILE_PLANE_SZ (32)
;     u8  *cov_pair);     [ebp+32]  カバレッジ 4bytes/row × 16行 = 64bytes
;
; 2タイルペア (32px = 4bytes/row) のカバレッジマスク付きDWORD転送。
; 行ごとに4プレーンをアンロール処理する。
;
; 各行の処理:
;   cov32 = cov_pair[row*4]
;   uncov32 = ~cov32
;   any32 = 0
;   4プレーンそれぞれ:
;     src32 = pack(tile_l, tile_r)  ← DWORD
;     any32 |= src32
;     dst32 = (dst32 & cov32) | (src32 & uncov32)
;   cov_pair[row*4] |= any32
; =========================================================================
asm_tile_pair_masked:
    push ebp
    mov ebp, esp
    sub esp, 20             ; ローカル変数
    push ebx
    push esi
    push edi

    ; ローカル変数レイアウト:
    ; [ebp-4]  = cov32
    ; [ebp-8]  = uncov32
    ; [ebp-12] = any32
    ; [ebp-16] = current_dst_off
    ; [ebp-20] = row counter

    mov eax, [ebp+12]
    mov [ebp-16], eax       ; current_dst_off = dst_off
    mov dword [ebp-20], 0   ; row = 0

.pm_row_loop:
    cmp dword [ebp-20], 16
    jge .pm_done

    ; cov32 をロード
    mov edi, [ebp+32]       ; cov_pair
    mov eax, [ebp-20]       ; row
    mov ecx, [edi + eax*4]  ; cov32 = cov_pair[row*4]
    mov [ebp-4], ecx        ; save cov32

    ; early-out: 完全カバー済み
    cmp ecx, 0xFFFFFFFF
    je .pm_row_next

    not ecx
    mov [ebp-8], ecx        ; uncov32 = ~cov32
    mov dword [ebp-12], 0   ; any32 = 0

    ; タイルデータポインタ計算: tile + row * 2
    mov eax, [ebp-20]
    shl eax, 1              ; row * 2
    mov esi, [ebp+20]
    add esi, eax            ; esi = tile_l + row*2
    mov ebx, [ebp+24]
    add ebx, eax            ; ebx = tile_r + row*2

    ; ===== Plane 0 =====
    ; src32 パック
    movzx eax, byte [esi]
    shl eax, 24
    movzx ecx, byte [esi+1]
    shl ecx, 16
    or eax, ecx
    movzx ecx, byte [ebx]
    shl ecx, 8
    or eax, ecx
    movzx ecx, byte [ebx+1]
    or eax, ecx
    ; eax = src32
    or [ebp-12], eax        ; any32 |= src32
    mov ecx, [ebp-8]
    and eax, ecx            ; src32 & uncov32
    mov edi, [ebp+8]        ; bb_planes
    mov edx, [edi]          ; bb_planes[0]
    add edx, [ebp-16]       ; + dst_off
    mov ecx, [edx]          ; dst32
    and ecx, [ebp-4]        ; dst32 & cov32
    or ecx, eax
    mov [edx], ecx

    ; ===== Plane 1 =====
    mov eax, [ebp+28]       ; tile_psz
    add esi, eax
    add ebx, eax
    movzx eax, byte [esi]
    shl eax, 24
    movzx ecx, byte [esi+1]
    shl ecx, 16
    or eax, ecx
    movzx ecx, byte [ebx]
    shl ecx, 8
    or eax, ecx
    movzx ecx, byte [ebx+1]
    or eax, ecx
    or [ebp-12], eax
    mov ecx, [ebp-8]
    and eax, ecx
    mov edi, [ebp+8]
    mov edx, [edi+4]        ; bb_planes[1]
    add edx, [ebp-16]
    mov ecx, [edx]
    and ecx, [ebp-4]
    or ecx, eax
    mov [edx], ecx

    ; ===== Plane 2 =====
    mov eax, [ebp+28]
    add esi, eax
    add ebx, eax
    movzx eax, byte [esi]
    shl eax, 24
    movzx ecx, byte [esi+1]
    shl ecx, 16
    or eax, ecx
    movzx ecx, byte [ebx]
    shl ecx, 8
    or eax, ecx
    movzx ecx, byte [ebx+1]
    or eax, ecx
    or [ebp-12], eax
    mov ecx, [ebp-8]
    and eax, ecx
    mov edi, [ebp+8]
    mov edx, [edi+8]        ; bb_planes[2]
    add edx, [ebp-16]
    mov ecx, [edx]
    and ecx, [ebp-4]
    or ecx, eax
    mov [edx], ecx

    ; ===== Plane 3 =====
    mov eax, [ebp+28]
    add esi, eax
    add ebx, eax
    movzx eax, byte [esi]
    shl eax, 24
    movzx ecx, byte [esi+1]
    shl ecx, 16
    or eax, ecx
    movzx ecx, byte [ebx]
    shl ecx, 8
    or eax, ecx
    movzx ecx, byte [ebx+1]
    or eax, ecx
    or [ebp-12], eax
    mov ecx, [ebp-8]
    and eax, ecx
    mov edi, [ebp+8]
    mov edx, [edi+12]       ; bb_planes[3]
    add edx, [ebp-16]
    mov ecx, [edx]
    and ecx, [ebp-4]
    or ecx, eax
    mov [edx], ecx

    ; ===== カバレッジ更新 =====
    mov edi, [ebp+32]       ; cov_pair
    mov eax, [ebp-20]       ; row
    mov ecx, [ebp-12]       ; any32
    or [edi + eax*4], ecx   ; cov_pair[row] |= any32

.pm_row_next:
    mov eax, [ebp+16]       ; bb_pitch
    add [ebp-16], eax       ; current_dst_off += bb_pitch
    inc dword [ebp-20]      ; row++
    jmp .pm_row_loop

.pm_done:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_draw_column_tiles(
;     u8 **bb_planes,       [ebp+8]   BB プレーンポインタ配列 (4要素)
;     int  bb_pitch,        [ebp+12]  BBピッチ (80)
;     int  dst_byte_x,      [ebp+16]  描画先Xバイトオフセット
;     int  dst_y_start,     [ebp+20]  描画先Y開始行 (ピクセル)
;     const u8 **tile_ptrs, [ebp+24]  タイルデータポインタ配列 (tile_count個)
;     int  tile_psz,        [ebp+28]  TILE_PLANE_SZ (32)
;     int  tile_count       [ebp+32]  タイル数 (≤24)
; );
;
; 1列分の不透明タイルを BB に直接ストリーム描画する。
; 各タイルは 16行×2バイト/行×4プレーン。
; タイル間で dst ポインタを 16*pitch 進めるインクリメンタル方式。
; =========================================================================
global asm_draw_column_tiles
asm_draw_column_tiles:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    ; tile_count チェック
    mov eax, [ebp+32]
    test eax, eax
    jle .dct_done

    ; プレーンループ (4プレーン)
    xor ecx, ecx           ; ecx = plane_idx (0-3)
.dct_plane:
    cmp ecx, 4
    jge .dct_done

    ; dst_base = bb_planes[plane_idx] + dst_y_start * bb_pitch + dst_byte_x
    mov eax, [ebp+8]
    mov edx, [eax + ecx*4]     ; bb_planes[plane_idx]
    mov eax, [ebp+20]          ; dst_y_start
    imul eax, [ebp+12]         ; * bb_pitch
    add eax, [ebp+16]          ; + dst_byte_x
    add edx, eax               ; edx = dst_base
    push edx                   ; [esp] = dst_ptr (タイル先頭)

    ; plane_offset = plane_idx * tile_psz
    mov eax, ecx
    imul eax, [ebp+28]
    push eax                   ; [esp] = plane_offset

    push ecx                   ; [esp] = plane_idx 退避

    ; タイルループ
    mov ebx, [ebp+24]          ; tile_ptrs 配列先頭
    xor ecx, ecx               ; tile_idx = 0
.dct_tile:
    cmp ecx, [ebp+32]          ; < tile_count ?
    jge .dct_next_plane

    ; esi = tile_ptrs[tile_idx] + plane_offset
    mov esi, [ebx + ecx*4]     ; tile_ptrs[tile_idx]
    add esi, [esp+4]           ; + plane_offset

    ; edi = dst_ptr
    mov edi, [esp+8]           ; dst_ptr

    push ecx                   ; tile_idx 退避

    ; 16行ループ: 各行 WORD (2バイト) をコピー
    mov ecx, 16
.dct_row:
    mov ax, [esi]              ; タイル行データ (2バイト)
    mov [edi], ax              ; BB に書き込み
    add esi, 2                 ; タイルデータ次行
    add edi, [ebp+12]          ; dst += bb_pitch
    dec ecx
    jnz .dct_row

    ; dst_ptr を次のタイル位置に更新 (edi は既に 16行分進んだ)
    mov [esp+12], edi          ; dst_ptr = edi (次タイル先頭)

    pop ecx                    ; tile_idx 復帰
    inc ecx
    jmp .dct_tile

.dct_next_plane:
    pop ecx                    ; plane_idx 復帰
    add esp, 8                 ; plane_offset, dst_ptr 破棄
    inc ecx
    jmp .dct_plane

.dct_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_draw_row_tiles(
;     u8 **bb_planes,       [ebp+8]   BB プレーンポインタ配列 (4要素)
;     int  bb_pitch,        [ebp+12]  BBピッチ (80)
;     int  dst_byte_x_start,[ebp+16]  描画先X開始バイトオフセット
;     int  dst_y,           [ebp+20]  描画先Y行 (ピクセル)
;     const u8 **tile_ptrs, [ebp+24]  タイルデータポインタ配列 (tile_count個)
;     int  tile_psz,        [ebp+28]  TILE_PLANE_SZ (32)
;     int  tile_count       [ebp+32]  タイル数 (≤24)
; );
;
; 1行分の不透明タイルを BB に直接ストリーム描画する。
; 各タイルは同じ Y 位置、X は 2バイト (16px) ずつ進む。
; =========================================================================
global asm_draw_row_tiles
asm_draw_row_tiles:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [ebp+32]
    test eax, eax
    jle .drt_done

    ; プレーンループ (4プレーン)
    xor ecx, ecx
.drt_plane:
    cmp ecx, 4
    jge .drt_done

    ; row_base = bb_planes[plane_idx] + dst_y * bb_pitch + dst_byte_x_start
    mov eax, [ebp+8]
    mov edx, [eax + ecx*4]
    mov eax, [ebp+20]
    imul eax, [ebp+12]
    add eax, [ebp+16]
    add edx, eax               ; edx = row_base

    ; plane_offset = plane_idx * tile_psz
    mov eax, ecx
    imul eax, [ebp+28]
    push eax                   ; [esp] = plane_offset
    push ecx                   ; [esp] = plane_idx 退避

    ; タイルループ
    mov ebx, [ebp+24]
    xor ecx, ecx
    mov edi, edx               ; edi = 現在タイルの BB 開始位置
.drt_tile:
    cmp ecx, [ebp+32]
    jge .drt_next_plane

    mov esi, [ebx + ecx*4]     ; tile_ptrs[tile_idx]
    add esi, [esp+4]           ; + plane_offset

    push ecx                   ; tile_idx 退避
    push edi                   ; dst_col_base 退避

    ; 16行ループ
    mov ecx, 16
.drt_row:
    mov ax, [esi]
    mov [edi], ax
    add esi, 2
    add edi, [ebp+12]          ; dst += bb_pitch
    dec ecx
    jnz .drt_row

    pop edi                    ; dst_col_base 復帰
    add edi, 2                 ; 次のタイル列 (+2 bytes = +16px)
    pop ecx                    ; tile_idx 復帰
    inc ecx
    jmp .drt_tile

.drt_next_plane:
    pop ecx                    ; plane_idx 復帰
    add esp, 4                 ; plane_offset 破棄
    inc ecx
    jmp .drt_plane

.drt_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_bb_shift_horiz(
;     u8 *planes[4],  [ebp+8]   4プレーンポインタ配列
;     int pitch,      [ebp+12]  BBピッチ (80)
;     int origin_y,   [ebp+16]  タイル領域開始Y
;     int tile_h,     [ebp+20]  タイル領域高さ (384)
;     int base_byte,  [ebp+24]  水平開始オフセット
;     int tile_bytes, [ebp+28]  水平幅バイト数 (48)
;     int shift_bytes [ebp+32]  シフト量 (正=左シフト, 負=右シフト)
; );
;
; インクリメンタルポインタ方式: line += pitch で行を進める (imul 排除)。
; =========================================================================
asm_bb_shift_horiz:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 16
    ; [ebp-16] = abs_shift
    ; [ebp-20] = copy_size
    ; [ebp-24] = plane_idx
    ; [ebp-28] = line_ptr (現在行ポインタ)

    ; abs_shift = |shift_bytes|
    mov eax, [ebp+32]
    cdq
    xor eax, edx
    sub eax, edx
    mov [ebp-16], eax

    ; copy_size = tile_bytes - abs_shift
    mov ecx, [ebp+28]
    sub ecx, eax
    jle .hshift_done
    mov [ebp-20], ecx

    cmp dword [ebp+32], 0
    jg .hshift_left
    jmp .hshift_right

; ---- 左シフト (shift_bytes > 0) ----
.hshift_left:
    mov dword [ebp-24], 0
.hl_plane:
    cmp dword [ebp-24], 4
    jge .hshift_done

    ; line_ptr = planes[p] + origin_y * pitch + base_byte
    mov eax, [ebp+8]
    mov edx, [ebp-24]
    mov eax, [eax + edx*4]
    mov ecx, [ebp+16]
    imul ecx, [ebp+12]
    add eax, ecx
    add eax, [ebp+24]
    mov [ebp-28], eax       ; line_ptr

    mov ebx, [ebp+20]       ; ebx = tile_h (カウンタ)
.hl_row:
    test ebx, ebx
    jz .hl_plane_next

    mov edi, [ebp-28]       ; edi = line (dest)
    mov esi, edi
    add esi, [ebp-16]       ; esi = line + abs_shift (src)

    cld
    mov ecx, [ebp-20]
    mov edx, ecx
    shr ecx, 2
    rep movsd
    mov ecx, edx
    and ecx, 3
    rep movsb

    ; ゼロクリア abs_shift バイト
    xor eax, eax
    mov ecx, [ebp-16]
    mov edx, ecx
    shr ecx, 2
    rep stosd
    mov ecx, edx
    and ecx, 3
    rep stosb

    ; line_ptr += pitch
    mov eax, [ebp+12]
    add [ebp-28], eax
    dec ebx
    jmp .hl_row

.hl_plane_next:
    inc dword [ebp-24]
    jmp .hl_plane

; ---- 右シフト (shift_bytes < 0) ----
.hshift_right:
    mov dword [ebp-24], 0
.hr_plane:
    cmp dword [ebp-24], 4
    jge .hshift_done

    mov eax, [ebp+8]
    mov edx, [ebp-24]
    mov eax, [eax + edx*4]
    mov ecx, [ebp+16]
    imul ecx, [ebp+12]
    add eax, ecx
    add eax, [ebp+24]
    mov [ebp-28], eax

    mov ebx, [ebp+20]
.hr_row:
    test ebx, ebx
    jz .hr_plane_next

    mov eax, [ebp-28]       ; eax = line

    ; 後ろ向きコピー: src=line[0..copy_size-1] → dst=line[abs_shift..abs_shift+copy_size-1]
    mov ecx, [ebp-20]       ; copy_size
    mov edx, ecx
    and edx, 3              ; trailing bytes

    std

    test edx, edx
    jz .hr_no_trail
    ; esi = line + copy_size - 1, edi = line + abs_shift + copy_size - 1
    lea esi, [eax + ecx - 1]
    mov edi, eax
    add edi, [ebp-16]
    add edi, ecx
    dec edi
    mov ecx, edx
    rep movsb
    sub esi, 3
    sub edi, 3
    jmp .hr_do_dwords
.hr_no_trail:
    lea esi, [eax + ecx - 4]
    mov edi, eax
    add edi, [ebp-16]
    add edi, [ebp-20]
    sub edi, 4
.hr_do_dwords:
    mov ecx, [ebp-20]
    shr ecx, 2
    rep movsd

    cld

    ; 先頭 abs_shift バイトをゼロクリア
    mov edi, [ebp-28]
    xor eax, eax
    mov ecx, [ebp-16]
    mov edx, ecx
    shr ecx, 2
    rep stosd
    mov ecx, edx
    and ecx, 3
    rep stosb

    ; line_ptr += pitch
    mov eax, [ebp+12]
    add [ebp-28], eax
    dec ebx
    jmp .hr_row

.hr_plane_next:
    inc dword [ebp-24]
    jmp .hr_plane

.hshift_done:
    add esp, 16
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret


; =========================================================================
; void __cdecl asm_bb_shift_vert(
;     u8 *planes[4],  [ebp+8]
;     int pitch,      [ebp+12]
;     int origin_y,   [ebp+16]
;     int tile_h,     [ebp+20]
;     int base_byte,  [ebp+24]
;     int tile_bytes, [ebp+28]  (48 = 12 DWORD)
;     int shift_lines [ebp+32]  (正=上シフト, 負=下シフト)
; );
;
; インクリメンタルポインタ方式: dst += pitch, src += pitch で行を進める。
; =========================================================================
asm_bb_shift_vert:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 20
    ; [ebp-16] = abs_shift
    ; [ebp-20] = plane_idx
    ; [ebp-24] = dword_count
    ; [ebp-28] = dst_ptr
    ; [ebp-32] = src_ptr

    ; abs_shift = |shift_lines|
    mov eax, [ebp+32]
    cdq
    xor eax, edx
    sub eax, edx
    mov [ebp-16], eax

    cmp eax, [ebp+20]
    jge .vshift_done

    mov eax, [ebp+28]
    shr eax, 2
    mov [ebp-24], eax       ; dword_count

    cmp dword [ebp+32], 0
    jg .vshift_up
    jmp .vshift_down

; ---- 上シフト (shift_lines > 0) ----
.vshift_up:
    mov dword [ebp-20], 0
.vu_plane:
    cmp dword [ebp-20], 4
    jge .vshift_done

    ; area_base = planes[p] + origin_y * pitch + base_byte
    mov eax, [ebp+8]
    mov edx, [ebp-20]
    mov eax, [eax + edx*4]
    mov ecx, [ebp+16]
    imul ecx, [ebp+12]
    add eax, ecx
    add eax, [ebp+24]       ; eax = area_base

    ; dst = area_base (row 0)
    mov [ebp-28], eax
    ; src = area_base + shift_lines * pitch
    mov ecx, [ebp-16]
    imul ecx, [ebp+12]
    add eax, ecx
    mov [ebp-32], eax

    ; コピー copy_rows = tile_h - abs_shift
    mov ebx, [ebp+20]
    sub ebx, [ebp-16]
.vu_copy:
    test ebx, ebx
    jz .vu_zero

    mov edi, [ebp-28]
    mov esi, [ebp-32]
    cld
    mov ecx, [ebp-24]
    rep movsd
    mov ecx, [ebp+28]
    and ecx, 3
    rep movsb

    mov eax, [ebp+12]
    add [ebp-28], eax
    add [ebp-32], eax
    dec ebx
    jmp .vu_copy

.vu_zero:
    ; ゼロクリア abs_shift 行
    mov ebx, [ebp-16]
.vu_zero_row:
    test ebx, ebx
    jz .vu_plane_next

    mov edi, [ebp-28]
    xor eax, eax
    mov ecx, [ebp-24]
    rep stosd
    mov ecx, [ebp+28]
    and ecx, 3
    rep stosb

    mov eax, [ebp+12]
    add [ebp-28], eax
    dec ebx
    jmp .vu_zero_row

.vu_plane_next:
    inc dword [ebp-20]
    jmp .vu_plane

; ---- 下シフト (shift_lines < 0): 逆順コピー ----
.vshift_down:
    mov dword [ebp-20], 0
.vd_plane:
    cmp dword [ebp-20], 4
    jge .vshift_done

    mov eax, [ebp+8]
    mov edx, [ebp-20]
    mov eax, [eax + edx*4]
    mov ecx, [ebp+16]
    imul ecx, [ebp+12]
    add eax, ecx
    add eax, [ebp+24]       ; eax = area_base

    ; dst = area_base + (tile_h - 1) * pitch
    mov ecx, [ebp+20]
    dec ecx
    imul ecx, [ebp+12]
    lea edx, [eax + ecx]
    mov [ebp-28], edx       ; dst = 最終行

    ; src = dst - abs_shift * pitch
    mov ecx, [ebp-16]
    imul ecx, [ebp+12]
    sub edx, ecx
    mov [ebp-32], edx       ; src = dst - shift*pitch

    ; コピー copy_rows = tile_h - abs_shift (逆順)
    mov ebx, [ebp+20]
    sub ebx, [ebp-16]
.vd_copy:
    test ebx, ebx
    jz .vd_zero

    mov edi, [ebp-28]
    mov esi, [ebp-32]
    cld
    mov ecx, [ebp-24]
    rep movsd
    mov ecx, [ebp+28]
    and ecx, 3
    rep movsb

    ; dst -= pitch, src -= pitch
    mov eax, [ebp+12]
    sub [ebp-28], eax
    sub [ebp-32], eax
    dec ebx
    jmp .vd_copy

.vd_zero:
    ; ゼロクリア abs_shift 行 (先頭行から — dst は既に正しい位置)
    ; ebp-28 は abs_shift-1 行を指している
    mov ebx, [ebp-16]
.vd_zero_row:
    test ebx, ebx
    jz .vd_plane_next

    mov edi, [ebp-28]
    xor eax, eax
    mov ecx, [ebp-24]
    rep stosd
    mov ecx, [ebp+28]
    and ecx, 3
    rep stosb

    mov eax, [ebp+12]
    sub [ebp-28], eax
    dec ebx
    jmp .vd_zero_row

.vd_plane_next:
    inc dword [ebp-20]
    jmp .vd_plane

.vshift_done:
    add esp, 20
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret




