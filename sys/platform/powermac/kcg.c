#include "sys/dev/video/kcg/kcg_module.h"
#include "sys/kern/console/font_source_kcg.h"

static const KcgModuleApi *public_api(void)
{
    return &kcg_file_api;
}

#include "sys/dev/video/kcg/kcg_read_provider.inc"

int kcg_module_ready(void)
{
    return kcg_file_ready();
}
