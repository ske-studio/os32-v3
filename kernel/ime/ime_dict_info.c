/* ime_dict_info (KAPI v80) — FEP side. Registration stub; the dictionary
 * metadata stage owns the body (docs/decisions/fep/DICT_META_DESIGN.md 追記). */
#include "ime.h"

int ime_dict_info_facade(int key, int offset, char *out, int capacity)
{
    (void)key; (void)offset; (void)out; (void)capacity;
    return OS32_ERR_NOSYS;
}
