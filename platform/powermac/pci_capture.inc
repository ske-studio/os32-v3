/* PCI Bus Binding to IEEE 1275 v2.1, ranges and assigned-addresses.
 * https://www.devicetree.org/open-firmware/bindings/pci/pci2_1.pdf
 * The shared OF scratch buffer is overwritten on every property read. */
static void pci_incomplete(int bridge)
{
    if (bridge < 0) info.pci.incomplete = 1;
    while (bridge >= 0) {
        struct pm_of_bridge *b = &info.pci.bridge[bridge];
        b->incomplete = 1;
        bridge = b->parent;
    }
}

static int pci_property(int node, const char *name, int bridge)
{
    cell args[3] = {(cell)node, ppc_ptr_cell(name), 0};
    if (!ofw_call("getproplen", 2, 1, args)) {
        int n = (int)args[2];
        if (n == -1) return -1;
        if (n >= 0 && n <= (int)sizeof(property) &&
            ofw_getprop(node, name, property, sizeof(property)) == n) return n;
    }
    pci_incomplete(bridge);
    return -2;
}

static u32 pci_scalar(int node, const char *name, int bridge, u32 fallback)
{
    int n = pci_property(node, name, bridge);
    if (n == 4) return be32(property);
    if (n != -1) pci_incomplete(bridge);
    return fallback;
}

static u64 pci_number(const u8 *p, unsigned cells)
{
    return cells == 2 ? ((u64)be32(p) << 32) | be32(p + 4) : be32(p);
}

static int pci_compatible(int node, const char *value, int bridge)
{
    int n = pci_property(node, "compatible", bridge);
    if (n < 0) return 0;
    if (!n || property[n-1]) { pci_incomplete(bridge); return 0; }
    int found = 0;
    for (int pos = 0; pos < n;) {
        const char *token = (const char *)property + pos;
        if (!*token) { pci_incomplete(bridge); return 0; }
        if (!kstrcmp(token, value)) found = 1;
        pos += kstrlen(token) + 1;
    }
    return found;
}

/* Translate a whole contiguous interval at every enclosing non-PCI bus. */
static int pci_cpu_interval(int bus, unsigned ac, unsigned sc, u64 *base, u64 bytes,
                            int bridge)
{
    int root = ofw_finddevice("/");
    if (!bytes || bytes-1 > ~(u64)0 - *base) return 0;
    for (unsigned depth = 0; bus != root; depth++) {
        if (ac == 1 && (*base > U32_MAX || bytes-1 > U32_MAX-*base)) return 0;
        int up = ofw_parent(bus);
        if (!up || up == -1 || depth >= PM_KBOOT_ANCESTORS) return 0;
        unsigned pac = pci_scalar(up, "#address-cells", bridge, 2);
        unsigned psc = pci_scalar(up, "#size-cells", bridge, 1);
        if (pac < 1 || pac > 2 || sc < 1 || sc > 2) return 0;
        unsigned stride = (ac + pac + sc) * 4;
        int n = pci_property(bus, "ranges", bridge);
        if (n < 0 || (unsigned)n % stride) return 0;
        u64 translated = *base;
        int found = 0;
        for (unsigned pos = 0; pos < (unsigned)n; pos += stride) {
            const u8 *p = property + pos;
            u64 child = pci_number(p, ac), parent = pci_number(p + ac*4, pac);
            u64 size = pci_number(p + (ac+pac)*4, sc);
            if (!size || size-1 > ~(u64)0-child || size-1 > ~(u64)0-parent) return 0;
            if (*base < child || *base-child >= size || bytes > size-(*base-child)) continue;
            if (found++) return 0;
            translated = parent + (*base-child);
        }
        if ((n && !found) || (pac == 1 && (translated > U32_MAX ||
            bytes-1 > U32_MAX-translated))) return 0;
        *base = translated;
        bus = up;
        ac = pac;
        sc = psc;
    }
    return *base <= U32_MAX && bytes-1 <= U32_MAX-*base;
}

static void pci_function(int node, int parent)
{
    struct pm_pci_capture *c = &info.pci;
    u32 vendor = pci_scalar(node, "vendor-id", parent, U32_MAX);
    u32 device_id = pci_scalar(node, "device-id", parent, U32_MAX);
    if (vendor == 0x106b && device_id == 0x21) c->gmac_known = 1;
    if (c->functions == PM_PCI_FUNCTIONS) { pci_incomplete(parent); return; }
    int index = c->functions++;
    struct pm_of_function *f = &c->function[index];
    f->id = node;
    f->bridge = parent;
    f->vendor = vendor;
    f->device = device_id;
    f->class_code = pci_scalar(node, "class-code", parent, U32_MAX);
    int n = pci_property(node, "compatible", parent);
    if (n > 0) {
        if ((unsigned)n > sizeof(f->compatible) || property[n-1]) pci_incomplete(parent);
        else {
            kmemcpy(f->compatible, property, n);
            f->compatible_bytes = n;
        }
    }
    n = pci_property(node, "assigned-addresses", parent);
    if (n <= 0) return;
    struct pm_of_bridge *b = &c->bridge[parent];
    if (b->ac != 3 || b->sc < 1 || b->sc > 2) { pci_incomplete(parent); return; }
    unsigned stride = (3 + b->sc) * 4;
    if ((unsigned)n % stride) { pci_incomplete(parent); return; }
    for (unsigned pos = 0; pos < (unsigned)n; pos += stride) {
        const u8 *p = property + pos;
        u64 base = pci_number(p+4, 2), bytes = pci_number(p+12, b->sc);
        if (!bytes || base + bytes < base || c->assigned == PM_PCI_ASSIGNED) {
            pci_incomplete(parent);
            return;
        }
        c->assignment[c->assigned++] = (struct pm_of_assignment){index, be32(p), base, bytes};
    }
}

static int pci_bridge(int node, int parent)
{
    struct pm_pci_capture *c = &info.pci;
    if (c->bridges == PM_PCI_BRIDGES) { pci_incomplete(parent); return -2; }
    int index = c->bridges++;
    struct pm_of_bridge *b = &c->bridge[index];
    b->id = node;
    b->parent = parent;
    b->host = parent < 0 ? index : c->bridge[parent].host;
    int up = ofw_parent(node);
    b->parent_ac = pci_scalar(up, "#address-cells", index, 2);
    b->parent_sc = pci_scalar(up, "#size-cells", index, 1);
    b->ac = pci_scalar(node, "#address-cells", index, 3);
    b->sc = pci_scalar(node, "#size-cells", index, 2);
    if (b->ac != 3 || b->sc < 1 || b->sc > 2 || b->parent_sc < 1 || b->parent_sc > 2 ||
        (parent < 0 ? b->parent_ac < 1 || b->parent_ac > 2 : b->parent_ac != 3)) {
        pci_incomplete(index);
        return index;
    }
    int n = pci_property(node, "bus-range", index);
    if (n == 8) { b->first = be32(property); b->last = be32(property+4); }
    if (n != 8 || b->last > 255 || b->first > b->last) pci_incomplete(index);
    else if (parent < 0 && pci_compatible(node, "uni-north", index)) {
        n = pci_property(node, "reg", index);
        unsigned stride = (b->parent_ac + b->parent_sc) * 4;
        if (n > 0 && !((unsigned)n % stride)) {
            u64 pa = pci_number(property, b->parent_ac);
            u64 len = pci_number(property + b->parent_ac*4, b->parent_sc);
            if (len >= 0xc00008 && pci_cpu_interval(up, b->parent_ac, b->parent_sc,
                                                    &pa, len, index) && !(pa & 3)) {
                b->cfg_pa = pa;
                b->supported = 1;
            } else pci_incomplete(index);
        } else pci_incomplete(index);
    }
    n = pci_property(node, "ranges", index);
    unsigned stride = (3 + b->parent_ac + b->sc) * 4;
    if (n <= 0 || (unsigned)n % stride) { pci_incomplete(index); return index; }
    for (unsigned pos = 0; pos < (unsigned)n; pos += stride) {
        const u8 *p = property + pos, *up_addr = p + 12;
        unsigned pci = parent >= 0;
        u64 base = pci_number(p+4, 2);
        u64 pa = pci_number(up_addr + pci*4, b->parent_ac-pci);
        u64 bytes = pci_number(up_addr + b->parent_ac*4, b->sc);
        if (!bytes || base + bytes < base || pa + bytes < pa || c->ranges == PM_PCI_RANGES) {
            pci_incomplete(index);
            break;
        }
        unsigned space = (be32(p)>>24)&3, parent_space = pci ? (be32(up_addr)>>24)&3 : 0;
        if (!pci && up != ofw_finddevice("/")) {
            if (!pci_cpu_interval(up, b->parent_ac, b->parent_sc, &pa, bytes, index) ||
                pci_property(node, "ranges", index) != n) {
                pci_incomplete(index);
                break;
            }
        }
        c->range[c->ranges++] = (struct pm_of_range){index, space,
            parent_space, base, pa, bytes};
    }
    return index;
}
