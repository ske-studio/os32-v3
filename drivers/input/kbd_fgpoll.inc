/* CUI cooperative foreground point (GF_GUARD_THERM_DESIGN §5, F2).
 * nouveau nvkm/subdev/therm/temp.c:162-208 polls independently of drawing.
 * OS32 uses foreground waits/tries instead of the alarm worker. The resident
 * adapter owns module lifetime, reentry and deadlines; keep none of them here.
 * Included after kbd_gui_mode, outside IRQ and ring dequeue critical sections. */
#include "../video/geforce/geforce_module.h"

static void kbd_foreground_poll(void)
{
    if (!kbd_gui_mode)
        geforce_foreground_poll();
}
