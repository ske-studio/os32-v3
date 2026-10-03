#include "system_surface.h"
#include "utf8_internal.h"
#include "con_sink.h"
#include "memmap.h"
#include "os32_kapi_shared.h"

int __attribute__((cold)) system_unicode_register(void)
{
    const struct ledger_surface sf = {
        .first = MEM_UNICODE_TABLE_BASE / PAGE_SIZE,
        .npages = MEM_UNICODE_TABLE_SIZE / PAGE_SIZE,
        .width = PAGE_SIZE, .pitch = PAGE_SIZE,
        .height = MEM_UNICODE_TABLE_SIZE / PAGE_SIZE,
        .owner = LEDGER_OWNER_KERNEL, .backing = LEDGER_SB_FIXED_RAM,
        .role = LEDGER_ROLE_UNICODE, .format = LEDGER_FMT_TABLE,
        .planes = 1, .cache = LEDGER_CACHE_WB, .perm_max = LEDGER_PERM_RO
    };
    return ledger_surface_create(&sf, 0);
}

__attribute__((section(".text.system_surface_source")))
int system_surface_source(u32 role, struct surface_query_source *out)
{
    struct surface_query_source source = {0};
    const struct ledger_surface *sf;
    if (!out) return OS32_ERR_INVAL;
    source.role = role;
    *out = source;
    if (role != LEDGER_ROLE_TVRAM && role != LEDGER_ROLE_UNICODE)
        return OS32_ERR_INVAL;
    sf = ledger_surface_find(0, role);
    if (!sf || sf->closing) return OS32_ERR_INVAL;
    source.count = 1;
    source.refs[0] = (struct surface_ref){(u32)(sf - ledger_surfaces), sf->gen};
    source.ready = role == LEDGER_ROLE_UNICODE ? utf8_jis_table_ready() :
                                               !con_sink_is_enabled();
    *out = source;
    return 0;
}
