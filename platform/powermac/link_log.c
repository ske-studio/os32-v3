/* Foreground-only LOG adapter. A failed request never consumes the FIFO. */
#include "lan_boot.h"
#include "boot_token.h"
#include "net/link.h"
#include "net/host_mailbox.h"
#include "os32/os32_kapi_shared.h"

#define CHUNK_BYTES 256u
#define REQUEST_BYTES 160u
static i32 handle = -1;
static u8 body[CHUNK_BYTES];
static u32 length, written, generation, loss_flags;
static u64 sequence, records, bytes, lost_records, lost_bytes;
static int pending, connected;

static char *decimal(char *p, u64 value)
{
    /* Shift/subtract avoids a libgcc 64-bit division on PowerPC. */
    char digits[20];
    u32 n = 0;
    do {
        u64 quotient = 0;
        u32 remainder = 0;
        for (int bit = 63; bit >= 0; bit--) {
            remainder = (remainder << 1) | (u32)((value >> bit) & 1);
            if (remainder >= 10) { remainder -= 10; quotient |= (u64)1 << bit; }
        }
        digits[n++] = (char)('0' + remainder);
        value = quotient;
    } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
static u64 sum(u64 a, u64 b)
{
    return b > ~(u64)0 - a ? ~(u64)0 : a + b;
}
static void close_request(void)
{
    if (handle >= 0) (void)link_host_close(handle, 0);
    handle = -1;
    written = 0;
}
void pm_link_log_poll(void)
{
    struct HostAlive alive;
    host_alive_snapshot(&alive);
    int up = link_hello_ok && alive.valid;
    if (connected && (!up || generation != alive.host_generation)) {
        /* Delivery at the old peer's tail is unprovable, even if retried. */
        if (pending) {
            lost_records = sum(lost_records, 1);
            lost_bytes = sum(lost_bytes, length);
        }
        close_request();
    }
    connected = up;
    if (!up) return;
    generation = alive.host_generation;
    if (!pending) {
        const u8 *data;
        u32 flags;
        if (pm_lan_log_peek(&data, &length, &sequence, &records, &bytes, &flags) != LAN_OK)
            return;
        for (u32 i = 0; i < length; i++) body[i] = data[i];
        loss_flags |= flags;
        pending = 1;
    }
    if (handle < 0) {
        static const char hex[] = "0123456789abcdef";
        char request[REQUEST_BYTES], *p = request;
        u8 boot[16];
        boot_token_get(boot);
        *p++ = 'L'; *p++ = 'O'; *p++ = 'G'; *p++ = ' ';
        for (u32 i = 0; i < 16; i++) {
            *p++ = hex[boot[i] >> 4]; *p++ = hex[boot[i] & 15];
        }
        *p++ = ' '; p = decimal(p, sequence);
        *p++ = ' '; p = decimal(p, sum(records, lost_records));
        *p++ = ' '; p = decimal(p, sum(bytes, lost_bytes));
        *p++ = ' '; p = decimal(p, loss_flags);
        *p++ = ' '; p = decimal(p, length);
        handle = link_host_open(request, (u32)(p - request), 0);
        return;
    }
    u32 status;
    i32 rc = link_host_status(handle, &status, 0, 0);
    if (rc != OS32_ERR_AGAIN) {
        if (!rc && status == 200) {
            pm_lan_log_complete(LAN_OK);
            pending = 0;
        }
        close_request();
        return;
    }
    if (written < length) {
        rc = link_host_write(handle, body + written, length - written, 0);
        if (rc > 0) written += (u32)rc;
        else if (rc != OS32_ERR_AGAIN) close_request();
    }
}
