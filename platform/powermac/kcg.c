#include "drivers/video/kcg/kcg_module.h"
#include "kernel/console/font_source_kcg.h"

void kcg_rom_read_ank(u8 ch, u8 *buf)
{
    kcg_file_api.read_ank(ch, buf);
}

void kcg_rom_read_kanji(u16 jis, u8 *buf)
{
    kcg_file_api.read_kanji(jis, buf);
}
