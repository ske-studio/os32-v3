/* Product IDE driver; only the port boundary is replaced. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ide.h"
#include "ide_addr.h"
#include "kprintf.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define MAX_SECTORS 257u
#define LOG_CAP 300
static u16 identify_words[IDE_IDENTIFY_WORDS];
static unsigned bank, head, sect, cyl_lo, cyl_hi, count_reg;
static unsigned identifying, id_word, active, writing, phase, word, chunk;
static unsigned settle, busy, ready, commands, phases, words, addr_writes;
static u32 start_lba, seed;
static unsigned log_count[LOG_CAP], log_reg[LOG_CAP];
static u32 log_lba[LOG_CAP];
enum { NORMAL, STUCK_BSY, NO_DRQ, ERROR, FAULT, ERROR_DRQ, END_DRQ };
static unsigned fault_kind, fault_phase, fault_command, pre_busy;
static unsigned strict_timing;
/* The device owns the DRQ block size; phase still counts consumed sectors. */
static unsigned identify_error;
static unsigned individual_caps, advertised_per_drive[IDE_MAX_DRIVES];
static unsigned advertised_multiple, present_mask = 1, identify_fail;
static unsigned device_multiple[IDE_MAX_DRIVES], setup_count[IDE_MAX_DRIVES];
static unsigned setting, setting_busy, setup_fault, block_sectors = 1;
static unsigned log_opcode[LOG_CAP];
static unsigned char written_sectors[MAX_SECTORS];
#define TEST_READ_MULTIPLE 0xC4
#define TEST_WRITE_MULTIPLE 0xC5
#define TEST_SET_MULTIPLE 0xC6
static unsigned selected(void) { return bank * 2 + !!(head & IDE_DRVHEAD_SLAVE); }
static unsigned advertised(void) { return individual_caps ? advertised_per_drive[selected()] : advertised_multiple; }

static u16 pattern(u32 lba, unsigned w)
{
    return (u16)((lba * 193u + w * 71u + seed) ^ (w << 8));
}

void irq_disable(unsigned int irq) { (void)irq; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

unsigned int inp(unsigned int port)
{
    CHECK(port == IDE_ALT_STATUS || port == IDE_STATUS);
    if (identify_error) return IDE_ST_ERR;
    if (identifying) return IDE_ST_DRQ;
    if (setting) {
        if (setting_busy) { setting_busy--; return IDE_ST_BSY | IDE_ST_ERR | IDE_ST_DRQ; }
        if (setup_fault == STUCK_BSY) return IDE_ST_BSY;
        if (setup_fault == ERROR_DRQ) return IDE_ST_ERR | IDE_ST_DRQ;
        if (setup_fault == END_DRQ) return IDE_ST_DRQ;
        if (setup_fault == ERROR) return IDE_ST_ERR;
        if (setup_fault == FAULT) return IDE_ST_DF;
        device_multiple[selected()] = count_reg;
        setting = 0;
        return IDE_ST_DRDY;
    }
    /* No status wait/settle is allowed between sectors of one DRQ block. */
    CHECK(!active || !ready || (word == 0 && phase % block_sectors == 0));
    if (!active) return pre_busy ? IDE_ST_BSY | IDE_ST_DRQ : IDE_ST_DRDY;
    if (settle) { settle--; return IDE_ST_DRQ; } /* stale previous phase */
    if (busy) { busy--; return IDE_ST_BSY | IDE_ST_DRQ | IDE_ST_ERR | IDE_ST_DF; }
    if (commands == fault_command && phase == fault_phase) {
        if (fault_kind == STUCK_BSY) return IDE_ST_BSY | IDE_ST_DRQ;
        if (fault_kind == NO_DRQ) return IDE_ST_DRDY;
        if (fault_kind == ERROR) return IDE_ST_ERR;
        if (fault_kind == FAULT) return IDE_ST_DF | IDE_ST_DRQ;
        if (fault_kind == ERROR_DRQ) { ready = 1; return IDE_ST_ERR | IDE_ST_DRQ; }
        if (fault_kind == END_DRQ) return IDE_ST_DRQ;
    }
    if (phase == chunk) return IDE_ST_DRDY;
    ready = 1;
    return IDE_ST_DRDY | IDE_ST_DRQ;
}

void outp(unsigned int port, unsigned int value)
{
    switch (port) {
    case IDE_BANK1: bank = value; break;
    case IDE_DEV_CTRL: break;
    case IDE_DRV_HEAD: head = value; break;
    case IDE_SECT_CNT: count_reg = value; addr_writes++; break;
    case IDE_SECT_NUM: sect = value; addr_writes++; break;
    case IDE_CYL_LO: cyl_lo = value; addr_writes++; break;
    case IDE_CYL_HI: cyl_hi = value; addr_writes++; break;
    case IDE_COMMAND:
        if (value == IDE_CMD_IDENTIFY) {
            identify_error = identify_fail || !(present_mask & (1u << selected()));
            identifying = !identify_error; setting = active = 0; id_word = 0; break;
        }
        identify_error = 0;
        if (value == TEST_SET_MULTIPLE) {
            CHECK(!pre_busy && !identifying);
            CHECK(count_reg == advertised() && count_reg > 1);
            setup_count[selected()]++;
            setting = 1; setting_busy = IDE_SEL_SETTLE + 2; active = 0;
            break;
        }
        CHECK(value == IDE_CMD_READ || value == IDE_CMD_WRITE ||
              value == TEST_READ_MULTIPLE || value == TEST_WRITE_MULTIPLE);
        CHECK(!setting || (setup_fault != STUCK_BSY && setup_fault != ERROR_DRQ && setup_fault != END_DRQ));
        setting = 0;
        block_sectors = (value == TEST_READ_MULTIPLE || value == TEST_WRITE_MULTIPLE) ? device_multiple[selected()] : 1;
        CHECK(block_sectors > 0);
        CHECK(!pre_busy);
        identifying = 0;
        writing = value == IDE_CMD_WRITE || value == TEST_WRITE_MULTIPLE;
        active = 1; phase = 0; word = 0; ready = 0;
        settle = strict_timing ? IDE_SEL_SETTLE : 0; busy = strict_timing ? 2 : 0;
        chunk = count_reg ? count_reg : 256;
        if (head & IDE_DRVHEAD_LBA)
            start_lba = sect | (cyl_lo << 8) | (cyl_hi << 16) | ((u32)(head & 15) << 24);
        else {
            unsigned heads = identify_words[IDE_ID_VALID_WORD] ? identify_words[IDE_ID_CUR_HEADS_WORD] : identify_words[IDE_ID_DEF_HEADS_WORD];
            unsigned spt = identify_words[IDE_ID_VALID_WORD] ? identify_words[IDE_ID_CUR_SPT_WORD] : identify_words[IDE_ID_DEF_SPT_WORD];
            start_lba = (((cyl_lo | (cyl_hi << 8)) * heads + (head & 15)) * spt) + sect - 1;
            CHECK(chunk <= spt - (sect - 1));
        }
        CHECK(commands < LOG_CAP);
        log_opcode[commands] = value;
        log_count[commands] = chunk; log_reg[commands] = count_reg;
        log_lba[commands++] = start_lba;
        break;
    default: CHECK(0);
    }
}

static void transferred(void)
{
    CHECK(ready && !settle && !busy);
    words++;
    if (++word == IDE_PIO_WORDS) {
        if (writing) written_sectors[phase] = 1;
        word = 0; phase++;
        if (phase % block_sectors == 0 || phase == chunk) {
            phases++; ready = 0;
            settle = strict_timing ? IDE_SEL_SETTLE : 0; busy = strict_timing ? 2 : 0;
        }
    }
}

unsigned int inpw(unsigned int port)
{
    u16 value;
    CHECK(port == IDE_DATA);
    if (identifying) {
        CHECK(id_word < IDE_IDENTIFY_WORDS);
        value = identify_words[id_word];
        if (id_word == 47) value = 0x8000 | advertised();
        if (id_word == 59 && value == 0xFFFF)
            value = device_multiple[selected()] ? 0x100 | device_multiple[selected()] : 0;
        id_word++;
        if (id_word == IDE_IDENTIFY_WORDS) identifying = 0;
        return value;
    }
    CHECK(!writing && phase < chunk);
    value = pattern(start_lba + phase, word);
    transferred();
    return value;
}

void outpw(unsigned int port, unsigned int value)
{
    CHECK(port == IDE_DATA && writing && phase < chunk);
    CHECK(value == pattern(start_lba + phase, word));
    transferred();
}

static void reset_io(void)
{
    active = identifying = identify_error = setting = commands = phases = words = addr_writes = 0;
    fault_kind = NORMAL; pre_busy = 0; seed++;
    memset(written_sectors, 0, sizeof(written_sectors));
}

static void configure(int mode, int sasi, u32 total)
{
    reset_io();
    memset(identify_words, 0, sizeof(identify_words));
    memset(device_multiple, 0, sizeof(device_multiple));
    memset(setup_count, 0, sizeof(setup_count));
    identify_words[47] = 0x8000 | advertised_multiple;
    identify_words[59] = 0xFFFF; /* model reports the actual current setting */
    identify_words[IDE_ID_DEF_CYL_WORD] = 20;
    identify_words[IDE_ID_DEF_HEADS_WORD] = sasi ? 4 : 3;
    identify_words[IDE_ID_DEF_SPT_WORD] = sasi ? 33 : 7;
    if (mode == IDE_AMODE_LBA28) identify_words[IDE_ID_CAP_WORD] = IDE_ID_CAP_LBA;
    if (mode == IDE_AMODE_CHS_CUR) {
        identify_words[IDE_ID_VALID_WORD] = 1;
        identify_words[IDE_ID_CUR_CYL_WORD] = 30;
        identify_words[IDE_ID_CUR_HEADS_WORD] = 2;
        identify_words[IDE_ID_CUR_SPT_WORD] = 5;
    }
    identify_words[IDE_ID_TOTAL_WORD] = (u16)total;
    identify_words[IDE_ID_TOTAL_WORD + 1] = (u16)(total >> 16);
    CHECK(ide_init() == __builtin_popcount(present_mask));
    reset_io();
}

static struct { u16 before; u16 data[MAX_SECTORS * IDE_PIO_WORDS]; u16 after; } buffer;

static int request_drive(int drive, int wr, u32 lba, u32 count)
{
    u32 i;
    buffer.before = 0xC35A; buffer.after = 0x7E91;
    for (i = 0; i < MAX_SECTORS * IDE_PIO_WORDS; i++)
        buffer.data[i] = wr ? pattern(lba + i / IDE_PIO_WORDS, i % IDE_PIO_WORDS) : 0xDEAD;
    return wr ? ide_write_sectors(drive, lba, count, buffer.data) : ide_read_sectors(drive, lba, count, buffer.data);
}

static int request(int wr, u32 lba, u32 count)
{
    return request_drive(4, wr, lba, count);
}

static void verify_read(u32 lba, u32 count)
{
    u32 i;
    for (i = 0; i < count * IDE_PIO_WORDS; i++)
        CHECK(buffer.data[i] == pattern(lba + i / IDE_PIO_WORDS, i % IDE_PIO_WORDS));
    CHECK(buffer.before == 0xC35A && buffer.after == 0x7E91);
    for (; i < MAX_SECTORS * IDE_PIO_WORDS; i++) CHECK(buffer.data[i] == 0xDEAD);
}

static void sizes(void)
{
    static const unsigned counts[] = { 0, 1, 2, 64, 255, 256, 257 };
    unsigned wr, i;
    configure(IDE_AMODE_LBA28, 0, IDE_LBA28_LIMIT);
    for (wr = 0; wr < 2; wr++) for (i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        unsigned n = counts[i];
        reset_io();
        CHECK(request(wr, 19, n) == IDE_OK);
        printf("%s count=%u commands=%u phases=%u words=%u\n", wr ? "write" : "read", n, commands, phases, words);
        CHECK(commands == (n + 255) / 256);
        CHECK(phases == n && words == n * IDE_PIO_WORDS);
        if (n >= 256) CHECK(log_reg[0] == 0);
        if (n == 257) CHECK(log_lba[1] == 275 && log_count[1] == 1);
        if (!wr) verify_read(19, n);
        CHECK(buffer.before == 0xC35A && buffer.after == 0x7E91);
    }
}

static void boundaries(void)
{
    unsigned wr, mode, i;
    for (wr = 0; wr < 2; wr++) {
        configure(IDE_AMODE_LBA28, 0, IDE_LBA28_LIMIT);
        CHECK(request(wr, IDE_LBA28_LIMIT - 1, 1) == IDE_OK);
        reset_io(); CHECK(request(wr, IDE_LBA28_LIMIT - 1, 2) == IDE_ERR_RANGE);
        CHECK(!commands && !addr_writes);
        configure(IDE_AMODE_LBA28, 0, 50);
        CHECK(request(wr, 49, 1) == IDE_OK);
        reset_io(); CHECK(request(wr, 49, 2) == IDE_ERR_RANGE);
        CHECK(!commands && !addr_writes);
        CHECK(request(wr, 0xFFFFFFFFUL, 2) == IDE_ERR_RANGE);
        CHECK(!commands && !addr_writes);
        for (mode = IDE_AMODE_CHS_CUR; mode <= IDE_AMODE_CHS_DEF; mode++) {
            unsigned spt = mode == IDE_AMODE_CHS_CUR ? 5 : 7;
            unsigned heads = mode == IDE_AMODE_CHS_CUR ? 2 : 3;
            u32 limit = mode == IDE_AMODE_CHS_CUR ? 300 : 420;
            configure(mode, 0, 0);
            CHECK(request(wr, spt * heads - 1, spt + 2) == IDE_OK);
            CHECK(commands == 3 && log_count[0] == 1 && log_count[1] == spt && log_count[2] == 1);
            CHECK(log_lba[1] == spt * heads && log_lba[2] == spt * heads + spt);
            if (!wr) verify_read(spt * heads - 1, spt + 2);
            reset_io(); CHECK(request(wr, limit - 1, 1) == IDE_OK);
            reset_io(); CHECK(request(wr, limit - 1, 2) == IDE_ERR_RANGE);
            CHECK(!commands && !addr_writes);
        }
        for (mode = IDE_AMODE_LBA28; mode <= IDE_AMODE_CHS_DEF; mode += 2) {
            configure(mode, 1, 1000);
            CHECK(request(wr, 20, 4) == IDE_OK);
            CHECK(commands == 4);
            for (i = 0; i < 4; i++) CHECK(log_count[i] == 1 && log_lba[i] == 20 + i);
            if (!wr) verify_read(20, 4);
        }
        /* FatFs deliberately submits SASI requests at LBA increments of two. */
        for (i = 0; i < 4; i++) {
            reset_io(); CHECK(request(wr, 20 + 2 * i, 1) == IDE_OK);
            CHECK(commands == 1 && log_lba[0] == 20 + 2 * i);
        }
    }
    configure(IDE_AMODE_CHS_DEF, 0, 0);
    CHECK(ide_read_sector_chs(0, 1, 2, 7, buffer.data) == IDE_OK);
    CHECK(log_lba[0] == 41 && commands == 1);
    reset_io();
    for (i = 0; i < IDE_PIO_WORDS; i++) buffer.data[i] = pattern(41, i);
    CHECK(ide_write_sector_chs(0, 1, 2, 7, buffer.data) == IDE_OK);
    reset_io(); CHECK(ide_read_sector(0, 41, buffer.data) == IDE_OK);
    reset_io();
    for (i = 0; i < IDE_PIO_WORDS; i++) buffer.data[i] = pattern(41, i);
    CHECK(ide_write_sector(0, 41, buffer.data) == IDE_OK);
    CHECK(ide_read_sectors(1, 0, 0, buffer.data) == IDE_ERR_NO_DRIVE);
}

static void errors(void)
{
    unsigned wr, at, kind;
    static const unsigned positions[] = { 0, 17, 255, 256 };
    strict_timing = 1;
    configure(IDE_AMODE_LBA28, 0, 1000);
    for (wr = 0; wr < 2; wr++) for (at = 0; at < 4; at++) for (kind = STUCK_BSY; kind <= END_DRQ; kind++) {
        unsigned p = positions[at], drain = (!wr && kind == ERROR_DRQ && p < 256);
        int ret;
        if (kind == END_DRQ && p != 256) continue;
        if (kind == NO_DRQ && p == 256) continue; /* idle is normal completion */
        reset_io(); fault_kind = kind; fault_phase = p; fault_command = 1;
        ret = request(wr, 19, 257);
        CHECK(ret == ((kind == STUCK_BSY || kind == NO_DRQ || kind == END_DRQ) ? IDE_ERR_TIMEOUT : IDE_ERR_IO));
        CHECK(commands == 1 && phases == p + drain);
        CHECK(words == (p + drain) * IDE_PIO_WORDS);
        CHECK(buffer.before == 0xC35A && buffer.after == 0x7E91);
        if (!wr) {
            u32 i;
            /* ERR+DRQ is consumed but never copied into the destination. */
            for (i = p * IDE_PIO_WORDS; i < MAX_SECTORS * IDE_PIO_WORDS; i++)
                CHECK(buffer.data[i] == 0xDEAD);
        }
        /* A failed request never retries. Next request still has bounded BSY wait. */
        active = 0; pre_busy = 1;
        CHECK(request(wr, 19, 2) == IDE_ERR_TIMEOUT && commands == 1);
        reset_io(); CHECK(request(wr, 19, 2) == IDE_OK);
        if (!wr) verify_read(19, 2);
    }
}

#include "ide_pio_host_multiple.inc"

int main(int argc, char **argv)
{
    if (argc == 1 || !strcmp(argv[1], "sizes")) sizes();
    if (argc == 1 || !strcmp(argv[1], "boundaries")) boundaries();
    if (argc == 1 || !strcmp(argv[1], "errors")) errors();
    if (argc == 1 || !strcmp(argv[1], "multiple")) multiple_sizes();
    if (argc == 1 || !strcmp(argv[1], "multiple-errors")) multiple_errors();
    if (argc == 1 || !strcmp(argv[1], "multiple-boundaries")) multiple_boundaries();
    if (argc == 1 || !strcmp(argv[1], "multiple-identify")) multiple_identify();
    if (argc == 1 || !strcmp(argv[1], "multiple-setup")) multiple_setup_errors();
    if (argc == 1 || !strcmp(argv[1], "multiple-lifecycle")) multiple_lifecycle();
    puts("ide_pio: PASS");
    return 0;
}
