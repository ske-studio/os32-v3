/* ======================================================================== */
/*  PCM_TEST.C — CS4231 再生の CPL=3 受入 (KAPI v61)                        */
/*                                                                          */
/*  票: docs/tasks/v3/TASK_PCM_CS4231.md §3 の E3 / E5                      */
/*                                                                          */
/*  **左右で違う中身を出す** のが肝 (E3): 左 = 1kHz の正弦、右 = frame 番号の */
/*  下位 16 ビット。こうすると NP21/W の `/api/sound?pcm=1` で採った生 frame  */
/*  から、左右の取り違え・旧半分の反復・ミュートを**見分けられる**。         */
/*  右が単調に増えていれば欠落 0、左の周期が 44.1 frame なら速度が正しい。    */
/*                                                                          */
/*    pcm_test          5 秒鳴らし、1 秒ごとに pcm_status を出して close      */
/*    pcm_test short    1 frame だけ書いて close (drain の最短経路)           */
/*    pcm_test nodev    装置が無い機械で OS32_ERR_NOSYS が返ることの確認      */
/*                                                                          */
/*  [C1] C89 (GNU89)。main() は**このファイルの最初の関数**。                */
/* ======================================================================== */

#include "os32api.h"

#define PCM_RATE        44100
#define PCM_SECONDS     5
#define PCM_TONE_HZ     1000
#define PCM_CHUNK       512            /* 1 回の write で作る frame 数 */
#define PCM_SINE_LEN    1024           /* 正弦表の点数 (位相は 10.22 固定小数) */
#define PCM_SINE_AMP    20000          /* ±20000 (16 ビットの範囲に余裕) */
#define PCM_PHASE_BITS  16
#define PCM_FRAME_BYTES 4
#define PCM_MIN_API     61

/* 1 frame = 左 s16 + 右 s16。**リトルエンディアンの並びそのまま**書く。 */
struct pcm_frame {
    short left;
    short right;
};

static short  sine_tab[PCM_SINE_LEN];
static struct pcm_frame chunk[PCM_CHUNK];

static void sine_init(void);
static void fill_chunk(u32 first_frame, u32 n);
static void show_status(KernelAPI *api, u32 sec);
static int  run_stream(KernelAPI *api, u32 total_frames, int report);

void main(int argc, char **argv, KernelAPI *api)
{
    int rc;
    u32 total;

    if (api->version < PCM_MIN_API) {
        api->kprintf(0x41, "KAPI v%d < %d: pcm_* absent\n",
                     api->version, PCM_MIN_API);
        return;
    }

    /* --- nodev: 装置の無い機械で「無い」と言えること --- */
    if (argc > 1 && argv[1][0] == 'n') {
        rc = api->pcm_open(PCM_RATE);
        if (rc == OS32_ERR_NOSYS) {
            api->kprintf(0xE1, "nodev PASS: pcm_open -> NOSYS (%d)\n", rc);
        } else {
            api->kprintf(0x41, "nodev FAIL: pcm_open -> %d (expected %d)\n",
                         rc, OS32_ERR_NOSYS);
            if (rc == 0) api->pcm_close();
        }
        return;
    }

    rc = api->pcm_open(PCM_RATE);
    if (rc != 0) {
        api->kprintf(0x41, "pcm_open(%d) -> %d\n", PCM_RATE, rc);
        return;
    }
    api->kprintf(0xE1, "pcm_open(%d) OK\n", PCM_RATE);

    sine_init();

    /* --- short: 1 frame だけ書いて close (drain の最短経路) --- */
    if (argc > 1 && argv[1][0] == 's') {
        fill_chunk(0, 1);
        rc = api->pcm_write(chunk, PCM_FRAME_BYTES);
        api->kprintf(rc == PCM_FRAME_BYTES ? 0xE1 : 0x41,
                     "short: pcm_write(4) -> %d\n", rc);
        rc = api->pcm_close();
        api->kprintf(rc == 0 ? 0xC1 : 0x41, "short: pcm_close -> %d\n", rc);
        return;
    }

    total = (u32)PCM_RATE * PCM_SECONDS;
    rc = run_stream(api, total, 1);

    /* close は drain (期限つき)。**「PI が来ない」は証拠にしない** ので、
     * 戻り値が 0 でなければ drain が失敗したということ。 */
    rc = api->pcm_close();
    api->kprintf(rc == 0 ? 0xC1 : 0x41, "pcm_close -> %d\n", rc);
}

/* ------------------------------------------------------------------------ */
/*  正弦表 — 放物線近似 (整数だけ)。                                         */
/*    sin(θ) ≒ (4θ/π)(1 − |θ|/π)  を θ = π·x/512 で整数化すると              */
/*    y = x·(512 − |x|) / 65536  (最大 1.0 は |x| = 256 のとき)              */
/*  1kHz が要るだけなので、波形の純度より「周期が 44.1 frame」が本体。        */
/* ------------------------------------------------------------------------ */
static void sine_init(void)
{
    int i, x, a;
    long v;

    for (i = 0; i < PCM_SINE_LEN; i++) {
        x = (i * 1024) / PCM_SINE_LEN - 512;      /* -512 .. 511 */
        a = (x < 0) ? -x : x;
        v = (long)x * (long)(512 - a);            /* -65536 .. 65536 */
        sine_tab[i] = (short)((v * PCM_SINE_AMP) / 65536L);
    }
}

/* first_frame から n frame ぶん作る。左 = 正弦、右 = frame 番号の下位 16。 */
static void fill_chunk(u32 first_frame, u32 n)
{
    u32 i, ph, inc;

    /* 位相の刻み = PCM_SINE_LEN × TONE_HZ / RATE を 16 ビット固定小数で。
     * 1024 × 1000 × 65536 / 44100 = 1521742 (44.1 frame で 1 周する)。 */
    /* (1024 × 1000) << 16 は 32 ビットを溢れる (E3 で左が 40Hz になった)。
     * 12 ビット分だけ先に上げて割り、残り 4 ビットを後で上げる。 */
    inc = ((((u32)PCM_SINE_LEN * PCM_TONE_HZ) << (PCM_PHASE_BITS - 4)) / (u32)PCM_RATE)
          << 4;
    ph = (u32)(first_frame * inc);
    for (i = 0; i < n; i++) {
        chunk[i].left = sine_tab[(ph >> PCM_PHASE_BITS) & (PCM_SINE_LEN - 1)];
        chunk[i].right = (short)((first_frame + i) & 0xFFFFu);
        ph += inc;
    }
}

static void show_status(KernelAPI *api, u32 sec)
{
    u32 freeb = 0, cnt = 0;
    int rc;

    rc = api->pcm_status(&freeb, &cnt);
    if (rc != 0) {
        api->kprintf(0x41, "  t=%us pcm_status -> %d\n", sec, rc);
        return;
    }
    /* counters = (underruns<<24) | (repeats<<16) | resyncs */
    api->kprintf(0xE1, "  t=%us free=%u under=%u rep=%u resync=%u\n",
                 sec, freeb, (cnt >> 24) & 0xFFu, (cnt >> 16) & 0xFFu,
                 cnt & 0xFFFFu);
}

/* 書き切るまで回す。満杯 (0) は sys_yield して再試行 — **driver の中では
 * 待たない**ので、待つのはここ。戻り 0 = 全部渡せた。 */
static int run_stream(KernelAPI *api, u32 total_frames, int report)
{
    u32 done = 0, want, next_report = (u32)PCM_RATE;
    u32 sec = 0;
    int rc;

    while (done < total_frames) {
        want = total_frames - done;
        if (want > (u32)PCM_CHUNK) want = (u32)PCM_CHUNK;
        fill_chunk(done, want);
        rc = api->pcm_write(chunk, want * PCM_FRAME_BYTES);
        if (rc < 0) {
            api->kprintf(0x41, "pcm_write -> %d (at frame %u)\n", rc, done);
            return rc;
        }
        if (rc == 0) {
            api->sys_yield();          /* 満杯。ステージングが空くまで譲る */
            continue;
        }
        done += (u32)rc / PCM_FRAME_BYTES;
        if (report && done >= next_report) {
            sec++;
            next_report += (u32)PCM_RATE;
            show_status(api, sec);
        }
    }
    if (report) api->kprintf(0xE1, "wrote %u frames (%d s)\n",
                             done, PCM_SECONDS);
    return 0;
}
