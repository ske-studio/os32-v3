/* ======================================================================== */
/*  KCG.C — 漢字キャラクタジェネレータ (KCG) ドライバ                       */
/*                                                                          */
/*  PC-98内蔵フォントROMから文字パターンを読み出してグラフィック描画          */
/*                                                                          */
/*  I/Oポートアクセス方式:                                                   */
/*    1. A1h ← JISコード下位バイト                                          */
/*    2. A3h ← JISコード上位バイト - 0x20                                   */
/*    3. A5h ← ライン番号 (0-15) + L/Rビット                                */
/*    4. A9h → パターンデータ (1バイト = 8ピクセル分)                         */
/*                                                                          */
/*  ANK文字:                                                                 */
/*    PC-98ではANK文字はJIS行0x29に配置 (半角漢字扱い)                       */
/*    例: ASCII 'A' (0x41) → JISコード 0x2941                               */
/*    ポート A3h ← 0x29-0x20 = 0x09                                         */
/*    出典: PC9800Bible §2-6-1 「半角漢字の場合」                            */
/*                                                                          */
/*  漢字: JIS上位=0x21-0x7E, 16x16ドット (左右各8ドット)                    */
/*                                                                          */
/*  Shift-JIS → JIS変換も内蔵                                               */
/*                                                                          */
/*  出典: PC9800Bible §2-6-5                                                */
/* ======================================================================== */

#include "kcg.h"
#include "../kernel/kcg_module.h"
#include "os32_kapi_shared.h"

#define KCG_SCALE_MAX 4

int kcg_scale = 1;

void kcg_init(void)
{
    kcg_scale = 1;
}

void kcg_set_scale(int scale)
{
    if (scale < 1) scale = 1;
    if (scale > KCG_SCALE_MAX) scale = KCG_SCALE_MAX;
    kcg_scale = scale;
}

void kcg_read_ank(u8 ch, u8 *buf)
{
    kcg_rom_read_ank(ch, buf);
}

void kcg_read_kanji(u16 jis_code, u8 *buf)
{
    kcg_rom_read_kanji(jis_code, buf);
}

/* Retained KAPI slot: no file access or low-memory writes, even during boot. */
int kcg_load_font(const char *path)
{
    (void)path;
    return OS32_ERR_NOSYS;
}
