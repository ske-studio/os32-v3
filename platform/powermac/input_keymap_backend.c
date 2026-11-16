#include "input/hal/keymap.h"

/* Registration failure leaves the existing USB/provider path in use. */
int pm_input_keymap_register(void)
{
    return km_backend_register(&hid_jp_backend);
}
