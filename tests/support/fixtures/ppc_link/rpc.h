/* Fixture-only observation points. Exchange identity survives transport close. */
enum { RPC_POLL = 1, RPC_CLIENT, RPC_IDLE, RPC_OTHER };
enum { RPC_OPEN = 1, RPC_WDATA = 2, RPC_RELEASE = 4 };
u32 rpc_start(u32 now, u32 width);
void rpc_begin(u32 deadline);
void rpc_frame(void *ctx, const u8 *buf, u32 len, u32 deadline);
void rpc_accept(void *ctx, const u8 *buf, u32 len, u32 deadline);
void rpc_opened(int handle);
void rpc_poll(u32 tick, int can_wait);
u32 rpc_compare(u32 now, u32 source);
void rpc_timeout(void *ctx, const u8 *buf, u32 len, u32 deadline);
void rpc_close(int handle, u32 reason);
void rpc_return(void *ctx, const u8 *buf, u32 len, u32 deadline);
