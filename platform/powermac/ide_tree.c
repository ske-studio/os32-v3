/* Apple Developer Note and BSD controller IDs/registers: docs/ppc/ata.md §§1–3. */
#include "ofw.h"
#include "kboot_info.h"
#include "ide_boot.h"
#include "kstring.h"

static int value(int node, const char *name, u32 expected)
{
    cell v;
    return ofw_getprop(node, name, &v, sizeof(v)) == sizeof(v) && v == expected;
}

static u32 bar_window(int node, u32 offset, u32 bytes)
{
    cell entries[5 * 8];
    int n = ofw_getprop(node, "assigned-addresses", entries, sizeof(entries));
    if (n <= 0 || n > (int)sizeof(entries) || n % 20) return 0;
    u32 base = 0;
    for (int i = 0; i < n / 4; i += 5) {
        cell *e = entries + i;
        if ((e[0] & 0xff) != 0x10) continue;
        if ((e[0] & 0x03000000) != 0x02000000 || e[1] || e[3] ||
            !e[2] || base || (e[2] & 1) || e[4] > ~e[2] ||
            offset > e[4] || bytes > e[4] - offset) return 0;
        base = e[2];
    }
    return base ? pm_ide_pci_phys(node, base + offset, bytes) : 0;
}

static int kauai(int node)
{
    cell id;
    if (!value(node, "vendor-id", 0x106b) ||
        ofw_getprop(node, "device-id", &id, sizeof(id)) != sizeof(id)) return 0;
    return id == 0x33 || id == 0x3b || id == 0x43 || id == 0x50 || id == 0x69;
}

static void walk(int node, int parent, int macio, unsigned depth, unsigned *left,
                 int *bank, u32 *pa)
{
    for (; node && node != -1 && *bank < PM_IDE_BANKS && *left && depth < PM_KBOOT_ANCESTORS;
         node = ofw_peer(node)) {
        --*left;
        char name[32] = {0};
        int n = ofw_getprop(node, "name", name, sizeof(name)-1);
        if (n < 0 || n >= (int)sizeof(name)) continue;
        if (macio && (!kstrncmp(name, "ata-", 4) || !kstrcmp(name, "ata") ||
                      !kstrcmp(name, "ATA") || !kstrcmp(name, "ide"))) {
            cell reg[8];
            int size = ofw_getprop(node, "reg", reg, sizeof(reg));
            if (size >= 8 && size <= (int)sizeof(reg) && !(size % 8) &&
                !(reg[0] & 1) && reg[1] > PM_IDE_CTL &&
                value(parent, "#address-cells", 1) && value(parent, "#size-cells", 1))
                pa[*bank] = bar_window(parent, reg[0], reg[1]);
            ++*bank;
        } else if (kauai(node)) {
            pa[(*bank)++] = bar_window(node, 0x2000, PM_IDE_CTL + 1);
        }
        walk(ofw_child(node), node, !kstrcmp(name, "mac-io"), depth + 1, left, bank, pa);
    }
}

void pm_ide_capture(u32 pa[PM_IDE_BANKS])
{
    unsigned left = PM_KBOOT_TREE_LIMIT;
    int bank = 0;
    int root = ofw_finddevice("/");
    if (root && root != -1) walk(ofw_child(root), root, 0, 0, &left, &bank, pa);
}
