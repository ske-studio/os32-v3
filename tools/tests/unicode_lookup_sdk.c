/* Separate SDK translation unit: its only external reference is the API. */
#include "unicode_lookup_mock.h"
u16 sdk_unicode_to_jis(u32 cp)
{
    return unicode_mock_api->unicode_to_jis(cp);
}
