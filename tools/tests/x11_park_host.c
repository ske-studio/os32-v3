#include "host_exec_context.h"
#include "font_guard_inactive.h"
/* Included by x11_shared_host.c after the real IME and keyboard sources.
 * Production exec_park/exec_resume, context save/restore and AppSlot transitions
 * run unmodified, with the real setjmp.asm. Only CPU, heap, redirect and caller
 * descriptor edges are substitutes. IME has one shared resident state: there
 * is deliberately no fixture save/restore of it when an owner changes. */
#include "exec.h"
#include "exec_heap.h"
#include "redir_access.h"
#include "tss.h"

#define REQUIRE(c, msg) do { if (!(c)) { say("FAIL " msg "\n"); \
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1) : "memory"); \
    __builtin_unreachable(); } } while (0)

static struct arch_fp_context fp_parent[APP_SLOT_COUNT];
enum { X87_SAVE = 1, X87_RESTORE = 2, X87_EVENT_CAPACITY = 8 };
static const struct arch_fp_context *x87_events[X87_EVENT_CAPACITY];
static int x87_kinds[X87_EVENT_CAPACITY], x87_count;
static void record_x87(int kind, const struct arch_fp_context *p)
{
    REQUIRE(x87_count < X87_EVENT_CAPACITY, "x87 event capacity");
    x87_kinds[x87_count] = kind;
    x87_events[x87_count++] = p;
}
void arch_fp_save(struct arch_fp_context *p) { record_x87(X87_SAVE, p); }
void arch_fp_restore(const struct arch_fp_context *p) { record_x87(X87_RESTORE, p); }
static int g_cur, g_cur_op_is_wait, ring3_in_syscall;
static AppSlot *g_cur_app;
static struct arch_syscall *g_cur_frame;
static u32 cr3, host_esp0;
static u32 ring3_tramp_page[KAPI_DATA_IDX_SBRK_HEAP_LIMIT + 1];
static struct x11_kapi { u32 sbrk_heap_limit; } host_kapi, *kapi = &host_kapi;
struct tss_entry kernel_tss;
volatile u32 kctx_irq_depth, kctx_exc_depth;
volatile u32 ring3_park_reject_count, ring3_resume_bad_frame_count, ring3_switch_count;
static IME_State park_expected;
static int park_partial, resumed_count;
static u32 live_frame[APP_FRAME_WORDS];

u32 tss_get_esp0(void) { return host_esp0; }
void tss_set_esp0(u32 n) { host_esp0 = n; }
void caller_access_save(volatile CallerAccessFrame *out)
{ kmemset((void *)out, 0, sizeof(*out)); }
void caller_access_leave(const volatile CallerAccessFrame *saved) { (void)saved; }
void caller_access_invalidate(void) {}
static void redir_switch_out(int id) { (void)id; }
static void redir_switch_in(int id) { (void)id; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 pd) { cr3 = pd; }
#define LEGACY_FAKE_PINNED_ENABLE
#define LEGACY_FAKE_PINNED 0
#define LEGACY_FAKE_LOWMAP_ENABLE
#define LEGACY_FAKE_LOWMAP 0
#include "legacy_guest_fake.h"


int paging_set_not_present(u32 start, u32 end) { (void)start; (void)end; return 0; }
void exec_heap_save_state(u32 *used) { *used = 0; }
void exec_heap_restore_state(u32 base, u32 size, u32 used)
{ (void)base; (void)size; (void)used; }
AppSlot *appslot_at(int id) { return &slots[id]; }
static void exec_pending_finish(void) {}
void ring3_resume(const u32 *frame, u32 pd, void *tss);

/* These names are private globals in the extracted product AppSlot functions. */
#define g_slot slots
#include "slot_source.inc"
#undef g_slot

/* exec_pending_finish is a no-op for a park landing. Its pending machinery is
 * covered by test_exec_r1; no owner-exit/IME cleanup substitute is called here. */
#include "shell_service_stub_host.h"   /* T7b-b2 S3b: exec.c の結線の弱い贋物 */
#include "exec_source.inc"

static void prepare_wait(void)
{
    static struct arch_syscall call = { live_frame }; g_cur_frame = &call;
    ring3_in_syscall = 1;
    ring3_wm_depth = 1;
    g_cur_op_is_wait = slots[2].in_op_wait = 1;
}

void ring3_resume(const u32 *frame, u32 pd, void *tss)
{
    REQUIRE((const void *)frame == HOST_FRAME(slots[2].frame) && frame[8] == 0x1234 && frame[APP_FRAME_EAX] == 37 &&
            pd == 2 && cr3 == 2 && tss == &kernel_tss &&
            owner == 2 && g_cur == 2 && slots[2].state == APP_STATE_RUNNING &&
            !slots[2].parked_from_wait, "real resume reaches saved frame");
    resumed_count++;
    if (park_partial == 2) {
        REQUIRE(equal_state(&park_expected), "IMETAB real resume preserves partial commit");
        REQUIRE(ime_trygetchar() == 0xba && ime_trygetkey() == 0x9c &&
                ime_trygetkey() == ((KEY_TAB << 8) | 9) && ime_trygetkey() == -1 && learns == 1,
                "IMETAB real resume delivers remaining candidate then pending Tab once");
    } else if (park_partial) {
        REQUIRE(equal_state(&park_expected), "real resume preserves unread commit and dictionary");
        REQUIRE(ime_feed_key(-1) == 0xba && ime_feed_key(-1) == 0x9c &&
                ime_feed_key(-1) == -1, "resumed commit tail delivered exactly once");
    } else {
        REQUIRE(equal_state(&park_expected), "real resume preserves candidate and dictionary");
        REQUIRE(ime_feed_key((KEY_RETURN << 8) | 13) == 0xe4 && learns == 1 &&
                ime_feed_key(-1) == 0xba && ime_feed_key(-1) == 0x9c &&
                ime_feed_key(-1) == -1, "resumed copied candidate selected and learned");
    }
    /* Search again through the still-live, globally switched dictionary. */
    REQUIRE(ime_feed_key((0x1d << 8) | 'a') == -1 &&
            ime_feed_key((KEY_SPACE << 8) | ' ') == -1 && searches == 2 &&
            g_ime.result_count == 2 && g_ime.store.system.db == shared_dict &&
            !kstrcmp(g_ime.store.system.path, "/db/fep_l.db") && opens == 2 && reopens == 1,
            "resumed dictionary remains usable without reopening");
    prepare_wait();
    (void)exec_park(); /* a second real park returns through exec_resume's landing */
    REQUIRE(0, "second park must longjmp");
}

static int park_lifetime(void)
{
    static struct addrspace space;
    for (park_partial = 0; park_partial < 3; park_partial++) {
        reset();
        kmemset(slots, 0, sizeof(slots));
        slots[2].state = APP_STATE_RUNNING;
        slots[2].gui = slots[2].cpl3 = 1;
        slots[2].as = &space; space.pd_phys = 2;
        kmemset(live_frame, 0, sizeof(live_frame)); live_frame[8] = 0x1234;
        g_cur = owner = cr3 = 2; g_cur_app = &slots[2];
        resumed_count = 0; ring3_switch_count = 0; candidate_draws = 0;
        x87_count = 0;
        ime_toggle();
        con_sink_enable(); kbd_set_gui_mode(1);
        CHECK(ime_feed_key((0x1d << 8) | 'a') == -1 &&
              ime_feed_key((KEY_SPACE << 8) | ' ') == -1 &&
              ime_feed_key(KEY_DOWN << 8) == -1 && g_ime.state == IME_ST_CANDLIST &&
              g_ime.converting && g_ime.result_count == 2 && candidate_draws >= 2,
              "WM candidates actually displayed before real park");
        if (park_partial == 2) {
            con_sink_disable(); kbd_set_gui_mode(0);
            key(KEY_TAB);
            CHECK(ime_trygetkey() == 0xe4 && g_ime.commit_pos == 1 && learns == 1,
                  "IMETAB CUI commit and pending Tab before real park");
        } else if (park_partial)
            CHECK(ime_feed_key((KEY_RETURN << 8) | 13) == 0xe4 &&
                  g_ime.commit_pos == 1 && g_ime.commit_len == 3,
                  "WM commit partially consumed before real park");
        park_expected = g_ime;
        /* Establish a live launch-side return point, using production assembly.
         * The complete production exec_resume below owns the next return point. */
        if (exec_setjmp(slots[2].jmpbuf) == 0) {
            prepare_wait();
            (void)exec_park();
            CHECK(0, "first park must longjmp");
        }
        CHECK(owner == 1 && g_cur == 1 && cr3 == 1 && !g_cur_app &&
              slots[2].state == APP_STATE_PARKED && slots[2].parked_from_wait,
              "real park reaches WM with saved frame");
        CHECK(x87_count == 1 && x87_kinds[0] == X87_SAVE &&
              x87_events[0] == &slots[2].fp_state, "x87 first park saves slot");
        CHECK(equal_state(&park_expected), "real park preserves candidate and dictionary");
        /* A management operation while parked is global, never rolled back by
         * resume. Already copied candidates and commit bytes stay valid. */
        CHECK(ime_switch_dict(2) == 0, "WM switches resident dictionary while parked");
        park_expected = g_ime;
        CHECK(exec_resume(2, 37) == 2 && resumed_count == 1 && ring3_switch_count == 1 &&
              owner == 1 && cr3 == 1 && slots[2].state == APP_STATE_PARKED,
              "real resume and second park complete round trip");
        CHECK(x87_count == 5 && x87_kinds[1] == X87_SAVE &&
              x87_events[1] == &fp_parent[exec_nest_level] &&
              x87_kinds[2] == X87_RESTORE && x87_events[2] == &slots[2].fp_state &&
              x87_kinds[3] == X87_SAVE && x87_events[3] == &slots[2].fp_state &&
              x87_kinds[4] == X87_RESTORE && x87_events[4] == &fp_parent[exec_nest_level],
              "x87 resume and park exchange slot and parent");
    }
    say("PASS X11 real exec park/resume candidate dictionary commit lifetime\n");
    return 0;
}
