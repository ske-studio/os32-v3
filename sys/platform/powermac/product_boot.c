#include "product_boot.h"
#include "apm.h"
#include "ofw.h"
#include "platform_module_pair.h"

struct pm_product_boot pm_product_handoff __attribute__((section(".product.handoff")));
const volatile struct pm_boot_plan pm_expected_plan __attribute__((section(".product.plan"))) = {0};
extern char pm_distribution_pair[];
static int valid;

int pm_validate_product(ppc_ci ci)
{
    const struct pm_product_boot *h = &pm_product_handoff;
    const struct pm_boot_plan *p = &h->plan;
    unsigned n = 0;
    while (n < sizeof(h->path) && h->path[n]) ++n;
    if (p->root_partition != pm_expected_plan.root_partition) {
        int out;
        static const char message[] = "HDD root partition number mismatch\r\n";
        ofw_init(ci);
        if (ofw_getprop(ofw_finddevice("/chosen"), "stdout", &out, sizeof(out)) == sizeof(out))
            ofw_write(out, message, sizeof(message)-1);
        return 0;
    }
    valid = h->magic == PM_BOOT_MAGIC && h->version == 1 && h->size == sizeof(*h) &&
        h->kind == 1 && n && n < sizeof(h->path) && n == h->path_bytes && h->path[0] == '/' &&
        p->root_partition && p->root_partition <= PM_APM_MAX_ENTRIES &&
        p->root_partition != p->boot_partition && p->boot_partition &&
        p->boot_partition <= PM_APM_MAX_ENTRIES && p->root_sectors &&
        p->root_type == 3 && pm_expected_plan.root_type == p->root_type &&
        p->boot_partition == pm_expected_plan.boot_partition &&
        p->root_start == pm_expected_plan.root_start && p->root_sectors == pm_expected_plan.root_sectors;
    return valid;
}

int platform_module_pair(u32 *out)
{
    if (!valid) return 0;
    *out = (u32)pm_distribution_pair;
    return 1;
}
