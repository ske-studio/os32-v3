/* ======================================================================== */
/*  SERIAL_WATCHDOG.H — 速度を上げたあと会話が続いているかを見る番犬        */
/*                                                                          */
/*  速度を上げたあと **ホストと足並みが揃ったかはゲストには分からない**。    */
/*  013Ah が効かない機種、ケーブルが速度に耐えない、ホストが開き直しに       */
/*  失敗した — どれでも「こちらは N、ホストは別の速度」になり、戻すための    */
/*  `serial 9600` すら届かない。だから **確認が取れなければゲストが自力で    */
/*  元へ戻す**。                                                            */
/*                                                                          */
/*  ⚠ **合図は明示的にする** (Codex レビュー往復 4 で設計変更)。             */
/*  往復 1〜3 では「受信した」「1 行往復した」を証拠に使おうとして、その     */
/*  たびに穴が出た — 切替行自身を数える / 断片を数える / EOT の送信失敗を    */
/*  数える / ローカルキーで解除される / 本文が落ちて EOT だけ通る……。       */
/*  **暗黙の推定は尽きない**ので、推定をやめる:                             */
/*                                                                          */
/*    arm   : rshell 経由の `serial N` が速度を変えた瞬間 (EOT の前後も      */
/*            成否も問わない)                                               */
/*    解除  : **新速度で `serial ack` という行を受けて実行したときだけ**      */
/*    戻す  : 期限 (SER_SWITCH_WATCHDOG_TICKS) まで ack が来なかったとき、   */
/*            または **arm 中に rshell を抜けるとき**                        */
/*                                                                          */
/*  `serial ack` はホストが投げる専用の合図で、ゲストは `ACK <baud> <mode>`  */
/*  を 1 行返す。**arm されていなくても同じ応答** (冪等) — ホストは何回でも  */
/*  投げてよく、どこかの回で読めればそれが確認になる。                       */
/*                                                                          */
/*  ローカル CUI で打った `serial N` は arm しない。**戻す相手が居ない**     */
/*  ので、勝手に速度が戻ると手元の操作の方が驚く。                           */
/*                                                                          */
/*  ここは**依存を 1 つも持たない**。シェル (userland/shell/rshell.c) から   */
/*  使い、ホスト試験 (tools/tests/serial_vfast_host.c) は .c をそのまま      */
/*  #include して回す。                                                     */
/*    試験: tools/tests/test_serial_vfast.py (ケース watchdog)              */
/*    記録: tools/tests/serial_vfast_tdd.md                                 */
/*    票  : docs/archive/realhw_v21/TASK_SERIAL_VFAST.md                          */
/* ======================================================================== */

#ifndef __SERIAL_WATCHDOG_H
#define __SERIAL_WATCHDOG_H

/* 期限 [tick]。PIT は 100Hz なので 500 tick = 5 秒。
 * ホスト道具は切替後 **1.5 秒以内**に新速度で `serial ack` を投げはじめ、
 * 0.5 秒ごとに 4 秒まで繰り返す (tools/rshell_serial.py)。つまり ack の
 * 機会は期限内に 8 回ほどある。**短すぎるとホストが開き直す前に戻り**、
 * 長すぎると失敗したまま待たされる。 */
#define SER_SWITCH_WATCHDOG_TICKS 500

#define SER_WD_WAIT   0   /* まだ期限内 — 待つ */
#define SER_WD_LINKED 1   /* ack が来た — 解除 */
#define SER_WD_REVERT 2   /* 期限切れで ack なし — 元の設定へ戻す */

struct serial_watchdog {
    int armed;                    /* 1 = ack 待ち */
    int acked;                    /* 1 = `serial ack` を受けて実行した */
    unsigned long start_tick;     /* 切り替えた tick */
    unsigned long prev_mode;      /* 戻し先のモード */
    unsigned long prev_baud;      /* 戻し先の速度 */
};

/* 切替直後に仕掛ける。acked は 0 に戻る。 */
void serial_watchdog_arm(struct serial_watchdog *w, unsigned long tick,
                         unsigned long prev_mode, unsigned long prev_baud);

/* **`serial ack` の行を受けて実行した**ときに呼ぶ。ここだけが解除の材料。
 * 仕掛かっていなければ何もしない (`serial ack` 自体は冪等に応答する)。 */
void serial_watchdog_ack(struct serial_watchdog *w);

/* いま戻すべきか。SER_WD_WAIT / _LINKED / _REVERT。
 * LINKED か REVERT を返したら番犬は下ろされる (2 度は返らない)。 */
int serial_watchdog_poll(struct serial_watchdog *w, unsigned long tick);

/* rshell を抜けるときに呼ぶ。**arm 中で未確認なら即座に REVERT**
 * (Codex レビュー往復 4 B4: 抜けたあとは poll する者が居ないので、
 * 番犬が仕掛かったまま忘れられて会話が死ぬ)。
 * 戻り SER_WD_REVERT = 戻すこと / SER_WD_WAIT = 何もしなくてよい。 */
int serial_watchdog_leave(struct serial_watchdog *w);

/* 判定そのもの (状態を持たない)。**ack が先**: 期限を過ぎていても ack を
 * 受けていたら LINKED (遅れて届いた ack を「無音」と読み替えない)。 */
int serial_watchdog_decide(unsigned long elapsed_ticks, int acked);

/* ======================================================================== */
/*  rshell の ESC (票 TASK_SERIAL_HOSTFS §1-v3 / 決裁 1B)                   */
/*                                                                          */
/*  シリアルから来た ESC で rshell を閉じるのは**行の先頭の単独の ESC**      */
/*  (後ろに続くバイトが RSH_ESC_ALONE_TICKS のあいだ来ない) のときだけ。     */
/*  SerialFS のフレームの中の 0x1B や、遅れて届いた応答の断片で閉じない。    */
/*  それ以外の位置の ESC を含む行は**実行せずに断る** (EOT は返す)。         */
/*  本体キーボードの ESC は従来どおりどこでも閉じる。                       */
/* ======================================================================== */
#define RSH_ESC_ALONE_TICKS 3     /* 30ms: 115200 でも 9600 でも続きは 2ms 以内 */
#define RSH_ESC_NONE  0           /* ESC ではない */
#define RSH_ESC_EXIT  1           /* rshell を閉じる */
#define RSH_ESC_JUNK  2           /* この行は実行しない */
int rsh_esc_classify(int ch, int from_serial, int at_line_start, int followed);

/* ------------------------------------------------------------------------ */
/*  rshell の 1 行の組み立て (レビュー往復 1、Codex 8)                        */
/*                                                                          */
/*  ESC を含んで拒否した行は、**本当の行末 (\n / \r) まで**拒否のまま読み捨て */
/*  る。受信に間が空いても (沈黙が何秒続いても) 解かない — 解くと残りが次の  */
/*  行として実行される (レビュー往復 2、Codex 3)。行末はホストの次の行の    */
/*  改行でも、本体キーボードの Enter でも閉じる (その行は拒否のまま、EOT を  */
/*  1 つ返す)。本体の ESC は rshell を閉じる (回復の口)。                    */
/*  拒否していない行は従来どおり、短い空回りで行が終わる。                   */
/* ------------------------------------------------------------------------ */
#define RSH_LINE_MORE 0           /* 続きを待つ */
#define RSH_LINE_DONE 1           /* 行が終わった */
#define RSH_LINE_EXIT 2           /* rshell を閉じる */

struct rsh_line {
    char *buf;
    int  cap;
    int  pos;
    int  overflow;     /* 上限を越えた (実行しない) */
    int  junk;         /* ESC を含んだ (実行しない) */
    int  local;        /* 本体キーボードのバイトを含んだ */
    int  bytes;        /* 受け取ったバイト数 (EOT を返すかの判定) */
};
void rsh_line_begin(struct rsh_line *l, char *buf, int cap);
/* 1 バイト入れる。at_start = 行の 1 文字目、followed = 行頭のシリアルの ESC の
 * 後ろに続きが来たか (それ以外は 0)。戻りは RSH_LINE_*。buf は常に NUL 終端。 */
int  rsh_line_feed(struct rsh_line *l, int ch, int from_serial, int at_start,
                   int followed);
/* バイトが来なかった (短い空回りの後)。戻りは RSH_LINE_DONE (普通の行は
 * 終わり) / RSH_LINE_MORE (拒否した行は行末まで待ち続ける)。 */
int  rsh_line_idle(const struct rsh_line *l);

/* ------------------------------------------------------------------------ */
/*  行の残りを読むループ (Codex レビュー P2、rshell.c から移した)             */
/*                                                                          */
/*  拒否した行の行末を待つ間も**番犬を見る**。以前は行頭の待ちだけが番犬を   */
/*  呼んでいたので、速度が合わずに「ESC + 続き (改行なし)」に化けた行を掴む */
/*  と、期限を過ぎても旧速度へ戻らず、旧速度のホストの改行も `serial ack`  */
/*  も読めないまま止まった。                                                */
/*                                                                          */
/*  見る判定は行頭と同じ serial_watchdog_poll。期限で REVERT が出たら、その */
/*  行は**捨てる** (速度が変わったのでバイト列に意味が無い) — 実行もせず、 */
/*  EOT も返さない (呼び手が戻した速度で返す EOT が合図になる)。拒否の規則 */
/*  (行末まで実行しない・沈黙では解けない・本体の Enter / ESC で回復) は    */
/*  変えない。番犬が仕掛かっていなければ従来と同じ動きになる。              */
/* ------------------------------------------------------------------------ */
#define RSH_LINE_REVERT 3         /* 番犬の期限切れ — 行を捨てた。速度を戻すこと */
#define RSH_REST_SPIN   50000     /* 空回りでバイトを待つ回数 (行の区切りの目安) */

struct rsh_io {
    int  (*getch)(void *ctx, int *from_serial);  /* 無ければ -1 */
    unsigned long (*tick)(void *ctx);            /* 番犬の時計 */
    void (*idle)(void *ctx);                     /* 1 tick ほど休む */
    void *ctx;
};

/* 行頭の 1 文字 (と ESC の続き) を rsh_line_feed に入れたあとから呼ぶ。
 * r = その戻り、ch / fs = まだ入れていない先読みのバイト (無ければ ch = -1)。
 * w = 番犬 (NULL 可)。戻りは RSH_LINE_DONE / _EXIT / _REVERT。 */
int  rsh_line_rest(struct rsh_line *l, int r, int ch, int fs,
                   struct serial_watchdog *w, const struct rsh_io *io);

/* `sfs run <コマンド行>` の行から子のコマンド行を取り出す。行全体が
 * 空白* "sfs" 空白+ "run" 空白+ <1 文字以上> の形でなければ NULL。
 * (`a && sfs run b` のような入れ子は受けない — 決裁 3A「ホストから送った
 * 1 行だけ」) */
const char *rsh_sfs_child(const char *line);

#endif /* __SERIAL_WATCHDOG_H */
