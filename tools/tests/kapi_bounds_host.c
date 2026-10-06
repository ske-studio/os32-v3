/* Real target bodies; only MMIO and unused dependencies are substituted. */
#include "types.h"
#include "memmap.h"
#include "tvram.h"
#include "io.h"

static u16 memory[(TVRAM_CG_WINDOW - TVRAM_CHAR_BASE) / 2 + 2];
static u16 snapshot[sizeof(memory) / sizeof(memory[0])];
static u16 outside;
static unsigned accesses, ports;
static void *host_mmio(u32 addr)
{
    accesses++;
    if (addr < TVRAM_CHAR_BASE - 2 || addr >= TVRAM_CG_WINDOW + 2)
        return &outside;
    return (u8 *)memory + (addr - (TVRAM_CHAR_BASE - 2));
}
static void host_out(unsigned p, unsigned v) { (void)p; (void)v; ports++; }
#undef P2V_IO
#define P2V_IO(p) host_mmio((u32)(p))
#define outp(p,v) host_out(p,v)
static unsigned host_in(unsigned p) { static unsigned n; (void)p; return (++n & 1) ? 0x20 : 0; }
#define inp(p) host_in(p)
#define io_wait_n(n) ((void)(n))
#include "console_source.inc"
#include "fm_source.inc"
#include "os32_kapi_shared.h"
static u8 db_memory[DB_SHM_RESULT_LIMIT + 4096] __attribute__((aligned(4)));
#undef MEM_SHM_BASE
#define MEM_SHM_BASE db_memory
/* This bounds fixture uses the direct TRUSTED caller convention. */
int ring3_call_from_user(void) { return 0; }
int res_owner_get(void) { return 1; }
#include "db_source.inc"
#include "gfx_core_source.inc"
#include "gfx_vram_source.inc"
/* Only the palette boundary is hardware-independent here. */
static unsigned palette_calls;
static int packed;
static int host_query(GFX_ScreenInfo *si)
{
    si->lease_first = packed ? 16 : 0;
    si->lease_count = packed ? 240 : 0;
    si->lease_mask = 0xffff;
    return 0;
}
static void host_palette(int first, int count, const u8 *rgb)
{ (void)first; (void)count; (void)rgb; palette_calls++; }
GfxBackend gfx_backend_pc98 = {.query=host_query, .set_palette=host_palette};
/* Retained by raster's real call graph, never used by rejected calls. */
int vram_scroll_y;
void cpu_delay_us(u32 n) { (void)n; }


static void say(const char *s, unsigned n)
{
    unsigned call=4;
    __asm__ volatile("int $0x80" : "+a"(call) : "b"(1), "c"(s), "d"(n) : "memory");
}
static unsigned checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { say("FAIL " msg "\n", sizeof("FAIL " msg "\n")-1); return 1; } } while (0)
static int test(void)
{
    unsigned i, j;
    u16 code;
    u8 attr;
    const int invalid[][2] = {
        {-1,0}, {0,-1}, {TVRAM_COLS,0}, {0,TVRAM_ROWS},
        {TVRAM_COLS,TVRAM_ROWS-1}, {0,TVRAM_ROWS_30},
        {0x7fffffff,0}, {0,0x7fffffff}, {(-0x7fffffff-1),0},
        {0,(-0x7fffffff-1)}, {0x40000000,0}, {0,0x08000000},
        {0x7fffffff,1}
    };
    for (i=0; i<sizeof(memory)/sizeof(memory[0]); i++) memory[i]=0xa55a;
    tvram_putchar_at(0,0,'A',0x23);
    CHECK(memory[1]=='A', "putchar first");
    tvram_putchar_at(TVRAM_COLS-1,TVRAM_ROWS-1,'Z',0x45);
    tvram_readchar_at(TVRAM_COLS-1,TVRAM_ROWS-1,&code,&attr);
    CHECK(code=='Z' && attr==0x45, "readchar last");
    tvram_readchar_at(0,0,0,0);
    tvram_putkanji_at(TVRAM_COLS-2,TVRAM_ROWS-1,0x2422,0x67);
    tvram_readchar_at(TVRAM_COLS-2,TVRAM_ROWS-1,&code,&attr);
    CHECK(code==0x2204 && attr==0x67, "kanji left");
    tvram_readchar_at(TVRAM_COLS-1,TVRAM_ROWS-1,&code,&attr);
    CHECK(code==0x2284 && attr==0x67, "kanji right");
    CHECK(memory[0]==0xa55a && memory[sizeof(memory)/sizeof(memory[0])-1]==0xa55a,
          "outer sentinels");
    for (i=0; i<sizeof(memory)/sizeof(memory[0]); i++) snapshot[i]=memory[i];
    for (i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
        accesses=0;
        tvram_putchar_at(invalid[i][0],invalid[i][1],'!',0x99);
        CHECK(accesses==0, "putchar bounds");
        tvram_putkanji_at(invalid[i][0],invalid[i][1],0x2422,0x99);
        CHECK(accesses==0, "kanji bounds");
        code=0x1234; attr=0x56;
        tvram_readchar_at(invalid[i][0],invalid[i][1],&code,&attr);
        CHECK(accesses==0 && code==0x1234 && attr==0x56, "readchar bounds");
        for (j=0; j<sizeof(memory)/sizeof(memory[0]); j++)
            if (memory[j]!=snapshot[j]) break;
        CHECK(j==sizeof(memory)/sizeof(memory[0]), "all TVRAM and sentinels unchanged");
    }
    accesses=0;
    tvram_putkanji_at(TVRAM_COLS-1,0,0x2422,0x99);
    tvram_putkanji_at(TVRAM_COLS-1,TVRAM_ROWS-1,0x2422,0x99);
    CHECK(accesses==0, "kanji row edge");
    ports=0;
    fm_set_tone_num(0,0);
    CHECK(ports>0, "tone valid");
    ports=0;
    fm_set_tone_num(0,-1);
    CHECK(ports==0, "tone bounds");
    fm_set_tone_num(0,(-0x7fffffff-1));
    fm_set_tone_num(0,NUM_TONES);
    fm_set_tone_num(0,0x7fffffff);
    CHECK(ports==0, "tone extremes");
    fm_note_on(0,0);
    CHECK(ports>0, "note valid");
    ports=0;
    fm_note_on(0,-12); /* Safe old index=0, but invalid note: runtime RED, not SIGSEGV. */
    CHECK(ports==0, "note bounds");
    fm_note_on(0,-1);
    fm_note_on(0,(-0x7fffffff-1));
    CHECK(ports==0, "note extremes");
    {
        DB_ResultHeader *hdr = (DB_ResultHeader *)db_memory;
        DB_ColumnInfo *cols = (DB_ColumnInfo *)(db_memory + sizeof(*hdr));
        const char *empty = (const char *)db_memory + DB_SHM_EMPTY_OFFSET;
        db_slots[0].in_use=1;
        db_slots[0].active_stmt=(sqlite3_stmt *)1;
        hdr->column_count=1;
        cols[0].data_offset=sizeof(*hdr)+sizeof(*cols);
        CHECK(kapi_db_column_text(0,0)==(const char *)db_memory+cols[0].data_offset,
              "DB valid");
        hdr->column_count=0x7fffffff;
        cols[(DB_SHM_RESULT_LIMIT-sizeof(*hdr))/sizeof(*cols)].data_offset=1;
        CHECK(kapi_db_column_text(0,(DB_SHM_RESULT_LIMIT-sizeof(*hdr))/sizeof(*cols))==empty,
              "DB column bounds");
        CHECK(kapi_db_column_text(0,0x10000000)==empty, "DB huge column");
        CHECK(kapi_db_column_text(0,-1)==empty, "DB negative column");
        CHECK(kapi_db_column_text(0,(DB_SHM_RESULT_LIMIT-sizeof(*hdr))/sizeof(*cols))==empty,
              "DB capacity");
        cols[0].data_offset=DB_SHM_RESULT_LIMIT;
        CHECK(kapi_db_column_text(0,0)==empty, "DB offset bounds");
        cols[0].data_offset=-1;
        CHECK(kapi_db_column_text(0,0)==empty, "DB negative offset");
        CHECK(kapi_db_column_text(-1,0)==empty, "DB handle bounds");
    }
    {
        u8 rgb[3]={0};
        for (packed=0;packed<=1;packed++) {
            int first=packed ? 16 : 1;
            CHECK(gfx_lease_palette(first,1,rgb)==0, "palette valid");
            palette_calls=0;
            CHECK(gfx_lease_palette(0x7fffffff,1,rgb)==OS32_ERR_INVAL && !palette_calls,
                  "palette first bounds");
            CHECK(gfx_lease_palette(first,0x7fffffff,rgb)==OS32_ERR_INVAL && !palette_calls,
                  "palette count bounds");
        }
        int x=1,y=1,w=0x7fffffff,h=0x7fffffff;
        CHECK(gfx_clip_screen(&x,&y,&w,&h,GFX_WIDTH,GFX_HEIGHT) &&
              w==GFX_WIDTH-1 && h==GFX_HEIGHT-1, "clip overflow");
        x=(-0x7fffffff-1); y=0; w=-1; h=1;
        CHECK(!gfx_clip_screen(&x,&y,&w,&h,GFX_WIDTH,GFX_HEIGHT), "clip negative");
        bb_b=(u8 *)memory;
        dirty_queue.count=0;
        gfx_add_dirty_rect(GFX_WIDTH,0,0x7fffffff,1);
        CHECK(!dirty_queue.count, "dirty outside");
        gfx_add_dirty_rect(1,1,0x7fffffff,0x7fffffff);
        CHECK(dirty_queue.count==1 && dirty_queue.rects[0].x==0 &&
              dirty_queue.rects[0].w==GFX_WIDTH && dirty_queue.rects[0].h==GFX_HEIGHT-1,
              "dirty clipped");
        dirty_queue.count=0;
        GFX_RasterPalTable table={0};
        u32 commits=gfx_counters.commits;
        table.count=-1;
        gfx_present_raster(&table);
        CHECK(gfx_counters.commits==commits, "raster negative count");
        table.count=GFX_RASTER_MAX_ENTRIES+1;
        gfx_present_raster(&table);
        CHECK(gfx_counters.commits==commits, "raster count bounds");
    }
    say("PASS kapi bounds checks=",sizeof("PASS kapi bounds checks=")-1);
    { char n[12]; unsigned len=0;
      do { n[len++]=(char)('0'+checks%10); checks/=10; } while(checks);
      while(len) say(&n[--len],1);
      say("\n",1);
    }
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
