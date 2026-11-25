#include "../../hal/input/keymap.h"
#include "../../hal/input/in2_int.h"
#include "os32_kapi_shared.h"

int pc98_input_keymap_register(u16 *generation)
{
    int src, rc;
    if (!generation) return OS32_ERR_INVAL;
    rc = km_backend_register(&pc98_jp_backend);
    if (rc) return rc;
    rc = km_backend_register(&hid_jp_backend);
    if (rc) return rc;
    src = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, generation);
    if (src <= 0) return src;
    rc = km_source_bind((u8)src, *generation, KM_MAP_PC98);
    if (rc) {
        input_src_release((u8)src, *generation);
        return rc;
    }
    return src;
}
