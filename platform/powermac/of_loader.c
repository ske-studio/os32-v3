/* The HFS template has its plan patched by the installer before deployment. */
#include "product_boot.h"
#include "ofw.h"
#include "memmap.h"
#include "kstring.h"
#include "apm.h"

const volatile struct pm_boot_plan pm_loader_plan __attribute__((section(".data.plan"))) = {0};

static int read_at(int file, unsigned offset, void *data, unsigned bytes)
{
    cell pos[4] = {file, 0, offset};
    if (ofw_call("seek", 3, 1, pos) || pos[3]) return 0;
    while (bytes) {
        cell read[4] = {file, (cell)data, bytes};
        if (ofw_call("read", 3, 1, read) || !read[3] || read[3] > bytes) return 0;
        bytes -= read[3];
        data = (char *)data + read[3];
    }
    return 1;
}

void powermac_boot(ppc_ci ci)
{
    struct pm_product_boot h = { .magic = PM_BOOT_MAGIC, .version = 1,
        .size = sizeof(h), .kind = 1, .plan = pm_loader_plan };
    char path[sizeof(h.path)];
    ofw_init(ci);
    int n = ofw_getprop(ofw_finddevice("/chosen"), "bootpath", path, sizeof(path));
    if (n <= 0 || n >= (int)sizeof(path)) return;
    path[n] = 0;
    int output;
    int console = ofw_getprop(ofw_finddevice("/chosen"), "stdout", &output, sizeof(output))
        == sizeof(output);
    if (console) {
        static const char prefix[] = "OS32 HDD loader: ";
        ofw_write(output, prefix, sizeof(prefix)-1);
        ofw_write(output, path, kstrlen(path));
        ofw_write(output, "\r\n", 2);
    }
    char *sep = path;
    while (*sep && *sep != ':') ++sep;
    if (*sep != ':' || sep == path) return;
    *sep = 0;
    int disk = ofw_finddevice(path);
    if (!disk || disk == -1) return;
    int path_size = ofw_package_to_path(disk, h.path, sizeof(h.path));
    if (path_size <= 0 || path_size >= (int)sizeof(h.path)) return;
    h.path[path_size] = 0;
    h.path_bytes = kstrlen(h.path);
    *sep++ = ':';
    unsigned partition = 0;
    while (*sep >= '0' && *sep <= '9' && partition <= PM_APM_MAX_ENTRIES)
        partition = partition * 10 + *sep++ - '0';
    if (!partition || partition != h.plan.boot_partition || *sep != ',' ||
        sizeof(path) - (unsigned)(sep-path) < sizeof(",\\PRODUCT.ELF")) return;
    kstrcpy(sep, ",\\PRODUCT.ELF");
    int file = ofw_open(path);
    struct {
        unsigned char ident[16];
        unsigned short type, machine;
        unsigned version, entry, phoff, shoff, flags;
        unsigned short ehsize, phsize, phnum, shsize, shnum, shstr;
    } header;
    if (file == -1) {
        static const char missing[] = "HDD loader cannot open PRODUCT.ELF\r\n";
        if (console) ofw_write(output, missing, sizeof(missing)-1);
        return;
    }
    if (!read_at(file, 0, &header, sizeof(header))) return;
    if (memcmp(header.ident, "\177ELF\1\2\1", 7) || header.type != 2 ||
        header.machine != 20 || header.version != 1 || header.ehsize != sizeof(header) ||
        header.phsize != 32 || header.phnum != 2 || header.phoff > 4096 ||
        header.entry != KERNEL_LOAD_ADDR) return;
    unsigned ph[2][8];
    if (!read_at(file, header.phoff, ph, sizeof(ph))) return;
    unsigned *load = ph[0];
    if (load[0] != 1 || ph[1][0] != 4 || load[2] != header.entry || load[3] != load[2] ||
        load[4] < PM_HANDOFF_OFFSET + sizeof(h) || load[4] > load[5] ||
        load[5] > MEM_KERNEL_IMAGE_MAX || !(load[6] & 1)) return;
    cell claim[4] = {load[2], load[5], 0};
    if (ofw_call("claim", 3, 1, claim) || claim[3] != load[2]) return;
    if (!read_at(file, load[1], (void *)load[2], load[4])) return;
    cell close[1] = {file};
    ofw_call("close", 1, 0, close);
    struct pm_product_boot *handoff = (void *)(load[2] + PM_HANDOFF_OFFSET);
    *handoff = h;
    for (unsigned a = load[2]; a < load[2]+load[4]; a += 32)
        __asm__ volatile("dcbst 0,%0" :: "r"(a) : "memory");
    __asm__ volatile("sync" ::: "memory");
    for (unsigned a = load[2]; a < load[2]+load[4]; a += 32)
        __asm__ volatile("icbi 0,%0" :: "r"(a) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
    ((void (*)(void *, unsigned, ppc_ci))header.entry)(handoff, sizeof(*handoff), ci);
}
