#ifndef BOOT_FONT_H
#define BOOT_FONT_H

/* 起動時の既定フォント読み込み (kernel/boot_font.c)。戻り値は kcg_load_font と同じ
 * (0 = 成功、負 = 失敗)。 */
int boot_font_load(void);

#endif /* BOOT_FONT_H */
