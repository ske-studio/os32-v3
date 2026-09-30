; =========================================================================
; gfx_const.inc — libos32gfx 共有定数 (PC-98 640x400 4プレーン)
; =========================================================================
; 各ASMファイルから %include "gfx_const.inc" で参照する。

%ifndef GFX_CONST_INC
%define GFX_CONST_INC

%define GFX_WIDTH    640        ; 画面幅 (ピクセル)
%define GFX_HEIGHT   400        ; 画面高 (ライン)
%define GFX_BPL      80         ; 1ラインあたりのバイト数 (640/8)
%define GFX_PLANE_SZ 32000      ; 1プレーンのサイズ (80*400)

%endif
