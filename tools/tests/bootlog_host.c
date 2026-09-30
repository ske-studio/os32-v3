/* ======================================================================== */
/*  bootlog_host.c — kernel/bootlog.c のホスト試験                           */
/*                                                                          */
/*  kernel/bootlog.c を 1 行も写さずに #include する (模型ではない)。        */
/*  ホスト側だけ -DBOOTLOG_NO_IRQ_LOCK (CPL=3 では cli/popfl を実行できない): */
/*  錠はここが用意し、呼んだ回数・順序・返した札の復元を数える。             */
/*  書き出しの手順は偽の VFS (呼ばれた順を記録し、段ごとに失敗を注入する。   */
/*  write の戻りは実物と同じく FS ごと: ext2 は 0、FAT はバイト数)。         */
/*                                                                          */
/*  使い方: bootlog_host <case>   (case は main() の表)                     */
/*  記録: tools/tests/bootlog_tdd.md                                        */
/* ======================================================================== */

#include <stdio.h>
#include <string.h>

#include "bootlog.c"

/* ---- 偽の錠 (irq_save / irq_restore の代わり) ----
 * lock は毎回違う札 (EFLAGS のつもり) を返す。unlock はその札を、入れ子
 * なしで、直前の lock のものと同じ順で受け取らなければならない。 */
static int g_lk_depth, g_lk_locks, g_lk_unlocks, g_lk_bad;
static unsigned int g_lk_seq, g_lk_last;

unsigned int bootlog_lock(void)
{
    if (g_lk_depth != 0) g_lk_bad++;          /* 入れ子 (cli の二重掛け) */
    g_lk_depth++;
    g_lk_locks++;
    g_lk_last = 0x200u + (++g_lk_seq);
    return g_lk_last;
}

void bootlog_unlock(unsigned int f)
{
    if (g_lk_depth != 1) g_lk_bad++;          /* 掛けていないのに戻す */
    if (f != g_lk_last) g_lk_bad++;           /* 違う札で戻す (IF が化ける) */
    g_lk_depth--;
    g_lk_unlocks++;
}

static void lk_reset(void)
{
    g_lk_depth = g_lk_locks = g_lk_unlocks = g_lk_bad = 0;
    g_lk_seq = g_lk_last = 0;
}

static int g_fail;

#define CHECK(cond, name) do { \
    if (cond) { printf("  ok   %s\n", name); } \
    else { printf("  FAIL %s\n", name); g_fail++; } \
} while (0)

static void reset(void)
{
    memset(g_bl_buf, 0, sizeof(g_bl_buf));
    g_bl_len = 0;
    g_bl_dropped = 0;
    g_bl_full = 0;
    g_bl_stopped = 0;
    lk_reset();
}

static const char *text(void) { return &g_bl_buf[BL_TEXT_OFF]; }

/* 本文を n バイトの 'x' で埋める (容量の端を作る) */
static void fill(u32 n)
{
    static char blk[256];
    memset(blk, 'x', sizeof(blk));
    while (n > 0) {
        u32 k = n > sizeof(blk) ? (u32)sizeof(blk) : n;
        bootlog_push(blk, k);
        n -= k;
    }
}

/* ------------------------------------------------------------------------ */
/*  1. 溜める                                                                */
/* ------------------------------------------------------------------------ */
static void case_collect(void)
{
    reset();
    CHECK(bootlog_is_active(), "1a 最初から積む (BSS のまま)");
    bootlog_push("abc", 3);
    bootlog_push("def\n", 4);
    bootlog_push(0, 5);
    bootlog_push("zzz", 0);
    CHECK(bootlog_len() == 7, "1b 長さは積んだ分だけ (NULL と長さ 0 は積まない)");
    CHECK(memcmp(text(), "abcdef\n", 7) == 0, "1c 順番どおり、属性は入らない");
    CHECK(bootlog_dropped() == 0, "1d 容量内では捨てない");
}

/* ------------------------------------------------------------------------ */
/*  2. あふれ — 後ろを捨てて先頭を残す                                       */
/* ------------------------------------------------------------------------ */
static void case_overflow(void)
{
    reset();
    fill(BL_TEXT_MAX - 5);
    bootlog_push("HEAD!", 5);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 0,
          "2a ちょうど満杯は捨てない");
    bootlog_push("tail", 4);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 4,
          "2b 満杯の後は新しい方を捨てて数える");
    CHECK(memcmp(text() + BL_TEXT_MAX - 5, "HEAD!", 5) == 0,
          "2c 先頭側 (先に来た方) が残る");

    reset();
    fill(BL_TEXT_MAX - 5);
    bootlog_push("0123456789", 10);
    CHECK(bootlog_len() == BL_TEXT_MAX, "2d 入る分だけ写す");
    CHECK(memcmp(text() + BL_TEXT_MAX - 5, "01234", 5) == 0, "2e 写したのは頭の 5 バイト");
    CHECK(bootlog_dropped() == 5, "2f 捨てたのは残りの 5 バイト");
    CHECK(g_bl_buf[BL_TEXT_OFF + BL_TEXT_MAX] == 0, "2g 本文の外 (末尾行の置き場) を踏まない");
}

/* ------------------------------------------------------------------------ */
/*  3. あふれの境目は UTF-8 の切れ目、一度あふれたら以後は全部捨てる          */
/* ------------------------------------------------------------------------ */
static void case_utf8_latch(void)
{
    /* "a" + あ (E3 81 82) + い (E3 81 84) */
    static const char s[] = "a\xE3\x81\x82\xE3\x81\x84";

    reset();
    fill(BL_TEXT_MAX - 4);
    bootlog_push(s, 7);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 3,
          "3a 4 バイトの空きには a+あ が入る");
    reset();
    fill(BL_TEXT_MAX - 3);
    bootlog_push(s, 7);
    CHECK(bootlog_len() == BL_TEXT_MAX - 2, "3b 3 バイトの空きには a だけ (あ を割らない)");
    CHECK(bootlog_dropped() == 6, "3c 割らずに捨てた分も数える");
    CHECK(text()[BL_TEXT_MAX - 3] == 'a', "3d 残った a");
    bootlog_push("ok", 2);
    CHECK(bootlog_len() == BL_TEXT_MAX - 2 && bootlog_dropped() == 8,
          "3e 空きが残っていても一度あふれたら後から来た行は捨てる");

    /* push をまたぐ文字 (console.c の kputc は 1 バイトずつ積む) */
    reset();
    fill(BL_TEXT_MAX - 1);
    bootlog_push("\xE3", 1);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 0,
          "3f 残り 1 バイトに文字の頭 E3 は収まる (続きが来るかもしれない)");
    bootlog_push("\x81\x82", 2);
    CHECK(bootlog_len() == BL_TEXT_MAX - 1, "3g 続きが捨てられたら、蓄えた頭 E3 も戻す");
    CHECK(bootlog_dropped() == 3, "3h 戻した 1 バイトも捨てた数に入る (2 + 1)");
    CHECK(text()[BL_TEXT_MAX - 2] == 'x', "3i 末尾は境界の手前の文字");

    reset();
    fill(BL_TEXT_MAX - 2);
    bootlog_push("\xE3", 1);
    bootlog_push("\x81", 1);
    bootlog_push("\x82", 1);
    CHECK(bootlog_len() == BL_TEXT_MAX - 2 && bootlog_dropped() == 3,
          "3j 1 バイトずつ来た 3 バイト文字の 3 バイト目が入らなければ 2 バイト戻す");

    reset();
    fill(BL_TEXT_MAX - 3);
    bootlog_push("\xF0\x9F\x98", 3);
    bootlog_push("\x81", 1);
    CHECK(bootlog_len() == BL_TEXT_MAX - 3 && bootlog_dropped() == 4,
          "3k 4 バイト文字 (F0 9F 98 81) も頭まで戻す");

    reset();
    fill(BL_TEXT_MAX - 3);
    bootlog_push("\xE3\x81\x82", 3);
    bootlog_push("\xE3", 1);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 1,
          "3l 末尾が完結した文字なら戻さない");

    reset();
    fill(BL_TEXT_MAX - 2);
    bootlog_push("\xC3\xA9", 2);        /* é */
    bootlog_push("z", 1);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 1,
          "3m 2 バイト文字が完結していれば戻さない");

    reset();
    fill(BL_TEXT_MAX - 1);
    bootlog_push("\x81", 1);             /* 頭の無い継続バイト (不正) */
    bootlog_push("q", 1);
    CHECK(bootlog_len() == BL_TEXT_MAX && bootlog_dropped() == 1,
          "3n 頭の無い継続バイトは文字と見なさず、触らない");

    reset();
    bootlog_push("\xE3", 1);
    bootlog_push("\x81", 1);
    bootlog_push("\x82", 1);
    CHECK(bootlog_len() == 3 && bootlog_dropped() == 0,
          "3o あふれていなければ途中の文字も戻さない (続きが来る)");
}

/* ------------------------------------------------------------------------ */
/*  3'. 錠 — 呼んだ回数と順序、札 (IF) の復元                                */
/* ------------------------------------------------------------------------ */
static void case_lock(void)
{
    u32 n;

    reset();
    bootlog_push("abc", 3);
    CHECK(g_lk_locks == 1 && g_lk_unlocks == 1 && g_lk_depth == 0 && g_lk_bad == 0,
          "L1 push は錠を 1 回掛けて 1 回戻す (同じ札で)");
    fill(BL_TEXT_MAX);                    /* あふれる */
    bootlog_push("more", 4);              /* 満杯の後の経路 */
    CHECK(g_lk_depth == 0 && g_lk_bad == 0 && g_lk_locks == g_lk_unlocks,
          "L2 あふれた経路も満杯の後の経路も戻す");
    lk_reset();
    bootlog_push(0, 3);
    bootlog_push("x", 0);
    CHECK(g_lk_locks == 0, "L3 NULL と長さ 0 は錠を掛けずに帰る");
    bootlog_compose("# H\n", &n);
    CHECK(g_lk_locks == 1 && g_lk_unlocks == 1 && g_lk_bad == 0,
          "L4 compose は 1 回掛けて戻す");
    lk_reset();
    bootlog_stop();
    CHECK(g_lk_locks == 1 && g_lk_unlocks == 1 && g_lk_bad == 0,
          "L5 stop は 1 回掛けて戻す");
    lk_reset();
    bootlog_push("after", 5);
    CHECK(g_lk_locks == 0, "L6 止めた後の push は錠を掛けない (費用は 1 回の読み)");
    (void)bootlog_len(); (void)bootlog_dropped(); (void)bootlog_is_active();
    CHECK(g_lk_locks == 0, "L7 読むだけの API は錠を掛けない");
}

/* ------------------------------------------------------------------------ */
/*  4. 止める                                                                */
/* ------------------------------------------------------------------------ */
static void case_stop(void)
{
    reset();
    bootlog_push("before\n", 7);
    bootlog_stop();
    bootlog_push("after\n", 6);
    CHECK(!bootlog_is_active(), "4a 止めた後は active でない");
    CHECK(bootlog_len() == 7, "4b 止めた後は積まない");
    CHECK(bootlog_dropped() == 0, "4c 止めた後の出力は「捨てた」に数えない");
}

/* ------------------------------------------------------------------------ */
/*  5. ヘッダ                                                                */
/* ------------------------------------------------------------------------ */
static void case_header(void)
{
    char buf[BOOTLOG_HDR_MAX];
    char small[24];
    BootlogHeaderInfo hi;
    u32 n;

    memset(&hi, 0, sizeof(hi));
    hi.build = "Sep 25 2026 17:14:00";
    hi.commit = "1d4ac40";
    hi.crc_valid = 1;
    hi.image_crc = 0x0012ABCDUL;
    hi.image_size = 452491UL;
    hi.ticks = 1234UL;
    n = bootlog_format_header(buf, sizeof(buf), &hi);
    CHECK(strcmp(buf, "# OS32 boot log  Build Sep 25 2026 17:14:00  Commit 1d4ac40"
                      "  Image CRC 0012ABCD (452491 bytes)  uptime-ticks 1234\n") == 0,
          "5a ヘッダの全文 (CRC は 8 桁の 16 進)");
    CHECK(n == strlen(buf), "5b 戻りは長さ");

    hi.crc_valid = 0;
    hi.build = 0;
    hi.ticks = 4294967295UL;
    bootlog_format_header(buf, sizeof(buf), &hi);
    CHECK(strcmp(buf, "# OS32 boot log  Build ?  Commit 1d4ac40"
                      "  Image CRC none  uptime-ticks 4294967295\n") == 0,
          "5c CRC が無ければ none、Build が無ければ ?、tick は 10 桁まで");

    memset(small, 'Z', sizeof(small));
    n = bootlog_format_header(small, sizeof(small), &hi);
    CHECK(n == sizeof(small) - 1 && small[n - 1] == '\n' && small[n] == '\0',
          "5d 収まらなければ切って、それでも 1 行 (改行 + NUL) で終わる");
    CHECK(bootlog_format_header(small, 1, &hi) == 0, "5e cap < 2 は書かない");
}

/* ------------------------------------------------------------------------ */
/*  6. 書き出し用の 1 本                                                     */
/* ------------------------------------------------------------------------ */
static int eq(const char *got, u32 n, const char *want)
{
    return n == (u32)strlen(want) && memcmp(got, want, n) == 0;
}

static void case_compose(void)
{
    const char *out, *out2;
    u32 n, n2;

    reset();
    bootlog_push("[selftest] 9/9 passed\n", 22);
    out = bootlog_compose("# H\n", &n);
    CHECK(eq(out, n, "# H\n[selftest] 9/9 passed\n"
                     "# end  kept 22 bytes  dropped 0 bytes\n"),
          "6a ヘッダ + 本文 + 末尾行が連続した 1 本");
    out2 = bootlog_compose("# H\n", &n2);
    CHECK(out2 == out && n2 == n && bootlog_len() == 22,
          "6b 何度組んでも同じ (本文は動かない)");

    reset();
    bootlog_push("no newline", 10);
    out = bootlog_compose("# H\n", &n);
    CHECK(memcmp(out, "# H\nno newline\n# end  kept 10 bytes", 35) == 0,
          "6c 本文が行の途中で切れていたら改行を足してから末尾行");
    bootlog_push("+more", 5);
    out = bootlog_compose("# H\n", &n);
    CHECK(memcmp(out + 4, "no newline+more\n", 16) == 0,
          "6d 組んだ後に積んでも本文は続きから (足した改行は本文ではない)");

    reset();
    out = bootlog_compose("# H\n", &n);
    CHECK(eq(out, n, "# H\n# end  kept 0 bytes  dropped 0 bytes\n"),
          "6e 空でもヘッダと末尾行は出る");

    reset();
    fill(BL_TEXT_MAX - 1);
    bootlog_push("ab", 2);
    out = bootlog_compose("# H\n", &n);
    CHECK(n > 4 + BL_TEXT_MAX &&
          eq(out + 4 + BL_TEXT_MAX, n - 4 - BL_TEXT_MAX,
             "\n# end  kept 16384 bytes  dropped 1 bytes\n"),
          "6f あふれたときも末尾行は置き場に収まり、捨てた数を書く");
    CHECK(out == &g_bl_buf[BL_TEXT_OFF - 4], "6g ヘッダは本文の直前に右詰め");
}

/* ------------------------------------------------------------------------ */
/*  7. 書き出し先の種別と FAT の 8.3                                         */
/* ------------------------------------------------------------------------ */

/* FatFs (FF_USE_LFN 0) の create_name と同じ判定: 本体 1〜8、ドットは 1 個
 * まで、拡張子 0〜3、禁止文字なし。 */
static int is_83_component(const char *s, u32 len)
{
    u32 i, body = 0, ext = 0;
    int dot = 0;
    if (len == 0) return 0;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c == '.') { if (dot || body == 0) return 0; dot = 1; continue; }
        if (strchr("*+,:;<=>[]|\"?\x7F ", c)) return 0;
        if (dot) ext++; else body++;
    }
    return body <= 8 && ext <= 3;
}

static int is_83_path(const char *p)
{
    while (*p) {
        const char *q;
        while (*p == '/') p++;
        q = p;
        while (*q && *q != '/') q++;
        if (q > p && !is_83_component(p, (u32)(q - p))) return 0;
        p = q;
    }
    return 1;
}

static void case_plan(void)
{
    CHECK(bootlog_plan("ext2") == BOOTLOG_FS_EXT2, "7a ext2 は書く");
    CHECK(bootlog_plan("fat") == BOOTLOG_FS_FAT, "7b fat は書く (8.3)");
    CHECK(bootlog_plan("hostdrv") == BOOTLOG_FS_SKIP, "7c hostdrv は書かない");
    CHECK(bootlog_plan("iso9660") == BOOTLOG_FS_SKIP, "7d iso9660 は書かない");
    CHECK(bootlog_plan("serialfs") == BOOTLOG_FS_SKIP, "7e serialfs は書かない");
    CHECK(bootlog_plan(0) == BOOTLOG_FS_SKIP, "7f 未マウント (NULL) は書かない");
    CHECK(bootlog_plan("ext") == BOOTLOG_FS_SKIP && bootlog_plan("ext2x") == BOOTLOG_FS_SKIP,
          "7g 名前は完全一致");
    CHECK(strcmp(bootlog_old_path(BOOTLOG_FS_EXT2), "/var/log/boot.log.1") == 0,
          "7h ext2 の前回分は boot.log.1");
    CHECK(bootlog_old_path(BOOTLOG_FS_SKIP) == 0, "7i 書かない種別に名前は無い");
    CHECK(is_83_path(bootlog_old_path(BOOTLOG_FS_FAT)), "7j FAT の前回分は 8.3");
    CHECK(is_83_path(SYS_BOOTLOG_FILE) && is_83_path(SYS_BOOTLOG_DIR) &&
          is_83_path(SYS_BOOTLOG_VAR_DIR), "7k /var /var/log /var/log/boot.log は 8.3");
    CHECK(!is_83_path("/var/log/boot.log.1"), "7l (判定の対照) boot.log.1 は 8.3 でない");
}

/* ------------------------------------------------------------------------ */
/*  8. 書き出しの手順 (偽の VFS)                                             */
/* ------------------------------------------------------------------------ */

#define FK_MAX   8
#define FK_ERR_IO (-7)

/* 名前 (エントリ) と実体 (inode) を分けて持つ — ext2 の rename は新名を載せて
 * から旧名を消すので、途中で落ちると 2 つの名前が同じ inode を指す。その
 * 状態を作って「次の起動」を回すため。 */
typedef struct { char path[40]; int ino; int used; } FkEnt;
typedef struct { char data[64]; u32 len; int isdir; int used; } FkIno;

static FkEnt  fk[FK_MAX];
static FkIno  fki[FK_MAX];
static char   fk_log[512];
static int    fk_fat;             /* 1 = FAT: rename は宛先があると EXIST、write はバイト数 */
static const char *fk_fail_op;    /* この名前の操作で fk_fail_rc を返す */
static const char *fk_fail_path;  /* (任意) このパスのときだけ */
static int    fk_fail_rc;
static int    fk_short;           /* FAT: write が 1 バイト少なく返す (ディスク満杯) */
static int    fk_rename_n;        /* rename を呼んだ回数 */
static int    fk_rename_after_link; /* ext2: この回の rename が新名を載せた後 (旧名を消す前) に落ちる */

#define P_LOG  "/var/log/boot.log"
#define P_NEW  "/var/log/boot.new"
#define P_OLD  "/var/log/boot.log.1"
#define P_OLD8 "/var/log/bootlog.1"

static void fk_reset(int fat)
{
    memset(fk, 0, sizeof(fk));
    memset(fki, 0, sizeof(fki));
    fk_log[0] = '\0';
    fk_fat = fat;
    fk_fail_op = 0;
    fk_fail_path = 0;
    fk_fail_rc = 0;
    fk_short = 0;
    fk_rename_n = 0;
    fk_rename_after_link = 0;
}

static FkEnt *fk_find(const char *p)
{
    int i;
    for (i = 0; i < FK_MAX; i++) if (fk[i].used && strcmp(fk[i].path, p) == 0) return &fk[i];
    return 0;
}

static int fk_new_ino(int isdir, const char *data)
{
    int i;
    for (i = 0; i < FK_MAX; i++) {
        if (!fki[i].used) {
            fki[i].used = 1;
            fki[i].isdir = isdir;
            fki[i].data[0] = '\0';
            fki[i].len = 0;
            if (data) { strcpy(fki[i].data, data); fki[i].len = (u32)strlen(data); }
            return i;
        }
    }
    return -1;
}

static FkEnt *fk_link(const char *p, int ino)
{
    int i;
    for (i = 0; i < FK_MAX; i++) {
        if (!fk[i].used) {
            fk[i].used = 1;
            strcpy(fk[i].path, p);
            fk[i].ino = ino;
            return &fk[i];
        }
    }
    return 0;
}

static FkEnt *fk_add(const char *p, int isdir, const char *data)
{
    return fk_link(p, fk_new_ino(isdir, data));
}

/* 名前を消す。他に名前が無ければ inode も解放 (ext2_unlink は links_count が
 * 0 になったときだけ inode を返す) */
static void fk_unlink(FkEnt *e)
{
    int i, ino = e->ino;
    e->used = 0;
    for (i = 0; i < FK_MAX; i++) if (fk[i].used && fk[i].ino == ino) return;
    fki[ino].used = 0;
}

/* 既定の /var /var/log と、指定の中身のファイル */
static void fk_setup(int fat, const char *log, const char *old, const char *tmp)
{
    fk_reset(fat);
    fk_add("/var", 1, 0); fk_add("/var/log", 1, 0);
    if (log) fk_add(P_LOG, 0, log);
    if (old) fk_add(fat ? P_OLD8 : P_OLD, 0, old);
    if (tmp) fk_add(P_NEW, 0, tmp);
}

static int fk_logop(const char *op, const char *p)
{
    strcat(fk_log, op);
    if (p) { strcat(fk_log, ":"); strcat(fk_log, p); }
    strcat(fk_log, ";");
    if (!fk_fail_op || strcmp(fk_fail_op, op) != 0) return 0;
    return !fk_fail_path || (p && strcmp(fk_fail_path, p) == 0);
}

static int fk_mkdir(const char *p)
{
    if (fk_logop("mkdir", p)) return fk_fail_rc;
    if (fk_find(p)) return OS32_ERR_EXIST;
    fk_add(p, 1, 0);
    return 0;
}

static int fk_rm(const char *p)
{
    FkEnt *n;
    if (fk_logop("rm", p)) return fk_fail_rc;
    n = fk_find(p);
    if (!n) return OS32_ERR_NOTFOUND;
    fk_unlink(n);
    return 0;
}

static int fk_rename(const char *a, const char *b)
{
    FkEnt *n, *d;
    fk_rename_n++;
    if (fk_logop("rename", a)) return fk_fail_rc;
    n = fk_find(a);
    if (!n) return OS32_ERR_NOTFOUND;
    d = fk_find(b);
    if (fk_fat) {
        if (d) return OS32_ERR_EXIST;         /* FatFs f_rename は置き換えない */
        strcpy(n->path, b);
        return 0;
    }
    /* ext2_rename (fs/ext2_dir.c): 元と宛先が同じ inode (ファイルの
     * ハードリンク同士) なら何もせず成功し、両名が残る (POSIX どおり)。
     * 別の inode の宛先があれば置き換え (旧 inode はリンク数が減るだけ)、
     * 無ければ新名を載せてから旧名を消す。fk_rename_after_link の回は新名を
     * 載せた後で落ち、2 つの名前が同じ inode を指したまま残る。 */
    if (d && d->ino == n->ino) return 0;
    if (d) fk_unlink(d);
    fk_link(b, n->ino);
    if (fk_rename_after_link == fk_rename_n) return FK_ERR_IO;
    n->used = 0;
    return 0;
}

/* 実物と同じ約束: 名前があればその inode を切り詰めて書く (ext2 の上書き /
 * FA_CREATE_ALWAYS)、無ければ新しい inode。失敗は切り詰めた後に起きる —
 * 「切り詰めた後の失敗」で中身は空になる。戻りは ext2 が 0、FAT が書いた
 * バイト数 (fk_short なら 1 少ない = 満杯)。 */
static int fk_write(const char *p, const void *data, u32 size)
{
    FkEnt *e;
    FkIno *n;
    int fail = fk_logop("write", p);
    e = fk_find(p);
    if (!e) e = fk_add(p, 0, 0);
    n = &fki[e->ino];
    n->data[0] = '\0';
    n->len = 0;
    if (fail) return fk_fail_rc;
    if (size >= sizeof(n->data)) size = sizeof(n->data) - 1;
    if (fk_fat && fk_short && size > 0) size--;
    memcpy(n->data, data, size);
    n->data[size] = '\0';
    n->len = size;
    return fk_fat ? (int)size : 0;
}

static int fk_sync(void)
{
    if (fk_logop("sync", 0)) return fk_fail_rc;
    return 0;
}

static const BootlogFsOps fk_ops = { fk_mkdir, fk_rm, fk_rename, fk_write, fk_sync };

static int fk_has(const char *p, const char *content)
{
    FkEnt *e = fk_find(p);
    return e && strcmp(fki[e->ino].data, content) == 0;
}

static int fk_absent(const char *p) { return fk_find(p) == 0; }

static int fk_same_ino(const char *a, const char *b)
{
    FkEnt *x = fk_find(a), *y = fk_find(b);
    return x && y && x->ino == y->ino;
}

static void case_save(void)
{
    int st, rc;

    /* a. 何も無い ext2 */
    fk_reset(0);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && rc == 0, "8a 初回は全段通る (EXIST / NOTFOUND は成功)");
    CHECK(strcmp(fk_log, "mkdir:/var;mkdir:/var/log;rm:" P_NEW ";write:" P_NEW ";"
                         "rename:" P_LOG ";rename:" P_NEW ";sync;") == 0,
          "8b 順序: mkdir → mkdir → rm boot.new → write boot.new → boot.log→.1 → boot.new→boot.log → sync");
    CHECK(fk_has(P_LOG, "NEW") && fk_absent(P_NEW), "8c boot.log に今回の中身、boot.new は残らない");

    /* b. 前々回と前回がある ext2 */
    fk_setup(0, "PREV", "OLDER", 0);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK, "8d 2 回目以降も通る");
    CHECK(fk_has(P_LOG, "NEW") && fk_has(P_OLD, "PREV") && fk_absent(P_NEW),
          "8e 前回分は .1 へ、前々回は消える");
    CHECK(strstr(fk_log, "rm:" P_OLD) == 0, "8e-2 ext2 の rename は宛先を置き換えるので .1 は消さない");

    /* c. FAT: rename は宛先があると断るので、先に消していないと回らない */
    fk_setup(1, "PREV", "OLDER", 0);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_FAT, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_OLD8, "PREV") && fk_has(P_LOG, "NEW") &&
          fk_absent(P_NEW), "8f FAT は bootlog.1 へ回して書く (write はバイト数で成功)");
    CHECK(strstr(fk_log, "rename:" P_LOG ";rm:" P_OLD8 ";rename:" P_LOG ";") != 0,
          "8f-2 FAT は rename が EXIST で断るので .1 を消してもう一度");

    /* d. /var が作れない → 書かずに止める */
    fk_reset(0);
    fk_fail_op = "mkdir"; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_MKDIR_VAR && rc == FK_ERR_IO && strstr(fk_log, "write") == 0,
          "8g /var が作れなければ MKDIR_VAR で止め、書かない");

    /* e. boot.new が書けない (切り詰めた後の失敗) → 既存には触らない */
    fk_setup(0, "PREV", "OLDER", 0);
    fk_fail_op = "write"; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_WRITE && rc == FK_ERR_IO, "8h write の失敗は WRITE");
    CHECK(fk_has(P_LOG, "PREV") && fk_has(P_OLD, "OLDER"),
          "8i 切り詰めた後に落ちても boot.log と .1 は無傷 (一時ファイルにしか書いていない)");
    CHECK(strstr(fk_log, "rename") == 0 && strstr(fk_log, "sync") == 0 &&
          strstr(fk_log, "rm:" P_OLD) == 0, "8j 書けなければ世代を動かさず sync もしない");
    CHECK(strstr(fk_log, "write:" P_NEW ";rm:" P_NEW ";") != 0 && fk_absent(P_NEW),
          "8k 途中で切れた boot.new は消す (成否は問わない)");

    /* e-2. 残っていた boot.new を消せない → 書かずに止める (既存 inode に書かない) */
    fk_setup(0, "PREV", "OLDER", "STALE");
    fk_fail_op = "rm"; fk_fail_path = P_NEW; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_RM_NEW && rc == FK_ERR_IO && strstr(fk_log, "write") == 0,
          "8k-2 残っていた boot.new を消せなければ RM_NEW で止め、書かない");
    CHECK(fk_has(P_LOG, "PREV") && fk_has(P_OLD, "OLDER") && fk_has(P_NEW, "STALE"),
          "8k-3 何も動かない");

    /* f. FAT で前回分を消せない → 止める。boot.log は残り、今回分は boot.new */
    fk_setup(1, "PREV", "OLDER", 0);
    fk_fail_op = "rm"; fk_fail_path = P_OLD8; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_FAT, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_RM_OLD && rc == FK_ERR_IO, "8l rm .1 の失敗は RM_OLD");
    CHECK(strstr(fk_log, "rename:" P_NEW) == 0 && strstr(fk_log, "sync") == 0,
          "8m rm が落ちたら公開しない");
    CHECK(fk_has(P_LOG, "PREV") && fk_has(P_OLD8, "OLDER") && fk_has(P_NEW, "NEW"),
          "8n boot.log を上書きせず、今回分は boot.new に残す");

    /* f-2. FAT で 2 度目の rename が落ちる */
    fk_setup(1, "PREV", "OLDER", 0);
    fk_fail_op = "rename"; fk_fail_path = P_LOG; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_FAT, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_ROTATE && rc == FK_ERR_IO && fk_has(P_LOG, "PREV") && fk_has(P_NEW, "NEW"),
          "8n-2 FAT の付け替えの失敗も ROTATE、boot.log は残る");

    /* g. 付け替え (boot.log → .1) が落ちる → boot.log を残す */
    fk_setup(0, "PREV", "OLDER", 0);
    fk_fail_op = "rename"; fk_fail_path = P_LOG; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_ROTATE && rc == FK_ERR_IO, "8o boot.log → .1 の失敗は ROTATE");
    CHECK(fk_has(P_LOG, "PREV") && fk_has(P_NEW, "NEW") && fk_has(P_OLD, "OLDER") &&
          strstr(fk_log, "rename:" P_NEW) == 0 && strstr(fk_log, "sync") == 0,
          "8p boot.log も .1 も残り、今回分は boot.new、公開も sync もしない");

    /* h. 公開 (boot.new → boot.log) が落ちる → 前回分は .1、今回分は boot.new */
    fk_setup(0, "PREV", "OLDER", 0);
    fk_fail_op = "rename"; fk_fail_path = P_NEW; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_PUBLISH && rc == FK_ERR_IO, "8q boot.new → boot.log の失敗は PUBLISH");
    CHECK(fk_has(P_OLD, "PREV") && fk_has(P_NEW, "NEW") && fk_absent(P_LOG) &&
          strstr(fk_log, "sync") == 0, "8r どのログも失わない (前回分は .1、今回分は boot.new)");

    /* h-2. boot.log が無い (前回の公開が落ちた) → .1 には触らず今回分を公開 */
    fk_setup(0, 0, "ONLY", "LEFT");
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "NEW") && fk_has(P_OLD, "ONLY") && fk_absent(P_NEW),
          "8r-2 boot.log が無ければ唯一の旧世代 .1 を残して今回分を公開する");
    CHECK(strstr(fk_log, "rm:" P_OLD) == 0, "8r-3 そのとき .1 は消さない");
    fk_setup(1, 0, "ONLY", 0);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_FAT, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "NEW") && fk_has(P_OLD8, "ONLY") &&
          strstr(fk_log, "rm:" P_OLD8) == 0, "8r-4 FAT でも同じ");

    /* i. FAT で書いた量が足りない (満杯) */
    fk_setup(1, "PREV", 0, 0);
    fk_short = 1;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_FAT, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_WRITE && rc == 2, "8s FAT で書いた量が足りなければ WRITE (rc は書けた量)");
    CHECK(fk_has(P_LOG, "PREV") && fk_absent(P_NEW), "8t boot.log は無傷、欠けた boot.new は消す");

    /* j. sync が落ちる */
    fk_reset(0);
    fk_fail_op = "sync"; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_SYNC && rc == FK_ERR_IO && fk_has(P_LOG, "NEW"),
          "8u sync の失敗は SYNC (ファイルは公開済み)");

    /* k. 残っていた boot.new (前回の起動が世代の更新の途中で止まった) */
    fk_setup(0, "PREV", 0, "STALE");
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "NEW") && fk_has(P_OLD, "PREV") &&
          fk_absent(P_NEW), "8v 残っていた boot.new は先に消され、公開後に残らない");
    CHECK(strstr(fk_log, "rm:" P_NEW ";write:" P_NEW ";") != 0, "8v-2 消すのは書く前");
    fk_setup(1, "PREV", 0, "STALE");
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_FAT, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "NEW") && fk_absent(P_NEW),
          "8w FAT でも同じ (書き込みは CREATE_ALWAYS で上書き)");

    /* l. 書かない種別 */
    fk_reset(0);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_SKIP, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_log[0] == '\0', "8x 書かない種別は何も呼ばない");

    /* m. 段の名前 */
    CHECK(strcmp(bootlog_stage_name(BOOTLOG_ST_ROTATE), "rotate") == 0 &&
          strcmp(bootlog_stage_name(BOOTLOG_ST_PUBLISH), "publish") == 0 &&
          strcmp(bootlog_stage_name(BOOTLOG_ST_RM_NEW), "rm /var/log/boot.new") == 0 &&
          strcmp(bootlog_stage_name(BOOTLOG_ST_WRITE), "write /var/log/boot.new") == 0 &&
          strcmp(bootlog_stage_name(BOOTLOG_ST_MKDIR_LOG), "mkdir /var/log") == 0 &&
          strcmp(bootlog_stage_name(99), "?") == 0, "8y 段の名前");
    CHECK(is_83_path(SYS_BOOTLOG_NEW), "8z boot.new は 8.3 (FAT でも作れる)");
}

/* ------------------------------------------------------------------------ */
/*  9. write の戻りの約束 (FS ごと)                                          */
/* ------------------------------------------------------------------------ */
static void case_write_rc(void)
{
    CHECK(bootlog_write_ok(BOOTLOG_FS_EXT2, 0, 100), "9a ext2 は 0 が成功 (fs/ext2_vfs.c)");
    CHECK(!bootlog_write_ok(BOOTLOG_FS_EXT2, -7, 100), "9b ext2 の負は失敗");
    CHECK(bootlog_write_ok(BOOTLOG_FS_FAT, 100, 100), "9c FAT は書いたバイト数 = len が成功");
    CHECK(!bootlog_write_ok(BOOTLOG_FS_FAT, 99, 100), "9d FAT の不足は失敗 (満杯)");
    CHECK(!bootlog_write_ok(BOOTLOG_FS_FAT, 0, 100), "9e FAT の 0 は「1 バイトも書けなかった」で失敗");
    CHECK(!bootlog_write_ok(BOOTLOG_FS_FAT, -7, 100), "9f FAT の負は失敗 (f_close の失敗を含む)");
    CHECK(!bootlog_write_ok(BOOTLOG_FS_SKIP, 0, 100), "9g 書かない種別に成功は無い");
}

/* ------------------------------------------------------------------------ */
/*  10. 再起動をまたぐ (同じ偽 FS に何度も保存する)                           */
/* ------------------------------------------------------------------------ */
static void case_reboots(void)
{
    int st, rc;

    /* 起動 1: 初回 */
    fk_reset(0);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B1", 2, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "B1") && fk_absent(P_OLD), "10a 起動 1: boot.log だけ");

    /* 起動 2: 書き込みが落ちる (媒体の障害) → 唯一の旧ログ B1 が残る */
    fk_log[0] = '\0';
    fk_fail_op = "write"; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B2", 2, &rc);
    CHECK(st == BOOTLOG_ST_WRITE && fk_has(P_LOG, "B1") && fk_absent(P_OLD) && fk_absent(P_NEW),
          "10b 起動 2 (write 失敗): 唯一の旧ログ B1 はそのまま");

    /* 起動 3: 付け替え (boot.log → .1) が落ちる → B1 は残り、B3 は boot.new */
    fk_log[0] = '\0';
    fk_fail_op = "rename"; fk_fail_path = P_LOG; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B3", 2, &rc);
    CHECK(st == BOOTLOG_ST_ROTATE && fk_has(P_LOG, "B1") && fk_has(P_NEW, "B3") && fk_absent(P_OLD),
          "10c 起動 3 (付け替え失敗): B1 は残り、B3 は boot.new");

    /* 起動 4: 公開が落ちる → B1 は .1 へ、B4 は boot.new (B3 は上書きされた) */
    fk_log[0] = '\0';
    fk_fail_op = "rename"; fk_fail_path = P_NEW; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B4", 2, &rc);
    CHECK(st == BOOTLOG_ST_PUBLISH && fk_has(P_OLD, "B1") && fk_has(P_NEW, "B4") && fk_absent(P_LOG),
          "10d 起動 4 (公開失敗): B1 は .1、B4 は boot.new、boot.log は無い");

    /* 起動 5: .1 だけが残っている状態から正常に保存 → B5 が boot.log、
     * boot.log が無かったので唯一の旧世代 B1 (.1) はそのまま */
    fk_log[0] = '\0';
    fk_fail_op = 0; fk_fail_path = 0;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B5", 2, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "B5") && fk_has(P_OLD, "B1") && fk_absent(P_NEW),
          "10e 起動 5 (正常): B5 が boot.log、B1 (.1) は残る");
    CHECK(strstr(fk_log, "rm:" P_OLD) == 0, "10f boot.log が無かったので .1 には触らない");

    /* 起動 6: .1 だけの状態で write が落ちる → .1 は残る */
    fk_setup(0, 0, "ONLY", 0);
    fk_fail_op = "write"; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B6", 2, &rc);
    CHECK(st == BOOTLOG_ST_WRITE && fk_has(P_OLD, "ONLY"),
          "10g .1 だけが残っていて write が落ちても、その唯一のログは消えない");

    /* 起動 7: 正常 → 2 世代 */
    fk_fail_op = 0;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B7", 2, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "B7") && fk_has(P_OLD, "ONLY"), "10h 続く正常な起動で boot.log、.1 は残る");
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B8", 2, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "B8") && fk_has(P_OLD, "B7"), "10i さらに 1 回で 2 世代 (ONLY は入れ替わる)");
}

/* ------------------------------------------------------------------------ */
/*  11. ext2 の rename が新名を載せた後で落ちる (2 つの名前が同じ inode)      */
/* ------------------------------------------------------------------------ */
static void case_hardlink(void)
{
    int st, rc;

    /* 起動 A: 公開 (boot.new → boot.log、この手順で 2 回目の rename) が旧名の
     * 削除で落ちる。fs/ext2_dir.c の順序 (links +1 → 新名を載せる → 旧名を
     * 消す) では boot.log と boot.new が同じ inode を指したまま残る */
    fk_setup(0, "PREV", 0, 0);
    fk_rename_after_link = 2;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "A", 1, &rc);
    CHECK(st == BOOTLOG_ST_PUBLISH && rc == FK_ERR_IO, "11a 公開が旧名の削除で落ちる");
    CHECK(fk_same_ino(P_LOG, P_NEW) && fk_has(P_LOG, "A") && fk_has(P_OLD, "PREV"),
          "11b boot.log と boot.new が同じ inode (中身は A)、.1 は PREV");

    /* 起動 B: そのまま保存。boot.new を先に消す (名前だけ消え、inode は
     * boot.log が持つ) → 新しい inode に書く → boot.log (A) を .1 へ → 公開 */
    fk_log[0] = '\0';
    fk_rename_n = 0;
    fk_rename_after_link = 0;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B", 1, &rc);
    CHECK(st == BOOTLOG_ST_OK, "11c 次の起動は通る");
    CHECK(fk_has(P_OLD, "A") && fk_has(P_LOG, "B") && fk_absent(P_NEW),
          "11d 前回の A は .1 に無傷で残り (共有 inode を切り詰めない)、B が boot.log");
    CHECK(strstr(fk_log, "rm:" P_NEW ";write:" P_NEW ";") != 0, "11e boot.new は書く前に消す");
    CHECK(!fk_same_ino(P_LOG, P_OLD), "11f boot.log と .1 は別の inode");

    /* 起動 A': boot.log → .1 (1 回目の rename) が旧名の削除で落ちる →
     * boot.log と .1 が同じ inode。次の起動: boot.log → .1 は同じ inode
     * 同士なので何もせず成功 (両名が残る、fs/ext2_dir.c)。続く公開
     * (boot.new → boot.log) が既存の boot.log を置き換える — 旧 inode は
     * リンク数が 1 減るだけで、.1 の名前で PREV が残る */
    fk_setup(0, "PREV", "OLDER", 0);
    fk_rename_after_link = 1;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "A", 1, &rc);
    CHECK(st == BOOTLOG_ST_ROTATE && fk_same_ino(P_LOG, P_OLD) && fk_has(P_NEW, "A"),
          "11g 付け替えが旧名の削除で落ちると boot.log と .1 が同じ inode、今回分は boot.new");
    fk_log[0] = '\0';
    fk_rename_n = 0;
    fk_rename_after_link = 0;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B", 1, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "B") && fk_has(P_OLD, "PREV") && fk_absent(P_NEW),
          "11h 次の起動で B が boot.log、PREV が .1 (どれも切り詰めていない)");
    CHECK(strstr(fk_log, "rename:" P_LOG ";rename:" P_NEW ";sync;") != 0 &&
          strstr(fk_log, "rm:" P_OLD) == 0 && !fk_same_ino(P_LOG, P_OLD),
          "11h-2 付け替えは no-op の成功、.1 は消さず、公開の置き換えで別の inode になる");

    /* 偽 FS そのものの約束: 同じ inode 同士の rename は何もせず成功 */
    fk_setup(0, "PREV", 0, 0);
    fk_link(P_OLD, fk_find(P_LOG)->ino);
    CHECK(fk_rename(P_LOG, P_OLD) == 0 && fk_same_ino(P_LOG, P_OLD) && fk_has(P_LOG, "PREV"),
          "11i 同じ inode 同士の rename は両名を残して成功 (ext2_dir.c の no-op)");

    /* boot.log と .1 が同じ inode (PREV)、boot.new (A) も残っている状態から、
     * 公開 (boot.new → boot.log) が落ちる */
    fk_setup(0, "PREV", 0, "A");
    fk_link(P_OLD, fk_find(P_LOG)->ino);
    fk_fail_op = "rename"; fk_fail_path = P_NEW; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B", 1, &rc);
    CHECK(st == BOOTLOG_ST_PUBLISH && rc == FK_ERR_IO, "11j 共有 inode の状態で公開が落ちると PUBLISH");
    CHECK(fk_has(P_LOG, "PREV") && fk_has(P_OLD, "PREV") && fk_same_ino(P_LOG, P_OLD) &&
          fk_has(P_NEW, "B") && strstr(fk_log, "sync") == 0,
          "11k 公開が落ちても boot.log と .1 は同じ inode の PREV のまま残り、今回分 B は boot.new");
    CHECK(strstr(fk_log, "rm:" P_OLD) == 0 && strstr(fk_log, "rm:" P_LOG) == 0,
          "11l そのとき boot.log も .1 も消していない");

    /* 続く起動は正常: B は boot.new ごと消え (8v と同じ)、C が boot.log、PREV が .1 */
    fk_log[0] = '\0';
    fk_fail_op = 0; fk_fail_path = 0;
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "C", 1, &rc);
    CHECK(st == BOOTLOG_ST_OK && fk_has(P_LOG, "C") && fk_has(P_OLD, "PREV") && fk_absent(P_NEW) &&
          !fk_same_ino(P_LOG, P_OLD), "11m 次の正常な起動で C が boot.log、PREV が .1 (別の inode)");

    /* 同じ状態から公開まで通る */
    fk_setup(0, "PREV", 0, "A");
    fk_link(P_OLD, fk_find(P_LOG)->ino);
    st = bootlog_save_with(&fk_ops, BOOTLOG_FS_EXT2, "B", 1, &rc);
    CHECK(st == BOOTLOG_ST_OK && rc == 0 && fk_has(P_LOG, "B") && fk_has(P_OLD, "PREV") &&
          fk_absent(P_NEW) && !fk_same_ino(P_LOG, P_OLD),
          "11n 共有 inode の状態から公開が通れば B が boot.log、PREV が .1 (別の inode)");
}

/* /var/log だけが作れない (1 本目の mkdir は通し、2 本目で落とす) */
static int g_mk_n;
static int fk_mkdir_2nd_fails(const char *p)
{
    g_mk_n++;
    if (g_mk_n == 2) { fk_logop("mkdir", p); return FK_ERR_IO; }
    return fk_mkdir(p);
}

static void case_save_mkdir_log(void)
{
    BootlogFsOps ops = fk_ops;
    int st, rc;
    ops.mkdir = fk_mkdir_2nd_fails;
    fk_reset(0);
    g_mk_n = 0;
    st = bootlog_save_with(&ops, BOOTLOG_FS_EXT2, "NEW", 3, &rc);
    CHECK(st == BOOTLOG_ST_MKDIR_LOG && rc == FK_ERR_IO && strstr(fk_log, "write") == 0,
          "8s' /var/log が作れなければ MKDIR_LOG で止め、書かない");
    fk_reset(0);
    fk_add("/var", 1, 0); fk_add("/var/log", 1, 0);
    g_mk_n = 0;
    ops.mkdir = fk_mkdir;
    st = bootlog_save_with(&ops, BOOTLOG_FS_EXT2, "NEW", 3, 0);
    CHECK(st == BOOTLOG_ST_OK, "8t' fail_rc が NULL でも落ちない");
    fk_setup(0, "PREV", 0, 0);
    fk_fail_op = "rename"; fk_fail_path = P_NEW; fk_fail_rc = FK_ERR_IO;
    st = bootlog_save_with(&ops, BOOTLOG_FS_EXT2, "NEW", 3, 0);
    CHECK(st == BOOTLOG_ST_PUBLISH, "8u' 途中の段で止まるときも fail_rc が NULL で落ちない");
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(void); } cases[] = {
        { "collect",      case_collect },
        { "overflow",     case_overflow },
        { "utf8_latch",   case_utf8_latch },
        { "stop",         case_stop },
        { "header",       case_header },
        { "compose",      case_compose },
        { "plan",         case_plan },
        { "lock",         case_lock },
        { "save",         case_save },
        { "save_mkdir",   case_save_mkdir_log },
        { "write_rc",     case_write_rc },
        { "reboots",      case_reboots },
        { "hardlink",     case_hardlink },
    };
    unsigned i;
    int ran = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (argc < 2 || strcmp(argv[1], cases[i].name) == 0) {
            printf("[%s]\n", cases[i].name);
            cases[i].fn();
            ran++;
        }
    }
    if (ran == 0) { printf("unknown case\n"); return 2; }
    return g_fail ? 1 : 0;
}
