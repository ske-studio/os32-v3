/* ATA/ATAPI-6 word 47/59 and SET MULTIPLE MODE; private to ide.c. */
static struct ide_multiple_state {
    u8 max;
    u8 active;
    u8 pending;
} drive_multiple[IDE_MAX_DRIVES];

static void ide_multiple_init(void)
{
    int drive;
    for (drive = 0; drive < IDE_MAX_DRIVES; drive++) {
        drive_multiple[drive].max = 0;
        drive_multiple[drive].active = 0;
        drive_multiple[drive].pending = 0;
    }
}

/* Public IDENTIFY observes only; a failed IDENTIFY never reaches here. */
static void ide_multiple_identify(int drive, const u16 *words, u16 sector_size)
{
    u8 max = sector_size == SYS_HDD_SECTOR_SIZE
             ? (u8)(words[IDE_ID_MULTI_MAX_WORD] & IDE_ID_MULTI_COUNT_MASK) : 0;
    u16 current = words[IDE_ID_MULTI_CURRENT_WORD];

    drive_multiple[drive].max = max;
    if (max <= 1) {
        drive_multiple[drive].active = 0;
        drive_multiple[drive].pending = 0;
    } else if (!drive_multiple[drive].active ||
               !(current & IDE_ID_MULTI_VALID) ||
               (current & IDE_ID_MULTI_COUNT_MASK) != drive_multiple[drive].active ||
               drive_multiple[drive].active > max) {
        drive_multiple[drive].active = 0;
        drive_multiple[drive].pending = 1;
    }
}

/* Called before SRST is asserted, including a reset that later times out. */
void ide_multiple_reset_bank(int bank)
{
    int drv;
    for (drv = 0; drv < IDE_DRIVES_PER_BANK; drv++) {
        int drive = bank * IDE_DRIVES_PER_BANK + drv;
        drive_multiple[drive].active = 0;
        drive_multiple[drive].pending = drive_multiple[drive].max > 1;
    }
}

/* Drive is already selected and its bounded BSY wait has completed. */
static int ide_multiple_prepare(int drive)
{
    u8 st;
    int ret;

    if (drive_multiple[drive].max <= 1) return IDE_OK;
    /* A failed C6h may still own the bus on a subsequent request. */
    st = (u8)inp(IDE_ALT_STATUS);
    if (st & IDE_ST_BSY) return IDE_ERR_TIMEOUT;
    if (st & IDE_ST_DRQ) return IDE_ERR_IO;
    if (!drive_multiple[drive].pending) return IDE_OK;

    drive_multiple[drive].active = 0;
    drive_multiple[drive].pending = 0;
    outp(IDE_SECT_CNT, drive_multiple[drive].max);
    outp(IDE_COMMAND, IDE_CMD_SET_MULTIPLE);
    ide_pio_settle();
    ret = ide_pio_finish();
    if (ret == IDE_OK) {
        drive_multiple[drive].active = drive_multiple[drive].max;
        return IDE_OK;
    }
    /* Clean rejection falls back once. Never overlay a still-active command. */
    st = (u8)inp(IDE_ALT_STATUS);
    if (st & (IDE_ST_BSY | IDE_ST_DRQ)) return ret;
    return IDE_OK;
}
