/* One READ/WRITE command, with one 16-bit PIO transfer per DRQ block.
 * IDENTIFY keeps its separate wait/transfer path in ide.c. */
static void ide_pio_settle(void)
{
    int i;
    for (i = 0; i < IDE_SEL_SETTLE; i++) inp(IDE_ALT_STATUS);
}

/* BSY masks all other bits. Return the stable status so READ can drain an
 * error-associated DRQ without publishing that sector to the caller. */
static int ide_pio_wait_phase(u8 *status)
{
    int timeout = IDE_TIMEOUT_LOOP;
    while (timeout-- > 0) {
        u8 st = (u8)inp(IDE_ALT_STATUS);
        if (st & IDE_ST_BSY) continue;
        *status = st;
        if (st & (IDE_ST_ERR | IDE_ST_DF)) return IDE_ERR_IO;
        if (st & IDE_ST_DRQ) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

static int ide_pio_finish(void)
{
    int timeout = IDE_TIMEOUT_BSY;
    while (timeout-- > 0) {
        /* Regular status also acknowledges the command's interrupt. */
        u8 st = (u8)inp(IDE_STATUS);
        if (st & IDE_ST_BSY) continue;
        if (st & (IDE_ST_ERR | IDE_ST_DF)) return IDE_ERR_IO;
        if (!(st & IDE_ST_DRQ)) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

static int ide_pio_command(int drive, const IdeAddr *a, u32 sectors,
                           void *read_buf, const void *write_buf)
{
    u16 *dst = (u16 *)read_buf;
    const u16 *src = (const u16 *)write_buf;
    u32 done, multiple;
    int ret;

    if (!drive_present[drive]) return IDE_ERR_NO_DRIVE;
    ide_select_drive(drive);
    ret = ide_wait_bsy();
    if (ret != IDE_OK) return ret;
    ret = ide_multiple_prepare(drive);
    if (ret != IDE_OK) return ret;
    multiple = drive_multiple[drive].active;
    if (multiple <= 1) multiple = 1;
    /* sectors is 1..256 here; only the register encodes 256 as zero. */
    ide_set_addr(a, (u8)(sectors == IDE_COMMAND_MAX_SECTORS ? 0 : sectors));
    outp(IDE_COMMAND, multiple > 1
         ? (src ? IDE_CMD_WRITE_MULTIPLE : IDE_CMD_READ_MULTIPLE)
         : (src ? IDE_CMD_WRITE : IDE_CMD_READ));
    ide_pio_settle();

    for (done = 0; done < sectors;) {
        u32 n = sectors - done < multiple ? sectors - done : multiple;
        u32 phase_words = n * IDE_PIO_WORDS;
        u8 st = 0;
        u32 w;
        ret = ide_pio_wait_phase(&st);
        if (ret != IDE_OK) {
            if (!src && ret == IDE_ERR_IO && (st & IDE_ST_ERR) && (st & IDE_ST_DRQ)) {
                /* ATA/ATAPI-6 §8.30.8: discard the entire failed DRQ block. */
                for (w = 0; w < phase_words; w++) (void)inpw(IDE_DATA);
                ide_pio_settle();
            }
            return ret;
        }
        if (src) {
            for (w = 0; w < phase_words; w++) outpw(IDE_DATA, (unsigned)*src++);
        } else {
            for (w = 0; w < phase_words; w++) *dst++ = (u16)inpw(IDE_DATA);
        }
        done += n;
        /* Let the previous DRQ phase retire before sampling the next status. */
        ide_pio_settle();
    }
    return ide_pio_finish();
}
