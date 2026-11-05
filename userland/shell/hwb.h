/* Shell-private HW-BASIC contracts; HW_BASIC_DESIGN §§2-4,7 (PM decisions).
 * No g_api access in parse/exec/log: every external effect uses backend.
 * All offsets are bytes in program.source. No allocation in these components;
 * cmd_hw owns source/program heap storage until unload/replacement. */
#ifndef SHELL_HWB_H
#define SHELL_HWB_H
#include "kdebug/hwb_wire.h"

#define HWB_VARIABLES 64U
#define HWB_ARRAY_ELEMENTS 16384U
#define HWB_NAME_CHARS 16U
#define HWB_LINE_BYTES 256U
#define HWB_FILE_BYTES (64U * 1024U)
#define HWB_LABELS 64U
#define HWB_CALL_DEPTH 8U
#define HWB_US_MIN 1U
#define HWB_US_MAX 10000000UL
#define HWB_ITERATIONS 1000000UL
/* One bit per possible instruction-start byte avoids an extra command limit.
 * Index + labels stay below the §5 12 KiB index budget at the 64 KiB limit. */
#define HWB_INDEX_BYTES BITMAP_BYTES(HWB_FILE_BYTES)
#define HWB_LOG_BYTES (4U * HWB_LINE_BYTES + 512U)
#define HWB_NO_PC HWB_FILE_BYTES

enum hwb_rc {
    HWB_OK = HW_OK, HWB_ARG = HW_ARG, HWB_NOSYS = HW_NOSYS,
    HWB_WINDOW = HW_WINDOW, HWB_IO = HW_IO_ERROR, HWB_TIMEOUT, HWB_ABORT
};
enum hwb_exit {
    HWB_EXIT_OK = 0, HWB_EXIT_ARG = 2, HWB_EXIT_NOSYS = 3,
    HWB_EXIT_WINDOW = 4, HWB_EXIT_IO = 5,
    HWB_EXIT_TIMEOUT = 124, HWB_EXIT_ABORT = 130
};
/* Pure mappings, invalid rc maps to IO; rc names use §4 uppercase spelling. */
int hwb_exit_status(enum hwb_rc rc);
const char *hwb_rc_name(enum hwb_rc rc);

struct hwb_backend {
    void *ctx;
    /* Raw kdebug_call result (0 or OS32_ERR_*); req outputs only valid at 0. */
    int (*access)(void *ctx, struct hw_req *req);
    /* Same snapshot lo/hi microseconds; negative failure leaves both untouched. */
    int (*time_now)(void *ctx, u32 *lo, u32 *hi);
    void (*yield)(void *ctx);
    /* POINT adapter: call result, flag output valid at 0; consumes old flag. */
    int (*point)(void *ctx, u32 *flag);
    /* Exactly one complete NUL-terminated line, without newline. cmd_hw uses
     * kprintf(attr,"%s\n",line) once; no separate serial delivery. */
    void (*line)(void *ctx, const char *line);
    /* Copy whole file, no truncation; capacity includes no implicit NUL.
     * 0 success sets length, negative failure leaves length untouched.
     * Caller supplies distinct scratch storage while an old load is live. */
    int (*read_file)(void *ctx, const char *path, char *buf, u32 capacity, u32 *length);
    /* Enumerated ID matches: cmd_hw scans pci_count/pci_get like cmd_pci.c,
     * NOT pci_find (first match loses ambiguity). Return total >=0 or negative
     * API error; store first capacity packed BDFs in enumeration order.
     * Exec requests candidates up to HWB_VARIABLES at a time using start;
     * start is a match ordinal, total counts all matches regardless of start.
     * Multiple candidates are printed, then ARG; never select one silently. */
    int (*find_id)(void *ctx, u32 vendor, u32 device, u32 start,
                   u32 *bdfs, u32 capacity);
};

/* Expressions are immutable source slices, evaluated with checked u32 C
 * precedence (+,-,&,|,^,<<,>>,parentheses). No precomputed hardware reads.
 * Each instruction is decoded on demand from a syntax-validated program;
 * operands retain slices so BARs/variables are resolved at execution time. */
struct hwb_span { u32 begin, end; }; /* half open, end <= source_bytes */
struct hwb_label { char name[HWB_NAME_CHARS + 1U]; u32 pc; };
struct hwb_program {
    const char *source;
    u32 source_bytes, instructions, label_count;
    u8 starts[HWB_INDEX_BYTES]; /* bit k marks an executable source offset k */
    struct hwb_label labels[HWB_LABELS];
};
enum hwb_opcode {
    HWB_DIM, HWB_LET, HWB_DEVICE_BDF, HWB_DEVICE_ID, HWB_WINDOW_BAR,
    HWB_READ, HWB_WRITE, HWB_FILL, HWB_WAIT, HWB_POLL, HWB_PRINT, HWB_LOG,
    HWB_LABEL, HWB_GOTO, HWB_IF, HWB_CALL, HWB_RETURN, HWB_END
};
enum hwb_address_kind { HWB_ADDR_PCI, HWB_ADDR_PORT, HWB_ADDR_PHYS,
                        HWB_ADDR_BAR, HWB_ADDR_WINDOW };
struct hwb_address {
    enum hwb_address_kind kind;
    struct hwb_span device; /* device/window variable name, empty for phys/port */
    struct hwb_span bar, offset;
};
struct hwb_instruction {
    enum hwb_opcode op;
    /* READ/WRITE/FILL: requested PCI/IO/PHYS from the command spelling, independent
     * of address.kind. Exec uses it for dispatch and BAR type validation;
     * IO requires an I/O BAR, PHYS a memory BAR (also for window references).
     * POLL: PCI for pci, IO for port, PHYS for phys/bar/window. Otherwise 0.
     * Exec never reparses the command spelling to recover this information. */
    enum hw_space space;
    u32 line, pc, next_pc, width; /* 1-based physical line; width in bytes */
    struct hwb_span index; /* optional destination subscript expression */
    struct hwb_span name, text; /* destination/label and quoted text contents */
    struct hwb_address address;
    /* LET/WRITE/FILL/WAIT/PRINT: value; FILL: count; IF: lhs/rhs and not_equal;
     * DEVICE_BDF: bus/dev/fn; DEVICE_ID: vendor/device;
     * WINDOW_BAR: address.device/bar; POLL: mask/value/timeout/interval. */
    struct hwb_span value, lhs, rhs, bus, dev, fn, vendor, device;
    struct hwb_span mask, timeout, interval, count;
    u32 not_equal;
};
enum hwb_value_kind { HWB_SCALAR, HWB_DEVICE, HWB_WINDOW_REF, HWB_ARRAY };
struct hwb_variable {
    char name[HWB_NAME_CHARS + 1U];
    enum hwb_value_kind kind;
    u32 value; /* scalar or packed BDF; window uses device_slot/bar */
    u32 device_slot, bar;
    u32 array_start, array_count;
};
struct hwb_initial { const char *name; u32 value; };
struct hwb_state {
    const struct hwb_backend *backend;
    const struct hwb_program *program;
    struct hwb_variable vars[HWB_VARIABLES];
    u32 array_used, array_values[HWB_ARRAY_ELEMENTS];
    u32 var_count, pc, call_stack[HWB_CALL_DEPTH], call_depth;
    u32 run, seq, writes, lines; /* writes counts successful writes only */
    enum hwb_rc rc;
    u32 stopped; /* end, failure or abort: step/continue cannot resume */
};
struct hwb_poll_result { u32 value, reads, elapsed_lo, elapsed_hi; };
struct hwb_log_record {
    u32 run, seq, line;
    enum hwb_opcode op;
    u32 space, width, bdf, addr, value;
    const char *target; /* resolved logical target, printable, valid for call */
    enum hwb_rc rc;
    const char *destination;
    u32 indexed, index;
    u32 requested_us, elapsed_lo, elapsed_hi, count, done;
};

/* Validate all syntax/labels before effects. On failure *out is scratch/invalid,
 * *error_line identifies the physical line; source and live state unchanged. */
enum hwb_rc hwb_parse(struct hwb_program *out, const char *source,
                      u32 bytes, u32 *error_line);
/* Decode indexed pc into out; failure leaves program/state unchanged. */
enum hwb_rc hwb_parse_instruction(const struct hwb_program *program, u32 pc,
                                  struct hwb_instruction *out);
/* Evaluate slice against state.program/vars; failure leaves *value unchanged
 * and has no backend effects. Undefined/type/overflow/shift errors are ARG. */
enum hwb_rc hwb_eval(const struct hwb_state *state, struct hwb_span expr, u32 *value);
/* Initialize empty session with borrowed backend, run counter starts at 0. */
void hwb_init(struct hwb_state *state, const struct hwb_backend *backend);
/* Reset VARIABLES ONLY (including device/window references), retain load/PC. */
void hwb_reset(struct hwb_state *state);
/* Read into caller's scratch buffer, parse scratch program, seed mode=0 then
 * initial args. On failure live load/vars/PC stay unchanged. On success borrow
 * storage, start new run/seq=0, consume one stale POINT, execute nothing yet. */
enum hwb_rc hwb_load(struct hwb_state *state, struct hwb_program *program,
                     char *source, u32 capacity, const char *path,
                     const struct hwb_initial *initial, u32 count);
/* Load as above then continue; failure halts with no subsequent instruction. */
enum hwb_rc hwb_run(struct hwb_state *state, struct hwb_program *program,
                    char *source, u32 capacity, const char *path,
                    const struct hwb_initial *initial, u32 count);
/* Execute exactly one indexed instruction (including label), with POINT first.
 * Failure marks stopped, retains failed PC, never rolls back successful writes. */
enum hwb_rc hwb_step(struct hwb_state *state);
/* Repeat step until end/failure; stopped sessions perform no new effects. */
enum hwb_rc hwb_continue(struct hwb_state *state);
/* Stop loaded session now; retain vars/PC/writes, emit ABORT terminal once. */
void hwb_abort(struct hwb_state *state);
/* Syntax-check/execute one interactive line, retain vars, start new run and
 * discard stale POINT once; loaded program lifetime/PC is retained. */
enum hwb_rc hwb_command(struct hwb_state *state, const char *source, u32 bytes);

/* Each formatter builds a full escaped line then calls backend.line ONCE.
 * No hardware/clock effects. begin uses PENDING; end names rc. Caller supplies
 * immutable record; poll emits only final value/reads/elapsed (no per-read log). */
void hwb_log_begin(const struct hwb_backend *backend, const struct hwb_log_record *r);
void hwb_log_end(const struct hwb_backend *backend, const struct hwb_log_record *r);
void hwb_log_poll(const struct hwb_backend *backend, const struct hwb_log_record *r,
                  const struct hwb_poll_result *result);
/* Text length <= HWB_LINE_BYTES; escape controls; tag/value for print. */
void hwb_log_print(const struct hwb_backend *backend, const struct hwb_log_record *r,
                   const char *tag, u32 bytes, u32 value);
/* Literal log text, same escaping/one-call contract as print. */
void hwb_log_text(const struct hwb_backend *backend, const struct hwb_log_record *r,
                  const char *text, u32 bytes);
/* §4 terminal: HW run=... op=end rc=... lines=... writes=... . */
void hwb_log_finish(const struct hwb_state *state);

STATIC_ASSERT(sizeof(struct hwb_state) <= 72U * 1024U, hwb_resident_budget);
STATIC_ASSERT(sizeof(struct hwb_program) <= 12U * 1024U, hwb_index_budget);
#endif
