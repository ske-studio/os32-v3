/* Execute the guest fixture's control flow with a recording KernelAPI.
 * This tests the fixture's verdicts, not the kernel behavior (covered by the
 * real-source fixtures). A fake exec result alone must not mask a leak. */
#include "kstring.h"
#define strcmp kstrcmp
#define strcpy kstrcpy
#define strcat kstrcat
#define strlen kstrlen
#define main raw_guest_main
#include "guest_source.inc"
#undef main

static KernelAPI table;
static u8 allocation[RAW_USER_BYTES];
static int selected, reads, opens, closes, frees, returned, bad, parent_case;
static int stats, sql_calls, saw_pass, saw_fail, sql_failure, ime_parent;
static int log_parent, switches, switch_failure, children, child_stop;
static u32 sqlite_bytes;
static MemStat snapshot;

static void say(const char *s)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(kstrlen(s)) : "memory");
}
#define CHECK(c) do { if (!(c)) { say("FAIL guest fixture: " #c "\n"); return 1; } } while (0)

static void __cdecl output(u8 color, const char *fmt, ...)
{
    (void)color;
    if (!kstrcmp(fmt, "raw_wait PASS normal %s next-KAPI app=%d\n")) saw_pass++;
    if (!kstrcmp(fmt, "raw_wait FAIL returned %s value=%d rc=%d\n")) saw_fail++;
}
static void *__cdecl alloc(u32 n) { return n == RAW_USER_BYTES ? allocation : 0; }
static void __cdecl release(void *p) { (void)p; frees++; }
static int __cdecl open_db(const char *p)
{ if (kstrcmp(p, ":memory:")) bad++; opens++; sqlite_bytes=8192; return 1; }
static int __cdecl close_db(int db) { (void)db; closes++; return 0; }
static int __cdecl sql(int db, const char *s)
{
    sql_calls++;
    if (db != 1 || !s[0]) bad++;
    /* This seam deliberately accepts only one statement, like kapi_db_exec.
     * A trailing semicolon is fine; SQL after it must fail the fixture. */
    for (const char *p=s; *p; p++) {
        if (*p == ';' && p[1]) { bad++; return -1; }
    }
    return sql_calls == sql_failure ? -1 : 0;
}
static u32 __cdecl db_bytes(void) { return sqlite_bytes; }
static i32 __cdecl stat(i32 id, void *out, u32 n)
{
    if (id != -1 || n != sizeof(MemStat)) bad++;
    *(MemStat *)out=snapshot; stats++; return sizeof(MemStat);
}
static int __cdecl read0(void) { selected=0; reads++; return returned; }
static int __cdecl read1(void) { selected=1; reads++; return returned; }
static int __cdecl read2(void) { selected=2; reads++; if (ime_parent) sqlite_bytes=12288; return returned; }
static int __cdecl read3(void) { selected=3; reads++; if (ime_parent) sqlite_bytes=12288; return returned; }
static int __cdecl read4(void) { selected=4; reads++; return returned; }
static void __cdecl mml(const char *s)
{ selected=5; reads++; if (kstrlen(s) != 1 && kstrlen(s) != RAW_MML_NOTES) bad++; }
static int __cdecl variant(int v)
{
    selected=7; reads++; switches++;
    if (v != 1) bad++;
    if (switch_failure) return -1;
    if (log_parent) sqlite_bytes=16384;
    return 0;
}
static int __cdecl child(const char *s)
{
    children++;
    if (ime_parent) {
        if (kstrcmp(s, ime_parent==1 ? RAW_CHILD "imechar stop" : RAW_CHILD "imekey stop")) bad++;
        if (reads < 2) bad++;
        /* The child repeats the same candidate search: its retained cache
         * grows only when the parent did not already warm it. */
        sqlite_bytes=12288;
    } else if (log_parent) {
        if (kstrcmp(s, child_stop ? RAW_CHILD "log stop" : RAW_CHILD "log done")) bad++;
        /* First switch retains resident dictionary state; subsequent switches
         * keep the same amount. A missing pre-switch must fail accounting. */
        (void)variant(1);
    } else if (kstrcmp(s, RAW_CHILD "kbdkey done")) bad++;
    snapshot.reclaim_count++;
    /* Each independent corruption must turn the guest's PASS into FAIL. */
    if (parent_case==1) snapshot.reclaim_count++;
    if (parent_case==2) snapshot.phys_free_pages--;
    if (parent_case==3) snapshot.owner_pages++;
    if (parent_case==4) sqlite_bytes++;
    if (parent_case==5) allocation[RAW_USER_BYTES-1]=0;
    return 0;
}
static int __cdecl result(int *kind, int *code)
{ *kind=ime_parent || child_stop || parent_case==6 ? EXEC_KIND_ABORTED : EXEC_KIND_EXITED;
  *code=ime_parent || child_stop ? EXEC_ERR_FAULT : 0; return 0; }

static void reset(void)
{
    selected=-1; reads=opens=closes=frees=bad=stats=sql_calls=saw_pass=saw_fail=0;
    returned='Q'; parent_case=sql_failure=ime_parent=0;
    log_parent=switches=switch_failure=children=child_stop=0;
    kmemset(&snapshot,0,sizeof(snapshot));
    snapshot.app_id=3; snapshot.owner_pages=37; snapshot.phys_free_pages=1000;
    snapshot.exec_heap_used=RAW_USER_BYTES;
}
static int test(void)
{
    const char *entries[] = {"kbdchar","kbdkey","imechar","imekey","serial","fm","db","log"};
    table.kprintf=output; table.mem_alloc=alloc; table.mem_free=release;
    table.db_open=open_db; table.db_close=close_db; table.db_exec=sql;
    table.db_mem_used=db_bytes; table.mem_stat=stat;
    table.kbd_getchar=read0; table.kbd_getkey=read1;
    table.ime_getchar=read2; table.ime_getkey=read3; table.serial_getchar=read4;
    table.fm_play_mml=mml; table.ime_switch_dict=variant;
    table.exec_run=child; table.exec_last_result=result;
    for (int i=0; i<8; i++) {
        reset();
        char *argv[]={"raw_wait_test",(char *)entries[i],"done"};
        CHECK(raw_guest_main(3,argv,&table)==0 && saw_pass==1 && !bad);
        CHECK(stats==2 && opens==1 && !closes && !frees);
        CHECK(i==6 ? sql_calls==4 && !reads : reads==1 && selected==i && sql_calls==3);
    }
    reset(); returned='X';
    char *wrong[]={"raw_wait_test","kbdkey","done"};
    CHECK(raw_guest_main(3,wrong,&table)==1 && saw_fail==1 && !saw_pass);
    reset();
    char *stopped[]={"raw_wait_test","kbdkey","stop"};
    CHECK(raw_guest_main(3,stopped,&table)==1 && saw_fail==1 && !saw_pass);
    for (int failure=0; failure<=6; failure++) {
        reset(); parent_case=failure;
        char *argv[]={"raw_wait_test","parent","kbdkey","done"};
        CHECK(raw_guest_main(4,argv,&table)==(failure ? 1 : 0));
        CHECK(!bad && stats==2 && sql_calls==3 && closes==1 && frees==1);
    }
    for (int mode=1; mode<=2; mode++) {
        for (int leak=0; leak<=1; leak++) {
            reset(); ime_parent=mode; parent_case=leak ? 4 : 0;
            char *argv[]={"raw_wait_test","parent",mode==1 ? "imechar" : "imekey","stop"};
            CHECK(raw_guest_main(4,argv,&table)==(leak ? 1 : 0));
            CHECK(!bad && reads==2 && closes==1 && frees==1);
        }
    }
    for (int failure=1; failure<=2; failure++) {
        reset(); sql_failure=failure;
        char *argv[]={"raw_wait_test","kbdkey","done"};
        CHECK(raw_guest_main(3,argv,&table)==1 && !reads && sql_calls==failure);
        reset(); sql_failure=failure;
        char *parent[]={"raw_wait_test","parent","kbdkey","done"};
        CHECK(raw_guest_main(4,parent,&table)==1 && stats==0 && sql_calls==failure);
    }
    for (int stop=0; stop<=1; stop++) {
        for (int leak=0; leak<=1; leak++) {
            reset(); log_parent=1; child_stop=stop; parent_case=leak ? 4 : 0;
            char *argv[]={"raw_wait_test","parent","log",stop ? "stop" : "done"};
            CHECK(raw_guest_main(4,argv,&table)==(leak ? 1 : 0));
            CHECK(!bad && switches==2 && stats==2 && children==1);
        }
    }
    reset(); log_parent=switch_failure=1;
    char *log[]={"raw_wait_test","parent","log","done"};
    CHECK(raw_guest_main(4,log,&table)==1 && !stats && !children);
    say("PASS guest fixture: log pre-switch before baseline, failure rejected, child leak still rejected\n");
    say("PASS guest fixture: settled resident FEP baseline, child SQLite leak rejected, separate CREATE/INSERT errors rejected\n");
    say("PASS guest fixture: eight raw entries, unexpected return/input rejected; parent rejects double reclaim/pool/USER/SQLite/sentinel/result mismatch\n");
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
