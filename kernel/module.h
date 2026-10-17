#ifndef KERNEL_MODULE_H
#define KERNEL_MODULE_H

#include "types.h"
#include "os32_kapi_shared.h"

#define KDEBUG_OPS_VERSION 2U

/* Wire slots 1..18 follow version; 17 is reserved until LB-4. */
struct kdebug_ops {
    u32 version;
    void (*selftest_run)(void);
    void (*selftest_post_exec)(void);
    void (*selftest_post_unicode)(void);
    void (*selftest_post_probe)(void);
    void (*crt_diag_bootlog)(void);
    int (*gcap_capture)(int mode, V86Gcap *out);
    int (*gcap_active)(void);
    int (*gcap_keep_if)(void);
    int (*gcap_insn_abort)(int opsize16, u8 op);
    int (*gcap_port_passed)(u16 port);
    int (*gcap_pass_in)(u16 port, int size, u32 *value);
    int (*gcap_pass_out)(u16 port, int size, u32 value);
    void (*gcap_note_in)(u16 port, int size, u32 value);
    void (*gcap_note_out)(u16 port, int size, u32 value);
    void (*gcap_detach)(void);
    void (*gcap_release)(void);
    void (*nic_selftest)(void);
    void (*nic_trace)(u32 stage); /* Normal context only: uses VFS. */
};

struct kdebug {
    u32 state, pair_crc, base, base_pa, pages, owner, open_count;
    struct kdebug_ops ops;
};
extern struct kdebug kdebug;

/* Boot only, after dma_pool_init; cooperative, normal kernel context. */
void kdebug_load(void);
const char *kdebug_state_name(int state);
int kdebug_init(struct kdebug_ops *candidate);

enum kdebug_state {
    KDEBUG_OFF = 0, KDEBUG_RUNNING = 1, KDEBUG_NOT_FOUND = 2,
    /* 3..6 and 9/11/12 come from module_image_result. */
    KDEBUG_NO_PAGES = 7, KDEBUG_SHORT_READ = 8, KDEBUG_INIT_FAILED = 10
};
#endif
