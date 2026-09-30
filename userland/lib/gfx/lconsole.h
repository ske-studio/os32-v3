/*
 * lconsole.h - OS32 Logical Console (差分仮想VRAM)
 * C89 compatible
 */
#ifndef OS32_LCONSOLE_H
#define OS32_LCONSOLE_H

#include "os32api.h"

/* コンソールサイズ */
#define LCONS_W 80
#define LCONS_H 25

/* ====== 初期化とクリア ====== */
void lcons_init(void);
void lcons_clear(void);

/* ====== セル操作 ====== */
void lcons_putc(int x, int y, char ch, unsigned char attr);
void lcons_putkanji(int x, int y, unsigned short jis, unsigned char attr);
void lcons_fill_rect(int start_x, int start_y, int w, int h, char ch, unsigned char attr);
void lcons_clear_line(int y, unsigned char attr);

/* ====== 文字列描画 ====== */
int lcons_put_string(int x, int y, const char *s, unsigned char attr);
int lcons_put_utf8_string(int x, int y, const char *utf8_str, unsigned char attr);
int lcons_put_utf8_char(int x, int y, void *dec, unsigned char attr);
int lcons_put_int(int x, int y, int val, int width, int right_align, char pad_char, unsigned char attr);
int lcons_int_to_str(int val, char *buf);

/* ====== 属性→色変換テーブル ====== */
typedef struct {
    unsigned char fg;  /* 前景色 (0-15) */
    unsigned char bg;  /* 背景色 (0-15, 0xFF=透過) */
} LConsAttrMap;

void lcons_set_attr_map(unsigned char attr, unsigned char fg, unsigned char bg);
void lcons_reset_attr_map(void);

/* ====== カーソル ====== */
void lcons_set_cursor(int x, int y);
void lcons_show_cursor(int visible);

/* ====== ポップアップ描画 ====== */
void lcons_draw_box(int x, int y, int w, int h, unsigned char attr);

/* ====== 物理VRAM(KCG)への差分同期 ====== */
void lcons_sync_vram(void);

#endif /* OS32_LCONSOLE_H */
