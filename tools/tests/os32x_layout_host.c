/* D35: real admission predicate; no entry is invoked by rejected headers. */
#include <stdio.h>
#include <string.h>
#include "os32_kapi_shared.h"
/* Compile the same loader predicate as protocol 1 for the reverse direction. */
#ifdef OS32_TEST_OLD_SHLIB
#undef OS32_SHLIB_PROTOCOL
#define OS32_SHLIB_PROTOCOL 1UL
#endif
#include "../../exec/os32x_hdr.c"
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL: %s\n", #x); failures++; } } while (0)
static OS32Header valid(void)
{
    OS32Header h;
    memset(&h, 0, sizeof(h));
    h.magic = OS32X_MAGIC; h.version = OS32X_HDR_VERSION;
    h.header_size = OS32X_HDR_SIZE; h.kapi_data_off = KAPI_DATA_FIELDS_OFF;
    h.kapi_abi_generation = OS32_KAPI_ABI_GENERATION;
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    return h;
}
int main(void)
{
    OS32Header h = valid();
    u32 i;
    {
        u8 wire[OS32X_HDR_SIZE + 1];
        OS32Header decoded;
        for (i = 0; i < OS32X_HDR_SIZE; i++) wire[i + 1] = (u8)(i + 1);
        CHECK(os32x_decode(&decoded, wire + 1, OS32X_HDR_SIZE - 1) == OS32X_LAYOUT_SHORT);
        CHECK(os32x_decode(&decoded, wire + 1, OS32X_HDR_SIZE) == OS32X_LAYOUT_OK);
        CHECK(decoded.magic == 0x04030201u);
        CHECK(decoded.header_size == 0x08070605u);
        CHECK(decoded.version == 0x0c0b0a09u);
        CHECK(decoded.flags == 0x100f0e0du);
        CHECK(decoded.entry_offset == 0x14131211u);
        CHECK(decoded.text_size == 0x18171615u);
        CHECK(decoded.bss_size == 0x1c1b1a19u);
        CHECK(decoded.heap_size == 0x201f1e1du);
        CHECK(decoded.stack_size == 0x24232221u);
        CHECK(decoded.min_api_ver == 0x28272625u);
        CHECK(decoded.load_addr == 0x2c2b2a29u);
        CHECK(decoded.kapi_data_off == 0x302f2e2du);
        CHECK(decoded.kapi_abi_generation == 0x34333231u);
        CHECK(decoded.memory_layout_generation == 0x38373635u);
        CHECK(decoded.shlib_protocol == 0x3c3b3a39u);
        CHECK(decoded.code_size == 0x403f3e3du);
    }
    CHECK(OS32X_HDR_VERSION == 5);
    CHECK(OS32X_HDR_SIZE == 64 && sizeof(h) == 16 * sizeof(u32));
#if __SIZEOF_LONG__ == 4
    CHECK(sizeof(h) == 64);
#endif
    h.version = 4; h.header_size = 60;
    CHECK(os32x_layout_check(&h, 60, KAPI_DATA_FIELDS_OFF));
    h.header_size = 64;
    CHECK(os32x_layout_check(&h, 64, KAPI_DATA_FIELDS_OFF));
    h = valid();
    CHECK(OS32_MEMORY_LAYOUT_GENERATION == 4);
#ifndef OS32_TEST_OLD_SHLIB
    CHECK(OS32_SHLIB_PROTOCOL == 2);
    h.shlib_protocol = 1; /* app 1 x shlib 2 */
#else
    h.shlib_protocol = 2; /* app 2 x shlib 1 */
#endif
    CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF) == OS32X_LAYOUT_MISMATCH);
    h.flags = OS32X_FLAG_SHLIB;
    CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF) == OS32X_LAYOUT_MISMATCH);
    h = valid();
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION - 1;
    CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF) == OS32X_LAYOUT_MISMATCH);
    h.flags = OS32X_FLAG_SHLIB;
    CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF) == OS32X_LAYOUT_MISMATCH);
    h = valid();
    CHECK(sizeof(h) / sizeof(u32) == OS32X_HDR_SIZE / 4);
    CHECK(!os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    for (i = 0; i < OS32X_HDR_SIZE; i++) CHECK(os32x_layout_check(&h, i, KAPI_DATA_FIELDS_OFF));
    h.version++; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h.version = OS32X_HDR_VERSION - 1; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.header_size++; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.flags = 0x0004 /* retired flag must be rejected */; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.flags = 0x8000; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.kapi_abi_generation++; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.memory_layout_generation++; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.shlib_protocol = OS32_SHLIB_PROTOCOL + 1; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.flags = OS32X_FLAG_SHLIB; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h.shlib_protocol = OS32_SHLIB_PROTOCOL; CHECK(!os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.kapi_data_off++; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.min_api_ver = KAPI_VERSION - 1; CHECK(!os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    h = valid(); h.min_api_ver = KAPI_VERSION + 1; CHECK(os32x_layout_check(&h, sizeof(h), KAPI_DATA_FIELDS_OFF));
    CHECK(os32x_layout_check(NULL, sizeof(h), KAPI_DATA_FIELDS_OFF));
    return failures ? 1 : 0;
}
