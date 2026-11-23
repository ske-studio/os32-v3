#if PM_PRODUCT_BOOT
#include "product_boot.h"
#endif
/* Apple Developer Note and BSD controller IDs/registers: docs/ppc/ata.md §§1–3. */
#include "ofw.h"
#include "kboot_info.h"
#include "ide_boot.h"
#include "apm.h"
#include "ide_compatible.h"
#include "kstring.h"

static int prop(int node, const char *key, void *data, unsigned cap)
{
    cell args[3] = { (cell)node, ppc_ptr_cell(key), 0 };
    if (ofw_call("getproplen", 2, 1, args)) return -1;
    int n = (int)args[2];
    if (n < 0 || (unsigned)n > cap || ofw_getprop(node, key, data, cap) != n)
        return -1;
    return n;
}

static int ata_node(int node, const char *name)
{
    if (!kstrcmp(name, "ATA") || !kstrcmp(name, "ide") ||
        !kstrcmp(name, "ata") || !kstrcmp(name, "ata0") ||
        !kstrncmp(name, "ata-", 4)) return 1;
    char buf[PM_KBOOT_PROP_BYTES + 1];
    int n = prop(node, "device_type", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        if (!kstrcmp(buf, "ata") || !kstrcmp(buf, "ide")) return 1;
    }
    n = prop(node, "compatible", buf, sizeof(buf) - 1);
    return pm_ide_compatible(buf, n);
}

static int value(int node, const char *name, u32 expected)
{
    cell v;
    return prop(node, name, &v, sizeof(v)) == sizeof(v) && v == expected;
}

static u32 bar_window(int node, u32 offset, u32 bytes)
{
    cell entries[5 * 8];
    int n = prop(node, "assigned-addresses", entries, sizeof(entries));
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
        prop(node, "device-id", &id, sizeof(id)) != sizeof(id)) return 0;
    return id == 0x33 || id == 0x3b || id == 0x43 || id == 0x50 || id == 0x69;
}

/* TFTP keeps its own /chosen/bootpath. Select the HDD by controller
 * package, ATA target and one-based APM entry in the explicit bootarg
 * os32-root=/absolute/controller/path/disk@0:3. */
int pm_root_unit = -1;
u32 pm_root_partition;
static int root_controller, root_target;
static int cd_controller;
int pm_cd_bank = -1;
int pm_cd_target;

static void capture_root(void)
{
    char args[PM_KBOOT_PROP_BYTES + 1];
#if PM_PRODUCT_BOOT
    const char *path = pm_product_handoff.path;
    unsigned len = kstrlen(path);
    if (len + 15 >= sizeof(args)) { pm_root_unit = -2; return; }
    kstrcpy(args, "os32-root=");
    kstrcpy(args + 10, path);
    int n = 10 + len;
    args[n++] = ':';
    unsigned number = pm_product_handoff.plan.root_partition;
    if (number >= 100) args[n++] = '0' + number / 100;
    if (number >= 10) args[n++] = '0' + number / 10 % 10;
    args[n++] = '0' + number % 10;
#elif PM_INSTALL_MEDIA
    /* Installer media always uses its embedded root, even with stale NVRAM. */
    int n = 0;
#else
    int chosen = ofw_finddevice("/chosen");
    int n = ofw_getprop(chosen, "bootargs", args, sizeof(args) - 1);
#endif
    if (n <= 0) return;
    if (n >= (int)sizeof(args) - 1) {
        pm_root_unit = -2;
        return;
    }
    args[n] = 0;
    for (char *p = args; *p;) {
        while (*p == ' ' || *p == '\t') ++p;
        char *token = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = 0;
        if (kstrncmp(token, "os32-root=", 10)) continue;
        if (pm_root_unit != -1) { root_controller = 0; return; }
        pm_root_unit = -2;
        char *path = token + 10, *leaf = path;
        if (*path != '/') return;
        for (char *q = path; *q; ++q) if (*q == '/') leaf = q;
        if (kstrncmp(leaf, "/disk@", 6) || (leaf[6] != '0' && leaf[6] != '1') ||
            leaf[7] != ':') return;
        root_target = leaf[6] - '0';
        u32 number = 0;
        for (char *q = leaf + 8; *q; ++q) {
            if (*q < '0' || *q > '9' || number > PM_APM_MAX_ENTRIES) return;
            number = number * 10 + *q - '0';
        }
        if (!number || number > PM_APM_MAX_ENTRIES) return;
        leaf[7] = 0;
        int disk = ofw_finddevice(path);
        if (!disk || disk == -1) return;
        int controller = ofw_parent(disk);
        *leaf = 0;
        if (!controller || controller == -1 || controller != ofw_finddevice(path)) return;
        int matches = 0;
        unsigned left = PM_KBOOT_TREE_LIMIT;
        int child = ofw_child(controller);
        for (; child && child != -1 && left; child = ofw_peer(child), --left) {
            if (!value(child, "reg", root_target)) continue;
            if (child != disk) return;
            ++matches;
        }
        if (child || matches != 1) return;
        root_controller = controller;
        pm_root_partition = number;
    }
}

static void walk(int node, int parent, int macio, unsigned depth, unsigned *left,
                 int *bank, u32 *pa)
{
    for (; node && node != -1 && *bank < PM_IDE_BANKS && *left && depth < PM_KBOOT_ANCESTORS;
         node = ofw_peer(node)) {
        --*left;
        char name[32] = {0};
        int n = prop(node, "name", name, sizeof(name)-1);
        if (n < 0) name[0] = 0;
        int previous = *bank;
        if (macio && ata_node(node, name)) {
            cell reg[8];
            int size = prop(node, "reg", reg, sizeof(reg));
            if (size >= 8 && size <= (int)sizeof(reg) && !(size % 8) &&
                !(reg[0] & 1) && reg[1] > PM_IDE_CTL &&
                value(parent, "#address-cells", 1) && value(parent, "#size-cells", 1))
                pa[*bank] = bar_window(parent, reg[0], reg[1]);
            ++*bank;
        } else if (kauai(node)) {
            pa[(*bank)++] = bar_window(node, 0x2000, PM_IDE_CTL + 1);
        }
        if (node == cd_controller && *bank != previous && pa[previous])
            pm_cd_bank = previous;
        if (node == root_controller && *bank != previous && pa[previous])
            pm_root_unit = previous * 2 + root_target;
        walk(ofw_child(node), node, !kstrcmp(name, "mac-io"), depth + 1, left, bank, pa);
    }
}

void pm_ide_capture(u32 pa[PM_IDE_BANKS])
{
    int cd = ofw_finddevice("cd");
    if (cd && cd != -1) {
        cell target;
        if (prop(cd, "reg", &target, sizeof(target)) == sizeof(target) && target < 2) {
            cd_controller = ofw_parent(cd);
            pm_cd_target = target;
        }
    }
    capture_root();
    unsigned left = PM_KBOOT_TREE_LIMIT;
    int bank = 0;
    int root = ofw_finddevice("/");
    if (root && root != -1) walk(ofw_child(root), root, 0, 0, &left, &bank, pa);
}
