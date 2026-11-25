#include "system_surface.h"
#include "con_sink.h"
#include "os32_kapi_shared.h"

__attribute__((section(".text.system_surface_source")))
int system_surface_source(u32 role, struct surface_query_source *out)
{
    struct surface_query_source source = {0};
    const struct ledger_surface *sf;
    if (!out) return OS32_ERR_INVAL;
    source.role = role;
    *out = source;
    if (role != LEDGER_ROLE_TVRAM)
        return OS32_ERR_INVAL;
    sf = ledger_surface_find(0, role);
    if (!sf || sf->closing) return OS32_ERR_INVAL;
    source.count = 1;
    source.refs[0] = (struct surface_ref){(u32)(sf - ledger_surfaces), sf->gen};
    source.ready = !con_sink_is_enabled() &&
                   !(gfx_surface_unready & (1U << (sf - ledger_surfaces)));
    *out = source;
    return 0;
}
