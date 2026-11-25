/* 単発 READ/WRITE DATA の共通コマンド列。失敗したバイトで送出を止める。 */
static int fdc_send_sector_command(u8 command, int drv, int cyl, int head,
                                   int sect, const struct fdc_geom *g)
{
    fdc_irq_fired = 0;
    if (fdc_send_byte(FDC_OPT_MF | command) != 0) return -1;
    if (fdc_send_byte((u8)((head << FDC_HEAD_SHIFT) | drv)) != 0) return -1;
    if (fdc_send_byte((u8)cyl) != 0) return -1;       /* C */
    if (fdc_send_byte((u8)head) != 0) return -1;      /* H */
    if (fdc_send_byte((u8)sect) != 0) return -1;      /* R (1 始まり) */
    if (fdc_send_byte(g->sec_n) != 0) return -1;      /* N */
    if (fdc_send_byte((u8)sect) != 0) return -1;      /* EOT = R */
    if (fdc_send_byte(g->gap3) != 0) return -1;       /* GPL */
    if (fdc_send_byte(FDC_DTL_UNUSED) != 0) return -1; /* DTL */
    return 0;
}
