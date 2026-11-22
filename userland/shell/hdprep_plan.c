/* Shared size parsing, mount checks and refusal messages for hdprep. */

#include "hdprep_plan.h"

int hdprep_check_mounts(int mount_count, int root_is_hd0)
{
    if (root_is_hd0) return HDPREP_E_ROOT;
    if (mount_count < 0) return HDPREP_E_ARG;
    if (mount_count > 0) return HDPREP_NEED_UMOUNT;
    return HDPREP_OK;
}

int hdprep_parse_mb(const char *s, unsigned long *out_mb)
{
    unsigned long v = 0;
    int n = 0;

    if (!out_mb) return HDPREP_E_ARG;
    if (!s || !*s) {
        *out_mb = HDPREP_DEFAULT_MB;
        return HDPREP_OK;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10UL + (unsigned long)(*s - '0');
        if (v > HDPREP_MAX_MB) return HDPREP_E_SIZE;   /* 桁が増える前に止める */
        s++;
        n++;
    }
    if (*s != '\0' || n == 0) return HDPREP_E_SIZE;
    if (v < HDPREP_MIN_MB || v > HDPREP_MAX_MB) return HDPREP_E_SIZE;
    *out_mb = v;
    return HDPREP_OK;
}

const char *hdprep_reason(int code)
{
    switch (code) {
    case HDPREP_OK:              return "ok";
    case HDPREP_NEED_UMOUNT:     return "hd0 is mounted (will umount first)";
    case HDPREP_E_NO_ATA:        return "hd0 does not answer IDENTIFY";
    case HDPREP_E_NO_BIOS:       return "no BIOS geometry (INT 1Bh AH=84h) for DA 80h";
    case HDPREP_E_SECLEN:        return "BIOS sector length (BX) is not 512";
    case HDPREP_E_ADDR:          return "drive has neither LBA nor a current CHS translation";
    case HDPREP_E_PT_USED:       return "LBA 1 has a partition entry (disk is not empty)";
    case HDPREP_E_MBR_SIG:       return "LBA 0 ends with 55AA (disk is not empty)";
    case HDPREP_E_ROOT:          return "hd0 is the root file system";
    case HDPREP_E_SIZE:          return "size must be 8..256 (MiB)";
    case HDPREP_E_TOO_SMALL:     return "disk is too small for the area";
    case HDPREP_E_CYL:           return "end cylinder does not fit in 16 bits";
    case HDPREP_E_STILL_MOUNTED: return "hd0 is still mounted after umount";
    case HDPREP_E_NO_TOTAL:      return "IDENTIFY reports no total sector count";
    default:                     return "bad argument";
    }
}
