#include "userland/system/inst_disk.h"
#include "sys/fs/pc98pt.h"

static unsigned long inst_rd16(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

/* 名前欄が "OS32" + 空白 (か NUL) か。書き手は空白で埋める
 * (pc98pt_make_os32 / 旧 cdinst / 旧 install / tools/pc98pt.py)。 */
static int name_is_os32(const unsigned char *e)
{
    const char *nm = PC98PT_NAME_OS32;
    int i;

    for (i = 0; nm[i]; i++)
        if (e[PC98PT_OFF_NAME + i] != (unsigned char)nm[i]) return 0;
    for (; i < PC98PT_NAME_LEN; i++) {
        unsigned char c = e[PC98PT_OFF_NAME + i];
        if (c != ' ' && c != 0) return 0;
    }
    return 1;
}

int inst_classify(const unsigned char *lba0, const unsigned char *lba1,
                  unsigned long heads, unsigned long spt,
                  unsigned long disk_total, unsigned long expect_start,
                  int *out_mode)
{
    PC98PartEntry e;
    const unsigned char *raw;
    unsigned long st, ln;
    int used, i, idx = -1;

    if (!lba0 || !lba1 || !out_mode) return INST_E_ARG;

    used = pc98pt_count_used(lba1);
    if (used == 0) {
        /* 空のディスク。hdprep と同じく LBA 0 の 55AA (何かの起動域) は断る */
        if (lba0[510] == 0x55 && lba0[511] == 0xAA) return HDPREP_E_MBR_SIG;
        *out_mode = INST_MODE_EMPTY;
        return 0;
    }
    if (used != 1) return INST_E_MULTI;

    for (i = 0; i < PC98PT_MAX_ENTRIES; i++) {
        (void)pc98pt_get(lba1, i, &e);
        if (!pc98pt_entry_empty(&e)) { idx = i; break; }
    }
    if (idx < 0) return INST_E_ARG;               /* count_used と食い違う (来ない) */
    raw = lba1 + idx * PC98PT_ENTRY_SIZE;

    /* OS32 が作ったもの = sid 0xE2 かつ名前 "OS32" */
    if (e.sys_id != PC98PT_SID_OS32 || !name_is_os32(raw)) return INST_E_FOREIGN;

    /* 標準配置で読めるなら開始を比べる */
    if (pc98pt_entry_range(&e, heads, spt, disk_total, &st, &ln) == PC98PT_OK) {
        if (st != expect_start) return INST_E_START;
        *out_mode = INST_MODE_RECREATE;
        return 0;
    }

    /* 旧配置 (+6 開始セクタ / +7 開始ヘッド / +8-9 開始シリンダ)。旧 cdinst と
     * 旧 install は 8/17 でシリンダ 12 (= LBA 1632) を書いた。BIOS 幾何で同じ
     * LBA になるとき (8/17 のディスク) だけ作り直す — 16/63 ではシリンダ 12 は
     * LBA 12,096 で、期待値 2016 と違うので断る */
    if (pc98pt_os32_is_legacy(lba1, heads, spt, disk_total)) {
        if (pc98pt_chs_to_lba(inst_rd16(raw + 8), raw[7], raw[6], heads, spt, &st)
            != PC98PT_OK)
            return INST_E_BROKEN;
        if (st != expect_start) return INST_E_START;
        *out_mode = INST_MODE_RECREATE_OLD;
        return 0;
    }
    return INST_E_BROKEN;
}

int inst_check_boot_files(unsigned long ipl_len, unsigned long loader_len,
                          unsigned long kernel_len)
{
    if (ipl_len == 0 || ipl_len > INST_IPL_MAX) return INST_E_IPL_SIZE;
    if (loader_len == 0 || loader_len > INST_LOADER_MAX) return INST_E_LOADER_SIZE;
    if (kernel_len == 0 || kernel_len > INST_KERNEL_MAX) return INST_E_KERNEL_SIZE;
    return 0;
}

void inst_patch_ipl(unsigned char *ipl, unsigned long heads, unsigned long spt)
{
    if (!ipl) return;
    ipl[INST_IPL_OFF_HEADS] = (unsigned char)heads;
    ipl[INST_IPL_OFF_SPT] = (unsigned char)spt;
    ipl[510] = 0x55;
    ipl[511] = 0xAA;
}
