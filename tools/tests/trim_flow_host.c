/* g3: real map/heap/slot/DONE and three independent real nano instances.
 * Only MMU aliases, cooperative context switching and WM delivery are models.
 * The scheduler is included unchanged; its input/derived/poll rules pick next.
 */
#include "mem_fixture.c"
#include "wm_source.c"
#include "os32_gui_shared.h"
#define u32 ma_u32
#define die ma_die
#define main ma_unused_main
#define _start ma_unused_start
#include "multiapp_model_host.c"
#undef _start
#undef main
#undef die
#undef u32

#define NANO_API(P) \
    void P##init(unsigned, unsigned); void *P##malloc(unsigned); \
    void P##free(void *); void *P##realloc(void *, unsigned); \
    void P##enable(void); unsigned P##serve(unsigned, unsigned (*)(void)); \
    unsigned P##retries(void); unsigned P##serves(void); \
    int P##busy(void); int P##errno(void); int P##nomem(void)
NANO_API(A_); NANO_API(B_); NANO_API(C_);

void *memcpy(void *d, const void *s, u32 n) { heap_copy(d, s, n); return d; }
void *memset(void *d, int c, u32 n) { u8 *p = d; while (n--) *p++ = c; return d; }
void os32_nano_prefix_check(const void *p) { (void)p; }

static struct addrspace c;
static MaState sched;
static int active, pressure, transaction, map_depth, stage_failed;
static u32 delivered[APP_SLOT_COUNT], consumed[APP_SLOT_COUNT], event_epoch[APP_SLOT_COUNT];
static int sent[APP_SLOT_COUNT], queued[APP_SLOT_COUNT];
static GuiEvent ring_event[APP_SLOT_COUNT];
static u32 ring_head[APP_SLOT_COUNT], ring_tail[APP_SLOT_COUNT];
static u32 yields, wm_events, as_events, protected_events, rollback_pages;
static u32 done_pages, hook_pages, hook_calls, c_hook_calls, obstacle, split, baseline_cur, initial_end;
static struct appmem_extent initial_extent;
static void *blocks[64], *nano_blocks[16];
static u32 block_count, low_count, data_crc;
static u32 trace[256], trace_count;
enum { MAP_BEGIN=1, STAGE_FAIL, MARK, MAP_NULL, UNLOCK, YIELD_PARK,
       X3, DELIVER_B, RESUME_B, POLL_B, TRIM_B, DONE_B, PARK_B,
       RESUME_A, RETRY_MAP, DONE_C };
static void event(u32 value) { CHECK("trace capacity", trace_count < 256); trace[trace_count++] = value; }
static void line(const char *s) { u32 n = 0; while (s[n]) n++; say(s, n); say("\n", 1); }
static void field(const char *s, u32 n) { u32 len = 0; while (s[len]) len++; say(s, len); number(n); say("\n", 1); }

/* Every WM entry and AS switch in this harness passes these observation points.
 * map_depth also covers prepare -> stage -> publish -> public KAPI return. */
void flow_pump_observe(void)
{
    wm_events++;
    if (transaction || map_depth) protected_events++;
    CHECK("no WM inside map transaction", !transaction && !map_depth);
}
static void select_app(int id)
{
    as_events++;
    if (transaction || map_depth) protected_events++;
    CHECK("no AS switch inside map transaction", !transaction && !map_depth);
    heap_select(id);
}

int flow_stage(struct paging_app_stage *tx, struct addrspace *as, u32 lo, u32 hi)
{
    struct addrspace before = *as;
    u32 pages = ledger_owner_pages(as->owner), root = paging_current_cr3();
    u32 free_before = heap_free_calls, events = wm_events + as_events;
    transaction = 1;
    int rc = paging_app_stage(tx, as, lo, hi);
    if (rc) {
        CHECK("rollback AS pages and CR3", equal(&before, as, sizeof(before)) &&
              ledger_owner_pages(as->owner) == pages && paging_current_cr3() == root && _irq_enabled());
        CHECK("rollback no dispatch", wm_events + as_events == events);
        rollback_pages += heap_free_calls - free_before;
        transaction = 0;
        if (active && rc == APPMEM_ENOSPC) { stage_failed = 1; event(STAGE_FAIL); }
    }
    return rc;
}
void flow_publish(struct appmem_table *table, const struct appmem_plan *plan)
{
    CHECK("publish inside transaction", transaction && !_irq_enabled());
    appmem_publish(table, plan);
    transaction = 0;
}
void appslot_trim_request_as(struct addrspace *as)
{
    if (active) {
        CHECK("mark after rollback", stage_failed && !transaction && _irq_enabled());
        event(MARK); stage_failed = 0;
    }
    trim_product_request(as);
}
#include "trim_exec_source.c"
void kbd_set_gui_mode(int on) { (void)on; }
void ime_set_render(void *table) { (void)table; }
#include "trim_gui_source.c"

/* Public map wrapper and allocator callbacks: failure injection is at pgalloc,
 * not a fabricated NULL allocator result. Fail the second allocation where
 * possible, exercising actual staged-page rollback before the trim mark. */
static void *map_at(unsigned bytes, unsigned hint, unsigned flags)
{
    if (active) event(current_slot == 2 && yields ? RETRY_MAP : MAP_BEGIN);
    if (pressure) {
        heap_alloc_fail = bytes <= PAGE_SIZE;
        heap_alloc_rollback_fail = bytes > PAGE_SIZE ? 2 : 0;
    }
    map_depth++;
    void *p = wrap_mem_map(bytes, (void *)hint, flags);
    map_depth--;
    heap_alloc_fail = heap_alloc_rollback_fail = 0;
    CHECK("map transaction closed", !transaction);
    if (active && !p) event(MAP_NULL);
    return p;
}
void *flow_map(void *opaque, unsigned bytes, unsigned flags)
{
    (void)opaque; return map_at(bytes, 0, flags);
}
int flow_grow(void *opaque, unsigned base, unsigned bytes)
{
    (void)opaque; return map_at(bytes, base, APPMEM_MAP_EXACT) == (void *)base ? 0 : -1;
}
int flow_unmap(void *opaque, unsigned base, unsigned bytes)
{
    (void)opaque; return wrap_mem_unmap((void *)base, bytes);
}

static u32 extent_count(struct addrspace *as, u32 kind)
{
    u32 count = 0;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) count += as->appmem.e[i].kind == kind;
    return count;
}
static u32 crc(const void *p, u32 n)
{
    u32 v = ~0U; const u8 *s = p;
    while (n--) { v ^= *s++; for (u32 k = 0; k < 8; k++) v = (v >> 1) ^ (0xedb88320U & (0U-(v&1))); }
    return ~v;
}
static u32 retained_crc(void)
{
    u32 value = 0, ordinal = 0;
    for (u32 i = 0; i < block_count; i++)
        if ((u32)blocks[i] < split && ordinal++ < low_count - 8) value ^= crc(blocks[i], 4000) + i;
    for (u32 i = 0; i < 4; i++) value ^= crc(nano_blocks[i], 4000) + i + 64;
    return value;
}
static void prep_back(void)
{
    select_app(3);
    do {
        CHECK("back capacity", block_count < 64);
        blocks[block_count] = wrap_mem_alloc(4000);
        CHECK("back allocation", blocks[block_count++]);
    } while (extent_count(&b, APPMEM_EXEC_ARENA) != 1);
    split = b.appmem_layout.exec_heap_cur_end;
    obstacle = (u32)map_at(PAGE_SIZE, split, APPMEM_MAP_EXACT);
    CHECK("back obstacle", obstacle == split);
    do {
        CHECK("back capacity", block_count < 64);
        blocks[block_count] = wrap_mem_alloc(4000);
        CHECK("back allocation", blocks[block_count++]);
    } while (extent_count(&b, APPMEM_EXEC_ARENA) != 2);
    for (u32 i = 0; i < block_count; i++) {
        if ((u32)blocks[i] < split) low_count++;
        memset(blocks[i], (int)i + 1, 4000);
    }
    CHECK("back preparation", low_count >= 8 && extent_count(&b, APPMEM_EXEC_ARENA) == 2 &&
          !extent_count(&b, APPMEM_EXEC_LARGE));
    u32 start = (u32)map_at(PAGE_SIZE, MEM_EXEC_LOAD_ADDR + 2 * PAGE_SIZE, APPMEM_MAP_EXACT);
    CHECK("back nano primary", start);
    B_init(start, PAGE_SIZE);
    for (u32 i = 0; i < 16; i++) {
        nano_blocks[i] = B_malloc(4000); CHECK("back nano allocation", nano_blocks[i]);
        memset(nano_blocks[i], (int)i + 65, 4000);
    }
    data_crc = retained_crc();
    initial_end = g_slot[3].exec_heap_base + g_slot[3].exec_heap_size;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++)
        if (b.appmem.e[i].kind == APPMEM_EXEC_INITIAL) initial_extent = b.appmem.e[i];
    /* G-2 unarmed DONE establishes the baseline after natural tails shrink. */
    g_slot[3].trim_pending = 1; g_slot[3].trim_epoch = ++appslot_trim_epoch;
    B_serve(g_slot[3].trim_epoch, 0);
    baseline_cur = b.appmem_layout.exec_heap_cur_end;
    CHECK("unarmed retains both arenas", extent_count(&b, APPMEM_EXEC_ARENA) == 2);
    line("PREP OK back ARENA=2 LARGE=0; unarmed DONE baseline captured");
}

static unsigned back_hook(void)
{
    u32 ordinal = 0;
    event(TRIM_B); hook_calls++;
    for (u32 i = 0; i < block_count; i++)
        if ((u32)blocks[i] >= split || ordinal++ >= low_count - 8) wrap_mem_free(blocks[i]);
    for (u32 i = 4; i < 16; i++) B_free(nano_blocks[i]);
    /* serve trims before hook. Trim the newly freed N tail in this hook. */
    extern unsigned B_trim(void);
    hook_pages = B_trim();
    CHECK("nano tail returned", hook_pages > 0);
    CHECK("DATA CRC retained", retained_crc() == data_crc);
    if (FLOW_CASE == 5) {
        u32 epoch = g_slot[4].trim_epoch, marks = appslot_trim_mark_count;
        CHECK("C pending before remark", g_slot[4].trim_pending);
        CHECK("hook raw map fails", !map_at(3 * PAGE_SIZE, 0, 0));
        CHECK("C remark preserves bit epoch", g_slot[4].trim_pending && g_slot[4].trim_epoch == epoch &&
              appslot_trim_mark_count == marks + 1 && g_slot[2].trim_pending);
    }
    line("DATA OK");
    return hook_pages;
}
int flow_gui(unsigned op, unsigned epoch)
{
    CHECK("SDK sends only DONE", op == GUI_OP_TRIM_DONE);
    u32 free_before = pgalloc_free_pages();
    int rc = gui_call(op, epoch);
    CHECK("DONE accepted", rc >= 0);
    if (active && current_slot == 3) {
        done_pages = rc; event(DONE_B);
        CHECK("DONE returns real pages", done_pages > 0 && pgalloc_free_pages() >= free_before + done_pages);
        pressure = 0;
    }
    if (active && current_slot == 4) event(DONE_C);
    return rc;
}

/* C transliteration of trim.rs deliver: pending bit, sent, tracked GUI,
 * non-front, PARKED, one window-zero event carrying the per-app epoch.
 * Its ring is one entry since only one pending trim is allowed per app. */
static void deliver(void)
{
    flow_pump_observe(); event(X3);
    for (int id = 2; id <= 4; id++) {
        if (!g_slot[id].trim_pending) { sent[id] = 0; continue; }
        if (sent[id] || !g_slot[id].gui || id == 2 || g_slot[id].state != APP_STATE_PARKED) continue;
        CHECK("ring has room", !queued[id]);
        queued[id] = sent[id] = 1; event_epoch[id] = g_slot[id].trim_epoch;
        ring_event[id] = (GuiEvent){0}; ring_event[id].kind = GUI_EV_TRIM;
        for (u32 byte = 0; byte < 4; byte++)
            ring_event[id].payload.raw[byte] = (u8)(event_epoch[id] >> (8 * byte));
        ring_tail[id]++;
        delivered[id]++;
        ma_set_ready(&sched, id, 1, 0);
        if (id == 3) event(DELIVER_B);
    }
}
static unsigned c_hook(void)
{
    c_hook_calls++;
    line("C hook after requester retry");
    return 0;
}
static void run_background(void)
{
    for (;;) {
        deliver();
        int id = ma_pick(&sched);
        if (id == 2 || !id) break;
        CHECK("scheduler chooses pending back", id == 3 || id == 4);
        CHECK("model resume", ma_resume(&sched, id) == MA_OK);
        select_app(id); g_slot[id].state = APP_STATE_RUNNING;
        if (id == 3) event(RESUME_B);
        if (queued[id]) {
            u32 epoch = 0;
            for (u32 byte = 0; byte < 4; byte++)
                epoch |= (u32)ring_event[id].payload.raw[byte] << (8 * byte);
            CHECK("TRIM epoch payload", epoch == g_slot[id].trim_epoch &&
                  ring_event[id].kind == GUI_EV_TRIM && ring_event[id].window == 0 &&
                  ring_tail[id] == ring_head[id] + 1);
            queued[id] = 0; consumed[id]++; ring_head[id]++;
            if (id == 3) event(POLL_B);
            if (FLOW_CASE != 1 && FLOW_CASE != 4) {
                if (id == 3) B_serve(event_epoch[id], back_hook);
                else C_serve(event_epoch[id], FLOW_CASE == 7 ? c_hook : 0);
            }
        }
        ma_set_ready(&sched, id, 0, 0);
        CHECK("back WAIT", ma_gui_call(&sched, MA_OP_WAIT) == MA_OK);
        CHECK("back park", ma_park(&sched) == MA_OK);
        g_slot[id].state = APP_STATE_PARKED;
        if (id == 3) event(PARK_B);
    }
}
void flow_yield(void)
{
    CHECK("retry after unlock", !A_busy());
    CHECK("yield outside transaction", !transaction && !map_depth);
    CHECK("CUI no yield", FLOW_CASE != 3);
    CHECK("raw map no yield", FLOW_CASE != 2);
    CHECK("requester initially excluded", !g_slot[2].trim_pending);
    if (FLOW_CASE == 6) {
        CHECK("blocked primary marks before first yield", g_slot[3].trim_pending &&
              !delivered[3] && !hook_calls);
        line("blocked primary: secondary ENOSPC marked back before first yield");
    }
    event(UNLOCK); yields++; event(YIELD_PARK);
    CHECK("front yields once", yields == 1);
    CHECK("front poll park", ma_park_poll(&sched) == MA_OK);
    g_slot[2].state = APP_STATE_WAIT_POLL;
    if (FLOW_CASE == 7) {
        sched.focus = 2;
        ma_set_ready(&sched, 2, 1, 0); /* target break still unread */
        CHECK("input exception ready includes requester", ma_ready(&sched, &sched.app[0]));
    }
    run_background();
    if (FLOW_CASE == 7)
        CHECK("input exception preserves pending before retry", delivered[3] == 1 &&
              delivered[4] == 1 && !consumed[3] && !consumed[4] && !hook_calls &&
              g_slot[3].trim_pending && g_slot[4].trim_pending);
    CHECK("poll resumes after inputs", ma_pick(&sched) == 2);
    CHECK("front model resume", ma_resume(&sched, 2) == MA_OK);
    select_app(2); g_slot[2].state = APP_STATE_RUNNING; event(RESUME_A);
}
static void check_order(void)
{
    const u32 expected[] = {MAP_BEGIN, STAGE_FAIL, MARK, MAP_NULL, UNLOCK,
        YIELD_PARK, X3, DELIVER_B, RESUME_B, POLL_B, TRIM_B, DONE_B, PARK_B,
        RESUME_A, RETRY_MAP};
    u32 next = 0;
    for (u32 i = 0; i < trace_count && next < sizeof(expected)/sizeof(*expected); i++)
        if (trace[i] == expected[next]) next++;
    CHECK("complete ordered trace", next == sizeof(expected)/sizeof(*expected));
    line("TRACE A map -> stage rollback -> mark -> NULL -> unlock -> yield/park -> X3 -> B input resume -> POLL/TRIM -> trim -> DONE -> B park -> A poll resume -> retry");
}
static void regrow(void)
{
    select_app(3);
    int initial_matches = 0;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++)
        if (equal(&initial_extent, &b.appmem.e[i], sizeof(initial_extent))) initial_matches++;
    CHECK("INITIAL extent unchanged", initial_matches == 1 && initial_extent.kind == APPMEM_EXEC_INITIAL);
    CHECK("G4 tail and full arena return", b.appmem_layout.exec_heap_cur_end < baseline_cur &&
          extent_count(&b, APPMEM_EXEC_ARENA) == 1 &&
          g_slot[3].exec_heap_base + g_slot[3].exec_heap_size == initial_end &&
          retained_crc() == data_crc);
    line("G-4 tail returned; ARENA 2->1; INITIAL unchanged; DATA OK");
    u32 cur = b.appmem_layout.exec_heap_cur_end, trim_total = appslot_trim_pages_total;
    CHECK("obstacle unmap", !wrap_mem_unmap((void *)obstacle, PAGE_SIZE));
    CHECK("obstacle excluded from trim total", appslot_trim_pages_total == trim_total);
    line("obstacle unmapped pages=1 (separate from trim)");
    void *p = wrap_mem_alloc(4000);
    CHECK("EXACT regrow", (u32)p == cur + BLK_HDR_SIZE &&
          b.appmem_layout.exec_heap_cur_end == cur + MEM_EXEC_HEAP_MIN &&
          extent_count(&b, APPMEM_EXEC_ARENA) == 1);
    line("regrow ok EXACT +65536 ARENA=1");
}
void _start(void)
{
    heap_ram(); host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    roots[0] = paging_kernel_pd_phys(); integration = heap_alias_on = 1;
    heap_mmap(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, 0);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE);
    appslot_init(); ring3_in_syscall = 1;
    heap_create(&a, 2); roots[1] = a.pd_phys;
    heap_create(&b, 3); roots[2] = b.pd_phys;
    g_slot[2].gui = g_slot[3].gui = 1;
    prep_back();
    if (FLOW_CASE == 5 || FLOW_CASE == 3 || FLOW_CASE == 7) {
        heap_create(&c, 4); g_slot[4].gui = 1;
        u32 start = (u32)map_at(PAGE_SIZE, MEM_EXEC_LOAD_ADDR + 2 * PAGE_SIZE, APPMEM_MAP_EXACT); CHECK("C nano primary", start);
        C_init(start, PAGE_SIZE);
    }
    select_app(2);
    u32 primary_hint = FLOW_CASE == 6 ? MEM_EXEC_HEAP_BASE - 2 * PAGE_SIZE :
                                       MEM_EXEC_LOAD_ADDR + 2 * PAGE_SIZE;
    u32 start = (u32)map_at(PAGE_SIZE, primary_hint, APPMEM_MAP_EXACT); CHECK("A nano primary", start);
    A_init(start, PAGE_SIZE);
    if (FLOW_CASE == 6) {
        /* flags=0 searches the lower window from its HIGH end. Place the
         * primary just below that raw map to test an actual EXACT obstacle. */
        CHECK("raw map blocks primary end", (u32)map_at(PAGE_SIZE, 0, 0) == start + PAGE_SIZE);
        struct appmem_plan plan;
        CHECK("primary grow is ENOVA", appmem_prepare(&a.appmem, &a.appmem_layout,
              PAGE_SIZE, start + PAGE_SIZE, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &plan) == APPMEM_ENOVA);
    }
    void *old = A_malloc(4000); CHECK("A free list exhausted", old);
    memset(old, 0xa7, 4000); u32 old_crc = crc(old, 4000);
    ma_init(&sched, 4096);
    for (int id = 2; id <= (FLOW_CASE == 5 || FLOW_CASE == 3 || FLOW_CASE == 7 ? 4 : 3); id++) {
        CHECK("model start", ma_start(&sched, 100, 1) == id);
        ma_gui_call(&sched, MA_OP_WAIT); ma_park(&sched);
        g_slot[id].state = APP_STATE_PARKED;
    }
    ma_set_ready(&sched, 2, 1, 0); ma_resume(&sched, 2); ma_set_ready(&sched, 2, 0, 0);
    g_slot[2].state = APP_STATE_RUNNING;
    if (FLOW_CASE == 3) {
        g_slot[2].gui = 0; sched.app[0].gui = 0; sched.app[0].slot = -1;
        sched.app[0].parent = 4; sched.app[2].state = MA_RUNNING;
        g_slot[4].state = APP_STATE_RUNNING; /* synchronous exec_run parent */
    }
    else A_enable();
    u32 marks = appslot_trim_mark_count, dones = appslot_trim_done_count;
    active = pressure = 1;
    void *p;
    if (FLOW_CASE == 2) p = map_at(3 * PAGE_SIZE, 0, 0);
    else if (FLOW_CASE == 4) p = A_realloc(old, 8000);
    else p = A_malloc(4000);
    CHECK("no protected callbacks", protected_events == 0);
    CHECK("rollback exercised", rollback_pages > 0);
    CHECK("requester not marked", FLOW_CASE == 5 || !g_slot[2].trim_pending);
    CHECK("mark target count", appslot_trim_mark_count == marks + (FLOW_CASE == 5 ? 3U : FLOW_CASE == 7 ? 2U : 1U));
    if (FLOW_CASE == 7) {
        CHECK("input exception ENOMEM retry=1", !p && A_nomem() && yields == 1 && A_retries() == 1 &&
              appslot_trim_done_count == dones && !hook_calls);
        CHECK("input exception retains allocation", crc(old, 4000) == old_crc);
        u32 free_before = pgalloc_free_pages();
        line("A input first: ENOMEM retry=1; B/C pending retained before hooks/DONE");
        ma_set_ready(&sched, 2, 0, 0);
        CHECK("front waits after retry", ma_gui_call(&sched, MA_OP_WAIT) == MA_OK && ma_park(&sched) == MA_OK);
        g_slot[2].state = APP_STATE_PARKED;
        run_background();
        CHECK("input exception later DONE and resources", appslot_trim_done_count == dones + 2 &&
              !g_slot[3].trim_pending && !g_slot[4].trim_pending && hook_calls == 1 && c_hook_calls == 1 &&
              B_serves() == 2 && C_serves() == 1 && delivered[3] == 1 && delivered[4] == 1 &&
              consumed[3] == 1 && consumed[4] == 1 && pgalloc_free_pages() > free_before);
        regrow();
        line("B/C later DONE: pending cleared, resources returned, DATA retained");
    } else if (FLOW_CASE == 2 || FLOW_CASE == 3) {
        CHECK("raw CUI ENOMEM no retry", !p && !yields && !A_retries() && (FLOW_CASE == 2 || A_nomem()));
        CHECK("CUI back mark survives", g_slot[3].trim_pending && !delivered[3]);
        if (FLOW_CASE == 3) {
            line("CUI ENOMEM self-mark=0 yield=0; back mark=1; child exits -> parent OP_WAIT X3");
            /* Parent's first X3 after synchronous child return delivers B. */
            CHECK("child returns to parent", ma_exit(&sched, 0) == MA_OK && sched.cur == 4);
            g_slot[2].state = APP_STATE_FREE;
            select_app(4);
            CHECK("parent OP_WAIT", ma_gui_call(&sched, MA_OP_WAIT) == MA_OK);
            CHECK("parent park", ma_park(&sched) == MA_OK);
            g_slot[4].state = APP_STATE_PARKED;
            run_background();
            CHECK("parent X3 delivered", delivered[3] == 1 && consumed[3] == 1 && appslot_trim_done_count == dones + 1);
        } else line("raw mem_map ENOMEM yield=0 retry=0");
    } else {
        CHECK("one whole operation retry", yields == 1 && A_retries() == 1);
        CHECK("one delivery consumption", delivered[3] == 1 && consumed[3] == 1 &&
              ring_head[3] == 1 && ring_tail[3] == 1);
        if (FLOW_CASE == 1 || FLOW_CASE == 4) {
            CHECK("unanswered ENOMEM bit remains", !p && A_nomem() && g_slot[3].trim_pending && appslot_trim_done_count == dones &&
                  B_serves() == 1 && !hook_calls);
            CHECK("realloc old CRC retained", crc(old, 4000) == old_crc);
            /* A normal Focus event can wake B without another TRIM/DONE. */
            ma_gui_call(&sched, MA_OP_WAIT); ma_park(&sched); g_slot[2].state = APP_STATE_PARKED;
            ma_set_ready(&sched, 3, 1, 0); run_background();
            CHECK("normal wake no redelivery", delivered[3] == 1 && consumed[3] == 1 &&
                  appslot_trim_done_count == dones && g_slot[3].trim_pending);
            line("unanswered ENOMEM retry=1 delivery=1 consume=1 done=0 bit retained; Focus wake unchanged; CRC OK");
        } else {
            CHECK("retry succeeds after DONE", p && !g_slot[3].trim_pending && appslot_trim_done_count == dones + (FLOW_CASE == 5 ? 2U : 1U));
            CHECK("one armed hook and serve", hook_calls == 1 && B_serves() == 2);
            check_order();
            if (FLOW_CASE == 5) CHECK("C DONE accepted after remark", delivered[4] == 1 && consumed[4] == 1 && !g_slot[4].trim_pending && C_serves() == 1);
            regrow();
            field("kernel trim pages=", done_pages); field("nano hook pages=", hook_pages);
        }
    }
    line("PASS trim_flow"); die(0);
}
