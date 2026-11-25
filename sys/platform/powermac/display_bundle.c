#include "display_provider.h"
#include "pgalloc.h"

int display_provider_bundle_ok(u32 role, u32 backend)
{
    return role == LEDGER_ROLE_CLIENT && backend == LEDGER_SF_OF_FB;
}
