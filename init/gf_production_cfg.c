#include "kstring.h"
#include "gf_production.h"

static int gf_selected;
static int gf_configured;
void gf_production_configure(const char *gfx, int gui)
{
    if (gf_configured) return;
    gf_configured = 1;
    gf_selected = gfx && !kstrcmp(gfx, "geforce") && gui != 1;
}
int gf_production_selected(void)
{
    return gf_selected;
}
