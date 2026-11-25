/* One READ/WRITE command, with one 16-bit PIO transfer per DRQ block.
 * IDENTIFY keeps its separate wait/transfer path in ide.c. */
static void ide_pio_settle(void)
{
    int i;
    for (i = 0; i < IDE_SEL_SETTLE; i++) ide_bus_alt();
}

/* BSY masks all other bits. Return the stable status so READ can drain an
 * error-associated DRQ without publishing that sector to the caller. */
static int ide_pio_wait_phase(u8 *status)
{
    IdeWait wait = ide_wait_start(IDE_EARLY_PHASE_POLLS);
    while (ide_wait_pending(&wait, IDE_IO_TIMEOUT_MS)) {
        u8 st = ide_bus_alt();
        if (st & IDE_ST_BSY) continue;
        *status = st;
        if (st & (IDE_ST_ERR | IDE_ST_DF)) return IDE_ERR_IO;
        if (st & IDE_ST_DRQ) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

static int ide_pio_finish(u32 limit_ms)
{
    IdeWait wait = ide_wait_start(IDE_EARLY_BSY_POLLS);
    while (ide_wait_pending(&wait, limit_ms)) {
        /* Regular status also acknowledges the command's interrupt. */
        u8 st = ide_bus_in(IDE_REG_STATUS);
        if (st & IDE_ST_BSY) continue;
        if (st & (IDE_ST_ERR | IDE_ST_DF)) return IDE_ERR_IO;
        if (!(st & IDE_ST_DRQ)) return IDE_OK;
    }
    return IDE_ERR_TIMEOUT;
}

static int ide_pio_command(int drive, const IdeAddr *a, u32 sectors,
                           void *read_buf, const void *write_buf)
{
    u8 *dst = read_buf;
    const u8 *src = write_buf;
    u32 done, multiple;
    int ret;

    if (!drive_present[drive]) return IDE_ERR_NO_DRIVE;
    ide_select_drive(drive);
    ret = ide_wait_bsy(IDE_IO_TIMEOUT_MS);
    if (ret != IDE_OK) return ret;
    ret = ide_multiple_prepare(drive, IDE_IO_TIMEOUT_MS);
    if (ret != IDE_OK) return ret;
    multiple = drive_multiple[drive].active;
    if (multiple <= 1) multiple = 1;
    /* sectors is 1..256 here; only the register encodes 256 as zero. */
    ide_set_addr(a, (u8)(sectors == IDE_COMMAND_MAX_SECTORS ? 0 : sectors));
    ide_bus_out(IDE_REG_STATUS, multiple > 1
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
                for (w = 0; w < phase_words; w++) (void)ide_bus_data_word();
                ide_pio_settle();
            }
            return ret;
        }
        if (src) {
            ide_bus_data_write(src, phase_words);
            src += 2 * phase_words;
        } else {
            ide_bus_data_read(dst, phase_words);
            dst += 2 * phase_words;
        }
        done += n;
        /* Let the previous DRQ phase retire before sampling the next status. */
        ide_pio_settle();
    }
    return ide_pio_finish(IDE_IO_TIMEOUT_MS);
}
