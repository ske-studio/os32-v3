/* Value-only mock of the appended KAPI member; no table address crosses it. */
#include "types.h"
struct unicode_mock_api {
    u16 (__attribute__((cdecl)) *unicode_to_jis)(u32 cp);
};
extern const struct unicode_mock_api *unicode_mock_api;
u16 sdk_unicode_to_jis(u32 cp);
