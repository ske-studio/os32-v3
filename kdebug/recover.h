#ifndef KDEBUG_RECOVER_H
#define KDEBUG_RECOVER_H

#include "types.h"
#define PCI_DECODE_CONSTANTS_ONLY
#include "../drivers/pci_decode.h"
#undef PCI_DECODE_CONSTANTS_ONLY
#include <stdbool.h>

/* DESIGN.final §3–5, §7 T1/T2/T3.
 * All callbacks are synchronous, bounded, and return 0 on success. No callback
 * may dispatch shell commands. A step must call recover_safe_point at each
 * poll/delay/instruction boundary. The owner initializes once per boot; FAILED
 * and cold-required cannot be cleared by starting a new session.
 */
/* GeForce 6200 (NV44A) */
#define KPEEK_NV_VENDOR      PCI_VENDOR_NVIDIA
#define KPEEK_NV44A_DEVICE   PCI_DEVICE_NV44A
#define KPEEK_NV_BAR_MMIO    0U       /* BAR0 = レジスタ空間 16MB */

/* BAR0 内のオフセット (nouveau / envytools の名前) と長さ */
#define KPEEK_NV_PMC_OFF        0x000000U   /* PMC: ID・有効化 */
#define KPEEK_NV_PMC_LEN        0x000400U
#define KPEEK_NV_PBUS_OFF       0x001800U   /* PBUS: PCI 設定の写し */
#define KPEEK_NV_PBUS_LEN       0x000100U
#define KPEEK_NV_PFB_OFF        0x100000U   /* PFB: メモリ制御 */
#define KPEEK_NV_PFB_LEN        0x001000U
#define KPEEK_NV_PSTRAPS_OFF    0x101000U   /* PEXTDEV の strap */
#define KPEEK_NV_PSTRAPS_LEN    0x000010U
#define KPEEK_NV_PCRTC0_OFF     0x600000U   /* PCRTC head 0 */
#define KPEEK_NV_PCRTC1_OFF     0x602000U   /* PCRTC head 1 */
#define KPEEK_NV_PCRTC_LEN      0x001000U
#define KPEEK_NV_PRAMDAC0_OFF   0x680000U   /* PRAMDAC head 0 */
#define KPEEK_NV_PRAMDAC1_OFF   0x682000U   /* PRAMDAC head 1 */
#define KPEEK_NV_PRAMDAC_LEN    0x001000U
#define KPEEK_NV_PROM_OFF       0x300000U   /* PROM: VBIOS の窓 */
#define KPEEK_NV_PROM_LEN       0x020000U
#define KPEEK_NV_PRMVIO0_OFF    0x0C0000U   /* NV_PRMVIO0_OFFSET */
#define KPEEK_NV_PRMCIO0_OFF    0x601000U   /* NV_PRMCIO0_OFFSET */
#define KPEEK_VGA_MISC_W        0x3C2U      /* NV_PRMVIO_MISC__WRITE */
#define KPEEK_VGA_SRX           0x3C4U      /* NV_PRMVIO_SRX */
#define KPEEK_VGA_SR            0x3C5U      /* NV_PRMVIO_SR */
#define KPEEK_VGA_MISC_R        0x3CCU      /* NV_PRMVIO_MISC__READ */
#define KPEEK_VGA_GRX           0x3CEU      /* NV_PRMVIO_GRX */
#define KPEEK_VGA_GX            0x3CFU      /* NV_PRMVIO_GX */
#define KPEEK_VGA_CRX           0x3D4U      /* NV_PRMCIO_CRX__COLOR */
#define KPEEK_VGA_CR            0x3D5U      /* NV_PRMCIO_CR__COLOR */
#define KPEEK_VGA_BYTE_LEN      1U

/* Fixed NV44A at PCI 0:13.0; PCI IDs and bit layouts live in pci_decode.h.
 * Keep RECOVER aliases used by the M5 script/PLL implementation. VGA/NV
 * addresses are canonical here, shared with the kpeek read allowlist. */
#define RECOVER_GF_BUS 0U
#define RECOVER_GF_DEV 13U
#define RECOVER_GF_FN 0U
#define RECOVER_GF_BDF (RECOVER_GF_DEV << 3)
#define RECOVER_GF_DEVICE ((PCI_DEVICE_NV44A << 16) | PCI_VENDOR_NVIDIA)
#define RECOVER_GF_PMC_IRQ 0x0140U
#define RECOVER_PMC_ENABLE 0x200U
#define RECOVER_PMC_ENABLE_BIT0 1U
#define RECOVER_PBUS184C 0x184cU
#define RECOVER_CR11_PROTECT 0x80U
#define RECOVER_CR11 0x11U
#define RECOVER_BAR_COUNT 3
#define RECOVER_HEAD_COUNT 2U

/* UART 8N1: start + eight data bits + stop, and time conversion units. */
#define RECOVER_UART_FRAME_BITS 10U
#define RECOVER_UART_US 1000000U
#define RECOVER_UART_FRAME_US (RECOVER_UART_FRAME_BITS * RECOVER_UART_US)
#define RECOVER_US_PER_MS 1000U

/* Published S0 / PREPARE-RUN / SIZE_BARS budget envelopes. */
#define RECOVER_BUDGET_BYTES_S0 4096U
#define RECOVER_BUDGET_BYTES_RUN 8192U
#define RECOVER_BUDGET_BYTES_SIZE_BARS 16384U
#define RECOVER_BUDGET_EXEC_MS_S0 2000U
#define RECOVER_BUDGET_EXEC_MS_RUN 4000U
#define RECOVER_BUDGET_EXEC_MS_SIZE_BARS 6000U
#define RECOVER_BUDGET_SECONDS_S0 10U
#define RECOVER_BUDGET_SECONDS_RUN 18U
#define RECOVER_BUDGET_SECONDS_SIZE_BARS 33U
#define RECOVER_BUDGET_POLLS_S0 16384U
#define RECOVER_BUDGET_POLLS_RUN 32768U
#define RECOVER_BUDGET_POLLS_SIZE_BARS 65536U
#define RECOVER_BUDGET_S0_WAIT_MS RECOVER_BUDGET_EXEC_MS_S0
#define RECOVER_BUDGET_S0_POINTS 256U
#define RECOVER_BUDGET_MARGIN_S 3U
#define RECOVER_BUDGET_POINTS 1024U
#define RECOVER_BUDGET_TRIAL_SECONDS 60U
#define RECOVER_BUDGET_STEPS 4096U
#define RECOVER_BUDGET_POLLS RECOVER_BUDGET_POLLS_SIZE_BARS
/* Validated maximum GF cleanup candidate, including UART busy polls. */
#define RECOVER_CLEANUP_TRANSPORT_POLLS 1048576U

#define RECOVER_MAX_REGS 32U
#define RECOVER_PROFILE_VERSION 4U
#define RECOVER_PROFILE_VPLL_VERSION 0x56504c04U
#define RECOVER_VPLL_SWITCH_OFF 0x68050cU
#define RECOVER_VPLL_SWITCH_ON 0x204U
#define RECOVER_VPLL_SWITCH_DEFAULT 0x4U
#define RECOVER_VPLL_STAGE2_FIRST 0x12045U
#define RECOVER_VPLL_REPEAT_FIRST 0x12046U
#define RECOVER_VPLL_REPEAT_LAST 0x1204aU
#define RECOVER_VPLL_STAGE2_LAST 0x1204bU
#define RECOVER_VPLL_MAIN 2U
#define RECOVER_VPLL_PC 0xe623U
#define RECOVER_VPLL_FIRST 0x12040U
#define RECOVER_VPLL_LAST 0x12044U
#define RECOVER_VPLL_ROWS 20U
#define KPEEK_NV_VPLL_CONTROL_OFF 0x00c040U
#define KPEEK_NV_VPLL_CONTROL_LEN 4U
#define KPEEK_NV_CORE_PLL_OFF 0x004000U
#define KPEEK_NV_MEM_PLL_OFF 0x004020U
#define KPEEK_NV_PLL_LEN 8U
#define RECOVER_HALF_TICKS 0x80000000UL
#define RECOVER_ROUTE_PORT 0x0FACU
#define RECOVER_ROUTE_MASK 0x03U
#define RECOVER_ROUTE_98 0U
#define RECOVER_COMMAND_MEM PCI_CMD_MEM_ENABLE
#define RECOVER_COMMAND_DMA_MEM (PCI_CMD_MEM_ENABLE | PCI_CMD_BUS_MASTER)

enum recover_state {
    REC_IDLE, REC_SNAPSHOTTING, REC_ARMED, REC_RUNNING, REC_PAUSED,
    REC_RESTORING, REC_VERIFIED, REC_FAILED
};
enum recover_reason {
    REC_NORMAL, REC_ABORT, REC_TIMEOUT, REC_OPERATION, REC_CONTRACT,
    REC_SNAPSHOT, REC_CONFIG, REC_BUS, REC_VERIFY, REC_LOG
};
enum recover_kind { REC_RW, REC_COLD, REC_W1C, REC_UNCLASSIFIED };
enum recover_phase { REC_PHASE_NONE, REC_PHASE_SAVE, REC_PHASE_QUIESCE,
    REC_PHASE_RESTORE, REC_PHASE_ROUTE, REC_PHASE_SCREEN, REC_PHASE_VERIFY };
enum recover_request { REC_REQUEST_NOW, REC_REQUEST_STATUS };

/* Target IDs are shared by snapshot, plan, guards and cleanup. */
#define RECOVER_NONE 0xffffffffU
#define RECOVER_TARGET_MAX 1024U
#define RECOVER_RULE_MAX 2048U
#define RECOVER_LINK_MAX 8U
#define RECOVER_NODE_MAX 2048U
#define RECOVER_ARG_MAX 8192U
#define RECOVER_CTX_MAX 2048U
#define RECOVER_DEP_MAX 64U
#define RECOVER_ENTRY_MAX 32U
/* Preserve the existing unsigned literal spelling in the preprocessed HWDEV
 * contract while taking the word width from types.h's single definition. */
#define RECOVER_UNSIGNED_(n) n##U
#define RECOVER_UNSIGNED(n) RECOVER_UNSIGNED_(n)
#define RECOVER_BITS_WORDS (RECOVER_NODE_MAX / RECOVER_UNSIGNED(BITS_PER_U32))
#define RECOVER_RANGE_MAX 256U
#define RECOVER_EXPR_ARGS 32U
#define RECOVER_PRMVIO KPEEK_NV_PRMVIO0_OFF
#define RECOVER_PRMCIO KPEEK_NV_PRMCIO0_OFF
#define RECOVER_HEAD_STRIDE (KPEEK_NV_PCRTC1_OFF - KPEEK_NV_PCRTC0_OFF)
#define RECOVER_WINDOW_PAGES 2U /* a peek chunk (< one page) spans at most two PTEs */
#define RECOVER_CFG_SIZE PCI_CFG_SPACE_SIZE
#define RECOVER_COMMAND PCI_CFG_COMMAND
#define RECOVER_COMMAND_OVERRIDE 0x0102U
#define RECOVER_CFG50 0x50U
#define RECOVER_CFG54 0x54U
#define RECOVER_PBUS1850 0x1850U
#define RECOVER_PBUS1854 0x1854U
#define RECOVER_SEQ KPEEK_VGA_SRX
#define RECOVER_GFX KPEEK_VGA_GRX
#define RECOVER_CRTC_MONO 0x3b4U
#define RECOVER_STATUS_MONO 0x3baU
#define RECOVER_ATTR 0x3c0U
#define RECOVER_FEATURE_READ 0x3caU
#define RECOVER_STATUS 0x3daU
#define RECOVER_CRTC KPEEK_VGA_CRX
#define RECOVER_MISC_WRITE KPEEK_VGA_MISC_W
#define RECOVER_MISC_READ KPEEK_VGA_MISC_R
#define RECOVER_VGA_ENABLE 0x3c3U
#define RECOVER_DAC_DATA 0x3c9U
#define RECOVER_IO_LAST 0xffffU
#define RECOVER_CR1F 0x1fU
#define RECOVER_CR1F_UNLOCKED 3U
#define RECOVER_CR1F_UNLOCK 0x57U
#define RECOVER_CR1F_LOCK 0x99U
#define RECOVER_CR44 0x44U
#define RECOVER_SR01 1U
#define RECOVER_SR_BLANK 0x20U
#define RECOVER_MAIN_MAX 4U
#define RECOVER_N1_MAIN 3U
#define RECOVER_CR44_HEAD0 0U
#define RECOVER_CR44_HEAD1 3U
#define RECOVER_N1_CFG50_BIT (1U << 0)
#define RECOVER_N1_CFG54_BIT (1U << 1)
#define RECOVER_N1_PBUS1850_BIT (1U << 2)
#define RECOVER_N1_PBUS1854_BIT (1U << 3)
#define RECOVER_N1_ALL_BITS (RECOVER_N1_CFG50_BIT | RECOVER_N1_CFG54_BIT | \
                             RECOVER_N1_PBUS1850_BIT | RECOVER_N1_PBUS1854_BIT)
#define RECOVER_BAR_TYPE_MASK PCI_BAR_TYPE_MASK
#define RECOVER_N1_PC 0xe81eU
#define RECOVER_BAR_MASK PCI_BAR_MEM_ADDR_MASK
#define RECOVER_ADDRESS_END (1ULL << 32)

enum recover_space { REC_PCI_CFG, REC_BAR_MMIO, REC_BAR_VRAM,
    REC_PC98_IO, REC_VGA_DIRECT, REC_VGA_INDEXED };
enum recover_auth_phase { REC_SIZE_BARS, REC_PREPARE, REC_RUN, REC_VRAM_TEST,
    REC_QUIESCE, REC_RESTORE, REC_ROUTE, REC_SCREEN, REC_VERIFY_PHASE };
enum recover_direction { REC_READ, REC_WRITE, REC_SELECT, REC_DATA };
enum recover_value_mode { REC_LITERAL, REC_SET, REC_RANGE, REC_EXPR, REC_SAVED };
enum recover_arg_kind { REC_IMM, REC_ARG_READ, REC_ARG_SAVED };
enum recover_guard_when { REC_BEFORE = 1, REC_AFTER = 2 };
enum recover_rule_flag { REC_N1 = 1, REC_LOCAL = 2, REC_FORCE = 4 };
struct recover_target {
    u32 space, bar, offset, head, port, index, width, access, read_safe;
};
struct recover_value {
    u32 mode, mask, bits, min, max, expr_id;
    const u32 *allowed; u32 allowed_count;
};
struct recover_guard { u32 key, mask, expected, when; };
struct recover_rule {
    u32 id, key, phase, main_id, pc, substep, write_mask, verify_mask;
    struct recover_value value;
    const u32 *requires; u32 requires_count;
    const struct recover_guard *guards; u32 guard_count;
    u32 flags;
};
struct recover_expr {
    u32 id, argc; const u32 *kinds;
    int (*eval)(const u32 *args, u32 argc, u32 *value);
};
#define REC_LAYOUT_SKIP_HEAD 1U
#define REC_LAYOUT_CHECK_EXPR 2U
struct recover_opcode_layout {
    u32 opcode, substep, argc; const u32 *kinds; u32 dir_substep; u32 flags;
};
struct recover_plan_arg { u32 kind, value; };
struct recover_plan_node {
    u32 phase, main_id, pc, opcode, substep, ctx, head, skip;
    u32 rule_id, expr_id, arg_first, arg_count, next[2];
};
struct recover_plan_dep { u32 consumer, required_rule, producer; };
struct recover_plan_entry { u32 phase, main_id, first; };
#define RECOVER_CAPTURE_MAX 32U
STATIC_ASSERT(RECOVER_CAPTURE_MAX <= 32U, recover_capture_bits);
struct recover_capture { u32 slot, producer_node, consumer_node, target, read_ordinal; };
struct recover_plan {
    u32 version, profile_crc, rom_crc, rom_size;
    const struct recover_plan_node *nodes; u32 node_count;
    const struct recover_plan_arg *args; u32 arg_count;
    u32 ctx_count;
    const struct recover_plan_dep *deps; u32 dep_count;
    const struct recover_plan_entry *entries; u32 entry_count;
    const struct recover_capture *captures; u32 capture_count;
    u32 unresolved_count; /* decoder inventory is not executable permission */
};
struct recover_plan_buffers { u32 *flow_bits, *valid_bits; };
struct recover_bar_extent { u32 raw, base, length, valid; };
struct recover_range { u32 base, len; };
/* key remains a target ID. Resolved offset is BAR-relative (or cfg/I/O).
 * Bus must use this address, never infer SELECT/DATA from previous calls. */
struct recover_bus_access { u32 key, dir_substep, space, bar, offset, width; };
struct recover_reg {
    u32 key, width, write_mask, verify_mask;
    enum recover_kind kind;
    bool read_safe;
    u32 restore_rule_id;
};
struct recover_write { u32 key, width, value, verify_mask, rule_id; };
struct recover_identity {
    u32 boot, image_version, kdebug_version, bdf, device, subsys, revision, bar0;
};
struct recover_bar_measurement {
    struct recover_identity identity;
    u32 measurement_session;
    struct recover_bar_extent bar[RECOVER_BAR_COUNT];
};
struct recover_profile {
    u32 version, crc;
    u32 expected_bdf, expected_device; /* 0/0: synthetic generic fixture */
    const struct recover_reg *regs; /* restoration order, not reverse order */
    u32 count;
    const struct recover_write *quiesce;
    u32 quiesce_count;
    u32 trial_limit, opcode_limit, restore_limit, max_steps, max_polls;
    bool cold_required;
    const struct recover_target *targets; u32 target_count;
    const struct recover_rule *rules; u32 rule_count;
    const struct recover_expr *exprs; u32 expr_count;
    const struct recover_opcode_layout *layouts; u32 layout_count;
    u32 restore_max_polls, s0_reserve_ticks, s0_reserve_polls;
    u32 restore_log_bytes, s0_reserve_bytes;
    /* VRAM reservation is a BAR-relative half-open interval, not capacity. */
    struct recover_range vram_reserved;
    const u32 *index_save_targets; u32 index_save_count;
};
struct recover_token {
    u32 session, profile_crc, profile_version, snapshot_crc, plan_hash;
    struct recover_identity identity;
};
struct recover_local_saved { u32 key, dir_substep, value; };
#define RECOVER_ABORT_SESSION_OFF 2U
#define RECOVER_ABORT_CRC_OFF 6U
#define RECOVER_ABORT_SIZE 10U
#define RECOVER_ABORT_SYNC0 0xa5U
#define RECOVER_ABORT_SYNC1 0x5aU
#define RECOVER_RX_LIMIT 64U
#define RECOVER_RX_DRAIN 4096U
#define RECOVER_ROW_MAX 384U
struct recover_transport;
struct recover;
struct recover_ops {
    u32 (*ticks)(void *opaque);
    int (*identity)(void *opaque, struct recover_identity *out);
    int (*start_ready)(void *opaque); /* stopped device AND confirmed CUI */
    int (*raw_config)(void *opaque, u32 *command, u32 bar[RECOVER_BAR_COUNT]);
    int (*read)(void *opaque, const struct recover_bus_access *a, u32 *value);
    int (*write)(void *opaque, const struct recover_bus_access *a, u32 value);
    int (*route_read)(void *opaque, u32 *value); /* 0FACh byte */
    int (*route_write)(void *opaque, u32 value);
    /* stop output/DMA, release AND verify decode independently of MMIO.
     * Failure prohibits display/VRAM accesses. Shared HAL implementation T2. */
    int (*screen_safe)(void *opaque);
    int (*screen_restore_verify)(void *opaque);
    int (*abort_requested)(void *opaque, u32 session); /* bounded serial poll */
    /* Sole owner of local PTE/IRQ/index/window release; idempotent. Uses
     * recover_access(NONE, ...) for classified local writes. Failure is fatal.
     * Operation returns immediately after any nonzero accessor/safe-point. */
    int (*local_cleanup)(void *opaque);
    int (*event)(void *opaque, enum recover_state state, u32 session);
};
struct recover {
    const struct recover_ops *ops;
    const struct recover_transport *transport;
    const struct recover_profile *s0_profile;
    u8 rx[RECOVER_ABORT_SIZE];
    u32 rx_used, discarded_bytes, seq, lost_lines;
    bool emitting, log_partial, s0_only;
    void *opaque;
    const struct recover_profile *profile;
    struct recover_token token;
    u32 saved[RECOVER_MAX_REGS], saved_count;
    struct recover_local_saved local_saved[RECOVER_MAX_REGS];
    u32 local_saved_count;
    u32 next_session, budget, spent, call_tick, opcode_tick, restore_tick;
    u32 steps, polls, first_key;
    /* GF manifest-derived UART budget; zero preserves the generic budget. */
    u32 log_limit, tx_ticks, opcode_spent;
    u32 restore_polls, uart_polls, uart_bytes;
    u32 restored_rules[RECOVER_RULE_MAX / RECOVER_UNSIGNED(BITS_PER_U32)];
    u32 command_saved, command_actual;
    bool in_s0, command_override;
    enum recover_state state;
    enum recover_reason reason;
    enum recover_phase first_phase;
    const struct recover_plan *plan;
    const struct recover_profile *bound_profile;
    struct recover_plan_buffers buffers;
    u32 bound_hash, cursor, expected_node, head, auth_phase;
    bool entered, accessed, zero_hold, gpu_written, measuring;
    const struct recover_opcode_layout *node_layout[RECOVER_NODE_MAX];
    const struct recover_rule *node_write[RECOVER_NODE_MAX];
    u32 index_port[RECOVER_TARGET_MAX], port_selected[RECOVER_TARGET_MAX];
    u32 capture_values[RECOVER_CAPTURE_MAX], capture_valid;

    struct recover_bar_measurement cache, measurement;
    struct recover_bar_extent bars[RECOVER_BAR_COUNT];
    bool enabled, route_write_approved, valid, in_call, in_opcode;
    bool failed, access_blocked, cold_latched, s0_verified, s1_verified;
};

/* profile_ok: format only; bind validates every path; begin checks extents. */
int recover_profile_ok(const struct recover_profile *p);
int recover_target_address(const struct recover_target *t, u32 dir,
                           struct recover_bus_access *a);
int recover_bind_plan(struct recover *r, const struct recover_profile *p,
    const struct recover_plan *plan, struct recover_plan_buffers *buf, u32 *hash);
int recover_plan_enter(struct recover *r, u32 node);
int recover_plan_leave(struct recover *r, u32 edge);
/* Evaluate checked operands once and perform the current node access. */
int recover_plan_access(struct recover *r, u32 *value);
int recover_plan_check(struct recover *r, u32 *value);
int recover_access(struct recover *r, u32 node, u32 dir_substep,
                   u32 target, u32 width, u32 *value);
int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask,
    const struct recover_range *exclude, u32 exclude_count,
    const struct recover_range *upstream, u32 upstream_count);
u32 recover_profile_crc(const struct recover_profile *p);
void recover_init(struct recover *r, const struct recover_ops *ops, void *opaque,
                  bool enabled, bool route_write_approved);
int trial_begin(struct recover *r, const struct recover_profile *profile,
                u32 plan_hash, u32 budget);
/* approved means the caller has obtained the next-stage approval. */
int trial_step(struct recover *r, const struct recover_token *token, bool approved,
               int (*operation)(struct recover *r, void *arg), void *arg);
int recover_safe_point(struct recover *r);
/* One bounded unit, including waits inside synchronous callbacks. In restore,
 * the local/① segment cannot consume the independent ② reservation. */
int recover_restore_point(struct recover *r);
int recover_delay_ms(struct recover *r, u32 ms,
                     int (*wait_us)(void *, u32), void *opaque);
struct recover_uart_ops {
    int (*ready)(void *opaque); /* negative error, 0 busy, 1 ready */
    int (*put)(void *opaque, u32 byte);
    int (*wait_frame)(void *opaque, u32 frame_us);
};
struct recover_transport {
    int (*get)(void *opaque); /* nonblocking, -1 means empty */
    struct recover_uart_ops uart;
    void *opaque;
    u32 frame_us, max_polls;
    int (*begin)(void *opaque);
    /* Always release ownership; on success first flush under r's budget. */
    int (*end)(struct recover *r, void *opaque, int row_rc);
};
struct recover *recover_owner(void);
/* NULL ops detaches trial references to the original S0 callbacks at IDLE or
 * terminal state; preserves result, session, FAILED and cold latches. */
int recover_owner_configure(const struct recover_ops *ops, void *opaque,
    const struct recover_profile *s0_profile, bool route_write_approved);
int recover_now_session(struct recover *r, u32 session);
int recover_receive(struct recover *r);
/* Shared REC sequence for future trial opcode/window records too. */
int recover_record(struct recover *r, const char *phase, const char *event,
                   u32 step, u32 addr, int rc, u32 elapsed);
int recover_gf_pre(struct recover *r, const struct recover_plan_node *node,
                   const char *event, bool executing, int rc);
int recover_gf_result(struct recover *r, u32 saved_words, bool minimal,
                      bool capacity_known, bool full_capacity, u32 capacity_bytes,
                      u32 tested_bytes);
int recover_uart_flush(struct recover *r, int (*empty)(void *), void *opaque,
                       u32 frame_us, u32 max_polls);
int recover_uart_byte(struct recover *r, const struct recover_uart_ops *ops,
                      void *opaque, u32 byte, u32 frame_us, u32 max_polls);

int trial_end(struct recover *r, const struct recover_token *token,
              enum recover_reason reason);
int recover_request(struct recover *r, const struct recover_token *token,
                    enum recover_request request);
/* T2 primitives: accessors are injectable; only local_cleanup owns release.
 * T2 maps one aligned datum per short window, peek one chunk (kdebug_window);
 * IRQs never span a script or wait. */
struct recover_window_ops {
    u32 (*irq_save)(void *opaque);
    void (*irq_restore)(void *opaque, u32 flags);
    u32 (*pte_read)(void *opaque, u32 va);
    int (*map)(void *opaque, u32 va, u32 pa, u32 pages, u32 flags);
    int (*unmap)(void *opaque, u32 va, u32 raw);
    int (*load)(void *opaque, u32 va, u32 width, u32 *value);
    int (*store)(void *opaque, u32 va, u32 width, u32 value);
};
struct recover_window {
    const struct recover_window_ops *ops;
    void *opaque;
    u32 va, raw[RECOVER_WINDOW_PAGES], flags, pages;
    bool owned, failed;
};
void recover_window_hal_init(struct recover_window *w);
int recover_window_cleanup(struct recover_window *w);
int recover_window_access(struct recover_window *w, u32 pa, u32 width,
                          bool write, u32 *value);

/* Common 98 text proof, after screen_safe has independently verified decode.
 * cell is one explicitly reserved unused TVRAM word, not a general write API.
 * The caller supplies the reservation and confirms CUI before any write. */
struct recover_screen_ops {
    int (*cui)(void *opaque);
    int (*safe)(void *opaque);
    int (*restore)(void *opaque);
    int (*read_cell)(void *opaque, u32 *value);
    int (*write_cell)(void *opaque, u32 value);
    int (*redraw)(void *opaque);
};
int recover_screen_98(struct recover *r, const struct recover_screen_ops *ops, void *opaque);

/* Provisional values from ADDENDUM.r2 §1. Hz is the real HAL frequency.
 * Static work describes the actual bounded callback sequence, including all
 * failure/BEGIN/END/final rows. Reject rather than grow any published limit. */
enum recover_budget_class { REC_BUDGET_S0, REC_BUDGET_GF, REC_BUDGET_SIZE_BARS };
struct recover_static_work { u32 bytes, wait_ms, points, s0_bytes, s0_wait_ms, s0_points; };
int recover_gf_screen_safe(struct recover *r);
int recover_gf_profile(struct recover_profile *p, bool cold, u32 kind, u32 hz);
int recover_budget_apply(struct recover_profile *p, u32 kind, u32 hz,
                         u32 baud, u32 frame_bits, const struct recover_static_work *work);
/* Finite raw PIT mode-2 latch sampling, <1us/count at both HAL clocks.
 * No tick_count, calibration, or sys_time_now IRR/clamp dependency. */
int recover_hal_wait_frame(void *opaque, u32 frame_us);
/* Explicit local order, supplied by the same classified descriptor rules.
 * UNLOCK → DATA → LOCK → original INDEX, then sole window release. */
enum recover_local_role { REC_LOCAL_UNLOCK, REC_LOCAL_DATA, REC_LOCAL_LOCK, REC_LOCAL_INDEX };
struct recover_local_step { u32 role, key, dir, width, value; };
int recover_indexed_cleanup(struct recover *r, const struct recover_local_step *steps,
                            u32 count, struct recover_window *window);
#endif
