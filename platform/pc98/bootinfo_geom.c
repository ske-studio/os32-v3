#include "bootinfo.h"
#include "bootinfo_native.h"
#include "../../init/boot_disk.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "build_id.h"
static const struct boot_disk_ops *s_disk;

void bootinfo_disk_register(const struct boot_disk_ops *ops)
{
    s_disk = ops;
}

int bootinfo_hdd_geom(int da, u16 *cyl, u8 *heads, u8 *spt, u16 *seclen)
{
    int i;

    if (bootinfo_get()->status != BOOTINFO_OK) return bootinfo_get()->status;
    for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
        const struct bootinfo_drive *d = &bootinfo_get()->drive[i];
        if (!d->queried || (int)d->da != da) continue;
        if (!d->valid) return -1;
        if (cyl)    *cyl = d->cyl;
        if (heads)  *heads = d->heads;
        if (spt)    *spt = d->spt;
        if (seclen) *seclen = d->seclen;
        return 0;
    }
    return -1;
}

int bootinfo_part_geom(int ide_drive, u16 *heads, u16 *spt)
{
    u8 h, s;
    int da;

    if (!s_disk || !s_disk->valid(ide_drive)) return -1;
    da = s_disk->bios_da(ide_drive);

    if (da != 0 && bootinfo_hdd_geom(da, (u16 *)0, &h, &s, (u16 *)0) == 0) {
        if (heads) *heads = h;
        if (spt)   *spt = s;
        return BOOTINFO_GEOM_BIOS;
    }
    /* BIOS 幾何が無い (FD 起動で問い合わせなかった / 規則に外れる) ときは
     * IDENTIFY の既定。NP21/W の NHD はどちらも同じ値なので従来どおり。 */
    if (s_disk->default_geom(ide_drive, heads, spt) != 0) return -1;
    return BOOTINFO_GEOM_IDENTIFY;
}

int hdd_geom_info(int drive, void *out_v)
{
    HddGeom *out = (HddGeom *)out_v;
    int da, i;

    if (!out) return OS32_ERR_INVAL;
    if (!s_disk || !s_disk->valid(drive)) return OS32_ERR_INVAL;
    kmemset(out, 0, sizeof(*out));

    da = s_disk->bios_da(drive);
    out->bios_da = (u8)da;
    if (da != 0 && bootinfo_get()->status == BOOTINFO_OK) {
        for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
            const struct bootinfo_drive *d = &bootinfo_get()->drive[i];
            if (!d->queried || (int)d->da != da) continue;
            out->bios_queried = 1;
            out->bios_valid   = d->valid;
            out->bios_heads   = d->heads;
            out->bios_spt     = d->spt;
            out->bios_cyl     = d->cyl;
            out->bios_seclen  = d->seclen;
            break;
        }
    }

    s_disk->fill_ata(drive, out);
    return 0;
}


const struct boot_disk_ops *pc98_boot_disk_get(void) { return s_disk; }
