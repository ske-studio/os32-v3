/* KAPI slot 239 (v86_gdc_capture) の受け口。記録器の本体は起動時に読む部品
 * (kdebug/v86_gcap.c)。部品が無ければ OS32_ERR_NOSYS。 */
#include "v86_gcap.h"
#include "module.h"

int v86_gdc_capture(int mode, V86Gcap *out)
{
    return kdebug.ops.gcap_capture ? kdebug.ops.gcap_capture(mode, out)
                                  : OS32_ERR_NOSYS;
}
