/* The backend selects its current provider for each public read. */
void kcg_rom_read_ank(u8 ch, u8 *buf)
{
    public_api()->read_ank(ch, buf);
}

void kcg_rom_read_kanji(u16 jis, u8 *buf)
{
    public_api()->read_kanji(jis, buf);
}
