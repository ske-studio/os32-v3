/* ILP32 KAPI -> actual platform -> recording NP2/link substitutes. */
#include "platform_device.h"
#include "platform_host.h"
#include "link.h"
#include "os32_kapi_shared.h"
static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void fail(unsigned line)
{
    char message[] = "FAIL L09 line 0000\n";
    for (int i = 17; i >= 14; i--) { message[i] = '0' + line % 10; line /= 10; }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(message), "d"(sizeof(message) - 1) : "memory");
    die(1);
}
#define CHECK(c) do { if (!(c)) fail(__LINE__); } while (0)
static int calls, owner, expected_owner, exited, present, info_kind;
static int valid = 1, writable = 1, owners;
static i32 result, handle;
static u32 expected_len;
static const void *user_buf;
static u8 stage[4] = {1, 2, 3, 4};
static char host_req_buf[LINK_MAX_PAYLOAD];
static u8 host_wr_buf[LINK_MAX_PAYLOAD];
static void *kmemcpy(void *out, const void *in, u32 len)
{
    for (u32 i = 0; i < len; i++) ((u8 *)out)[i] = ((const u8 *)in)[i];
    return out;
}
int np2_detect(void) { calls++; return present; }
static int info(char *buf, int size, int kind)
{
    CHECK(buf == user_buf && size == (int)expected_len && kind == info_kind);
    calls++;
    if (size > 0) buf[0] = present ? 'N' : 0;
    return present && size > 0 ? 1 : 0;
}
int np2_get_version(char *b, int n) { return info(b, n, 0); }
int np2_get_cpu(char *b, int n) { return info(b, n, 1); }
int np2_get_clock(char *b, int n) { return info(b, n, 2); }
int np2_check_hostdrv(char *b, int n) { return info(b, n, 3); }
static void check_owner(i32 h, int o)
{ CHECK(h == handle && o == expected_owner); calls++; }
i32 link_host_open(const char *req, u32 len, int o)
{
    CHECK(req == host_req_buf && req != user_buf && len == expected_len && o == expected_owner);
    for (u32 i = 0; i < len; i++) CHECK(req[i] == ((const char *)user_buf)[i]);
    calls++; return result;
}
i32 link_host_status(i32 h, u32 *st, u32 *len, int o)
{ check_owner(h, o); *st = 201; *len = 4; return result; }
i32 link_host_read_stage(i32 h, u32 cap, const u8 **out, int o)
{ check_owner(h, o); CHECK(cap == expected_len); *out = stage; return result; }
i32 link_host_write(i32 h, const void *buf, u32 len, int o)
{
    check_owner(h, o);
    CHECK(buf == host_wr_buf && buf != user_buf && len == expected_len);
    u32 n = len > LINK_MAX_PAYLOAD ? LINK_MAX_PAYLOAD : len;
    for (u32 i = 0; i < n; i++) CHECK(((const u8 *)buf)[i] == ((const u8 *)user_buf)[i]);
    return result;
}
i32 link_host_close(i32 h, int o)
{ check_owner(h, o); stage[0] = 99; return result; }
void link_host_owner_exit(int o)
{ CHECK(o == expected_owner); calls++; exited = o; }
static int res_owner_get(void) { owners++; return owner; }
static int ring3_user_range_ok(u32 p, u32 len)
{ (void)p; (void)len; return valid; }
static int ring3_user_ranges_writable(u32 p, u32 n, u32 p2, u32 n2)
{ CHECK(p == (u32)user_buf && n == expected_len && !p2 && !n2); return writable; }
static void ring3_fault_kill(void) { die(writable ? 1 : 0); }
#define KAPI_HIT(n) ((void)(n))
#define KAPI_OUT_LEN_S(p, n) (((int)(n) > 0) ? (u32)(n) : 0u)
#include "wrappers.inc"

void _start(void)
{
    u8 buf[LINK_MAX_PAYLOAD + 4];
    u32 st, len;
    for (u32 i = 0; i < sizeof(buf); i++) buf[i] = (u8)i;
    user_buf = buf;
    for (present = 0; present <= 1; present++) {
        CHECK(wrap_np2_detect() == present);
        for (int n = 0; n < 2; n++) {
            expected_len = n;
            info_kind = 0; wrap_np2_get_version((char *)buf, n);
            info_kind = 1; wrap_np2_get_cpu((char *)buf, n);
            info_kind = 2; wrap_np2_get_clock((char *)buf, n);
            info_kind = 3; CHECK(wrap_np2_check_hostdrv((char *)buf, n) == (present && n ? 1 : 0));
            if (n) CHECK(buf[0] == (present ? 'N' : 0));
        }
    }
    const i32 errors[] = {0, OS32_ERR_AGAIN, OS32_ERR_STALE, OS32_ERR_INVAL};
    const int ids[] = {0, 7, 19, -1};
    for (unsigned j = 0; j < 4; j++) {
        owner = expected_owner = ids[j]; handle = (i32)j - 1;
        for (unsigned e = 0; e < 4; e++) {
            result = errors[e]; calls = owners = 0;
            expected_len = 4;
            CHECK(kapi_host_open((char *)buf, 4) == result);
            st = len = 0xa5;
            CHECK(kapi_host_status(handle, &st, &len) == result);
            CHECK(st == (result ? 0xa5u : 201u) && len == (result ? 0xa5u : 4u));
            CHECK(kapi_host_status(handle, 0, 0) == result);
            CHECK(kapi_host_read(handle, buf, 4) == result);
            CHECK(kapi_host_write(handle, buf, 4) == result);
            CHECK(kapi_host_close(handle) == result);
            CHECK(calls == 6 && owners == 6);
        }
        result = 4; expected_len = 4; stage[0] = 1;
        CHECK(kapi_host_read(handle, buf, 4) == 4);
        CHECK(buf[0] == 1 && buf[3] == 4);
        const u8 *borrowed = 0;
        CHECK(platform_host_read_stage(handle, 4, &borrowed, owner) == 4 && borrowed == stage);
        CHECK(kapi_host_close(handle) == 4 && buf[0] == 1 && borrowed[0] == 99);
        expected_len = sizeof(buf);
        CHECK(kapi_host_write(handle, buf, sizeof(buf)) == 4);
        calls = owners = 0; exited = -99;
        host_owner_exit(owner);
        CHECK(exited == owner && calls == 1 && owners == 0);
    }
    calls = owners = 0; valid = 0; st = len = 0xa5;
    CHECK(kapi_host_open((char *)buf, 4) == OS32_ERR_INVAL);
    CHECK(kapi_host_status(handle, &st, &len) == OS32_ERR_INVAL && st == 0xa5 && len == 0xa5);
    CHECK(kapi_host_read(handle, buf, 4) == OS32_ERR_INVAL);
    CHECK(kapi_host_write(handle, buf, 4) == OS32_ERR_INVAL);
    valid = 1;
    CHECK(kapi_host_open(0, 4) == OS32_ERR_INVAL);
    CHECK(kapi_host_open((char *)buf, 0) == OS32_ERR_INVAL);
    CHECK(kapi_host_open((char *)buf, LINK_MAX_PAYLOAD + 1) == OS32_ERR_INVAL);
    CHECK(kapi_host_read(handle, 0, 4) == OS32_ERR_INVAL);
    CHECK(kapi_host_write(handle, buf, 0) == OS32_ERR_INVAL);
    CHECK(calls == 0 && owners == 0);
    expected_len = 1; writable = 0;
    wrap_np2_get_version((char *)buf, 1); /* must fault before device access */
    die(1);
}
