/* NetBSD macppc/dev/{pmu,cuda}.c match the OF name. Decode the
 * parent path using the OF PCI binding, as in mpic_tree.c. */
#include "kboot_info.h"
#include "via_window.h"
#include "kstring.h"
#include "memmap.h"

#define PCI_SPACE 0x03000000U
#define PCI_MEMORY 0x02000000U
#define PCI_ABSOLUTE 0x80000000U
#define PCI_BAR_TOKEN 0x00ffffffU
#define PCI_BAR0 0x10U

static int prop(int node, const char *key, void *data, unsigned cap)
{
    cell args[3] = { (cell)node, ppc_ptr_cell(key), 0 };
    if (ofw_call("getproplen", 2, 1, args)) return -2;
    int n = (int)args[2];
    if (n == -1) return n;
    if (n < 0 || (unsigned)n > cap ||
        ofw_getprop(node, key, data, cap) != n) return -2;
    return n;
}

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static int width(int node, const char *key, int fallback, int max)
{
    u8 buf[PM_CELL_BYTES];
    int n = prop(node, key, buf, sizeof(buf));
    if (n == -1) return fallback;
    if (n != (int)sizeof(buf) || !be32(buf) || be32(buf) > (u32)max) return 0;
    return (int)be32(buf);
}

static int address(const u8 *p, int cells, u32 *tag, u32 *value)
{
    *tag = cells == 3 ? be32(p) : 0;
    if (cells == 3 && (*tag & PCI_SPACE) != PCI_MEMORY) return 0;
    if (cells >= 2 && be32(p + (cells - 2) * PM_CELL_BYTES)) return 0;
    *value = be32(p + (cells - 1) * PM_CELL_BYTES);
    return 1;
}

static int contains(u32 base, u32 size, u32 start, u32 bytes)
{
    return size && size - 1 <= ~base && start >= base &&
           bytes <= size && start - base <= size - bytes;
}

static int macio_bar(int node, u32 tag, u32 offset, u32 bytes, u32 *result)
{
    u8 buf[PM_KBOOT_PROP_BYTES];
    int n = prop(node, "assigned-addresses", buf, sizeof(buf)), matches = 0;
    const int stride = 5 * PM_CELL_BYTES;
    if (n <= 0 || n % stride) return 0;
    for (int i = 0; i < n; i += stride) {
        const u8 *p = buf + i;
        u32 hi = be32(p), base, size;
        if ((hi & 0xffU) != PCI_BAR0) continue;
        if (!address(p, 3, &hi, &base) || be32(p + 3 * PM_CELL_BYTES))
            return 0;
        size = be32(p + 4 * PM_CELL_BYTES);
        u32 token = tag & PCI_BAR_TOKEN;
        if (token && token != (hi & PCI_BAR_TOKEN)) return 0;
        int relative = !(tag & PCI_ABSOLUTE) && token &&
                       contains(base, size, base, bytes) && offset <= size - bytes;
        int assigned = contains(base, size, offset, bytes);
        if (relative && assigned && base) return 0;
        if (!relative && !assigned) return 0;
        *result = relative ? base + offset : offset;
        matches++;
    }
    return matches == 1;
}

static int translate(int bus, int macio, int ac, int sc, int pac,
                     u32 *a, u32 *tag, u32 bytes)
{
    u8 buf[PM_KBOOT_PROP_BYTES];
    int n = prop(bus, "ranges", buf, sizeof(buf));
    int stride = (ac + pac + sc) * PM_CELL_BYTES, matches = 0;
    u32 next = 0, next_tag = 0;
    if (n < 0 || n % stride) return PM_VIA_BAD_PROPERTY;
    if (!n) return bus != macio && ((ac == 3) == (pac == 3))
                      ? 0 : PM_VIA_BAD_PROPERTY;
    for (int i = 0; i < n; i += stride) {
        const u8 *p = buf + i;
        u32 child, ch, dest, dh, size_tag, size;
        if (ac == 3 && (be32(p) & PCI_SPACE) != PCI_MEMORY) continue;
        if (!address(p, ac, &ch, &child) ||
            !address(p + ac * PM_CELL_BYTES, pac, &dh, &dest) ||
            !address(p + (ac + pac) * PM_CELL_BYTES, sc, &size_tag, &size))
            return PM_VIA_BAD_ADDRESS;
        if ((ch & PCI_SPACE) != (*tag & PCI_SPACE) ||
            !contains(child, size, *a, bytes)) continue;
        if (*a - child > ~dest) return PM_VIA_BAD_ADDRESS;
        dest += *a - child;
        if (bus == macio) {
            if (pac != 3 || !macio_bar(bus, dh, dest, bytes, &dest))
                return PM_VIA_BAD_ADDRESS;
            dh = PCI_MEMORY;
        } else if (pac == 3 && !(dh & PCI_ABSOLUTE) && (dh & 0xffU)) {
            return PM_VIA_BAD_ADDRESS;
        }
        if (bytes > ~dest) return PM_VIA_BAD_ADDRESS;
        next = dest;
        next_tag = dh;
        matches++;
    }
    if (matches != 1) return PM_VIA_BAD_ADDRESS;
    *a = next;
    *tag = next_tag;
    return 0;
}

/* Convert an assigned PCI memory address through its host bridge ranges. */
#if OS32_FEATURE_IDE
#include "ide_boot.h"
u32 pm_ide_pci_phys(int device, u32 address, u32 bytes)
{
    int bus = ofw_parent(device), root = ofw_finddevice("/");
    u32 tag = PCI_MEMORY;
    for (unsigned depth = 0; bus != root; ++depth) {
        int parent = ofw_parent(bus);
        if (!bus || bus == -1 || !parent || parent == -1 || depth >= PM_KBOOT_ANCESTORS)
            return 0;
        int ac = width(bus, "#address-cells", 2, 3);
        int sc = width(bus, "#size-cells", 1, 2);
        int pac = width(parent, "#address-cells", 2, 3);
        if (!ac || !sc || !pac || translate(bus, 0, ac, sc, pac, &address, &tag, bytes))
            return 0;
        bus = parent;
    }
    return tag ? 0 : address;
}
#endif

static int resolve(int node, int macio, int root, struct pm_via_boot *out)
{
    u8 reg[(3 + 2) * PM_CELL_BYTES];
    int bus = ofw_parent(node);
    int ac = width(bus, "#address-cells", 2, 3), sc = width(bus, "#size-cells", 1, 2);
    u32 a, tag, bytes, size_tag;
    if (!ac || !sc || prop(node, "reg", reg, sizeof(reg)) != (ac + sc) * PM_CELL_BYTES)
        return PM_VIA_BAD_PROPERTY;
    if (!address(reg, ac, &tag, &a) ||
        !address(reg + ac * PM_CELL_BYTES, sc, &size_tag, &bytes) ||
        bytes < PM_VIA_REG_BYTES || bytes > ~a) return PM_VIA_BAD_ADDRESS;
    for (unsigned depth = 0; bus != root; depth++) {
        int parent = ofw_parent(bus);
        if (!parent || parent == -1 || depth >= PM_KBOOT_ANCESTORS)
            return PM_VIA_BAD_PROPERTY;
        int pac = width(parent, "#address-cells", 2, 3);
        if (!pac || !sc) return PM_VIA_BAD_PROPERTY;
        int rc = translate(bus, macio, ac, sc, pac, &a, &tag, bytes);
        if (rc) return rc;
        bus = parent;
        ac = pac;
        sc = width(bus, "#size-cells", 1, 2);
    }
    u32 span = (a & (MEM_PAGE_SIZE - 1)) + PM_VIA_REG_BYTES;
    u32 rounded = (span + MEM_PAGE_SIZE - 1) & ~(MEM_PAGE_SIZE - 1);
    if (tag || rounded > ~(a & ~(MEM_PAGE_SIZE - 1))) return PM_VIA_BAD_ADDRESS;
    out->pa = a;
    out->bytes = bytes;
    return 0;
}

static int named(const char *buf, int n, const char *name)
{
    int len = (int)kstrlen(name);
    return (n == len || (n == len + 1 && !buf[len])) &&
           !kstrncmp(buf, name, len);
}

static int walk(int node, int macio, int root, unsigned depth, unsigned *left,
                int *found, struct pm_via_boot *out)
{
    for (; node; node = ofw_peer(node)) {
        if (node == -1) return PM_VIA_BAD_PROPERTY;
        if (!*left || depth >= PM_KBOOT_ANCESTORS) return PM_VIA_BAD_PROPERTY;
        --*left;
        char name[PM_KBOOT_PROP_BYTES];
        int n = prop(node, "name", name, sizeof(name));
        if (n == -2) return PM_VIA_BAD_PROPERTY;
        int child_macio = named(name, n, "mac-io") ? node : macio;
        if (macio && (named(name, n, "via-pmu") || named(name, n, "via-cuda"))) {
            if (*found && *found != node) return PM_VIA_AMBIGUOUS;
            if (!*found) {
                *found = node;
                out->kind = named(name, n, "via-pmu") ? PM_VIA_PMU : PM_VIA_CUDA;
                int len = prop(node, "compatible", out->compatible, PM_KBOOT_PROP_BYTES);
                for (int i = 0; i < len; i++) {
                    unsigned char c = out->compatible[i];
                    if (!c && i + 1 < len) out->compatible[i] = ',';
                    else if (c && (c < 32 || c > 126)) out->compatible[i] = '?';
                }
                out->compatible[len < 0 ? 0 : len] = 0;
                out->capture_rc = resolve(node, macio, root, out);
            }
        }
        int rc = walk(ofw_child(node), child_macio, root, depth + 1, left, found, out);
        if (rc) return rc;
    }
    return 0;
}

void pm_via_capture(int root, struct pm_via_boot *out)
{
    *out = (struct pm_via_boot){ .capture_rc = PM_VIA_MISSING };
    unsigned left = PM_KBOOT_TREE_LIMIT;
    int found = 0;
    int rc = walk(ofw_child(root), 0, root, 0, &left, &found, out);
    if (rc) {
        out->pa = out->bytes = 0;
        out->capture_rc = rc;
    }
}
