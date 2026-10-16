/* Real SfsClient + LAN transport; only NIC, clock and UART observation are fake. */
#include "sfs_lan.h"
#include "serialfs.h"
void vfs_register_fs(VfsOps *ops) { (void)ops; }
#include "os32_kapi_shared.h"

static SfsLan lan;
static SfsClient client;
static SfsLanNic nic;
static u32 ticks;
static int wait_ok, gates;
static u8 result[10 * 1024];
static u16 result_len;

static u32 clock_now(void *ctx) { (void)ctx; return ticks; }
static void clock_idle(void *ctx) { (void)ctx; ticks++; }
static int clock_wait(void *ctx) { (void)ctx; return wait_ok; }
static const SfsIo clock_io = {0, 0, clock_now, clock_idle, clock_wait};

/* A mutant may call the real UART gate API. Observe it, never open a UART. */
void serial_gate_set(int on) { if (on) gates++; }

void test_init(int (*send)(const void *, u32),
               int (*recv)(void *, u32, u32 *), void (*mac)(u8 *))
{
    ticks = 0;
    gates = 0;
    wait_ok = 1;
    nic.send = send; nic.recv = recv; nic.mac = mac;
    sfs_lan_init(&lan, &nic, &clock_io, 0);
    sfs_client_init(&client, &sfs_lan_io, &lan, SFS_LAN_BAUD);
    client.max_payload = SFS_LAN_MAX_PAYLOAD;
}
int test_hello(u32 nonce) { return sfs_client_hello(&client, nonce); }
int test_peer(const char *mac) { return sfs_lan_set_peer(&lan, mac); }
static int read_count(u16 count)
{
    u8 payload[32];
    const u8 *resp;
    u16 len, i;
    int n, rc;
    sfs_put32(payload, 0);
    sfs_put16(payload + 4, count);
    n = sfs_put_path(payload + 6, sizeof(payload) - 6, "/data");
    rc = sfs_client_call(&client, SFS_T_READ, payload, (u16)(6 + n), &resp, &len);
    if (rc) return rc;
    if (len < SFS_STATUS_LEN) return OS32_ERR_IO;
    result_len = len - SFS_STATUS_LEN;
    for (i = 0; i < result_len; i++) result[i] = resp[SFS_STATUS_LEN + i];
    return (int)sfs_get32(resp);
}
int test_read(void) { return read_count(SFS_READ_MAX); }
int test_read_max(void) { return read_count(client.max_payload - SFS_STATUS_LEN); }
const u8 *test_data(void) { return result; }
u32 test_len(void) { return result_len; }
u32 test_ticks(void) { return ticks; }
u32 test_resends(void) { return client.resends; }
u32 test_gates(void) { return (u32)gates; }
u32 test_pending(void) { return lan.rx_end - lan.rx_pos; }
void test_wait(int on) { wait_ok = on; }
void test_tick(void) { ticks++; }
int test_put(void)
{
    u8 buf[SFS_MAX_FRAME];
    u16 n = sfs_encode(buf, SFS_T_BYE, client.sid, 0, 0, 0);
    return sfs_lan_io.put(&lan, buf, n);
}
int test_get(void) { return sfs_lan_io.get(&lan); }

void test_offer(u32 n) { client.max_payload = (u16)n; }
u32 test_limit(void) { return client.max_payload; }
int test_stream(u32 size)
{
    return serialfs_get_ops()->read_stream(&client, "/data", result, size, 0);
}
int test_write(void)
{
    u32 i;
    for (i=0; i<1024; i++) result[i]=(u8)(i%251);
    return serialfs_get_ops()->write_file(&client, "/out", result, 1024);
}
static u32 us_reads;
static int us_fail;
static int clock_us(u32 *lo, u32 *hi)
{
    unsigned long long t = 0xfffffff0ULL + 400ULL * us_reads++;
    if (us_fail) return OS32_ERR_AGAIN;
    *lo = (u32)(t & 0xffffffffULL); *hi = (u32)(t >> 32); return 0;
}
void test_clock(int fail) { us_reads=0; us_fail=fail; client.time_us=clock_us; }
u32 test_samples(void) { return client.rtt_samples; }
u32 test_avg(void) { return client.rtt_samples ? (u32)(client.rtt_total_us/client.rtt_samples) : 0; }
u32 test_max(void) { return client.rtt_max_us; }

u32 test_trial(u32 baud) { return sfs_trial_ticks_limit(baud, client.max_payload); }
