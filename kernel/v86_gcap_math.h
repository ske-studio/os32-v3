/* ======================================================================== */
/*  V86_GCAP_MATH.H — `v86 -g` の記録器と引数の判定 (ハードに触らない部分)   */
/*                                                                          */
/*  票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1。実機の ROM の       */
/*  INT 18h AH=31h / 30h を V86 で呼び、その間の I/O を記録する。ここには    */
/*  「何を記録するか・どのポートを実機へ通すか・どの命令で打ち切るか・       */
/*  AH=31h の値から AH=30h の引数をどう決めるか」だけを置き、ホスト試験      */
/*  (tools/tests/test_v86_gcap.py) で実物のまま回す。                        */
/*                                                                          */
/*  実ポートへの読み書きは ops (関数ポインタ) 越し。カーネルは inp/inpw/     */
/*  outp/outpw を、自己試験 (V86G_MODE_SELFTEST) は実機に触らない模型を渡す。 */
/* ======================================================================== */

#ifndef __V86_GCAP_MATH_H
#define __V86_GCAP_MATH_H

#include "os32_kapi_shared.h"   /* V86Gcap / V86G_* (KAPI の出力と同じ並び) */

/* 実ポートの読み書き。幅 1 と 2 を別の口にしておくのは、16 ビットの
 * IN/OUT を 8 ビットで通していた既存の中継 (v86_io.c) と同じ誤りを
 * 型の上で起こせないようにするため。 */
typedef struct {
    unsigned int (*in8)(unsigned int port);
    unsigned int (*in16)(unsigned int port);
    void (*out8)(unsigned int port, unsigned int value);
    void (*out16)(unsigned int port, unsigned int value);
} V86gIoOps;

/* AH=31h を呼ぶ前に AL / BH へ入れておく印。どちらの並びでも正しい値に
 * ならない (bit2 並びは AL & FAh == 08h、bit3 並びは AL の D3・D0 以外が 0。
 * D0 = ラスタスキャンはどちらでも許す — 実機 Ra266 は AL=0Dh を返す)。
 * ROM が答えなければ (未対応の機能) 印のまま戻る。 */
#define V86G_SENTINEL_AL     0xFFU
#define V86G_SENTINEL_BH     0xFFU

/* 判定の結果 (v86g_decide の戻り) */
#define V86G_DEC_NO31       -1      /* 印のまま = AH=31h に答えが無い */

/* 記録を空にする (mode は呼び手が入れる) */
void v86g_reset(V86Gcap *g);

/* 採取中に実機へ通すポートか (09A8h / 09A0h / 60h〜6Eh の偶数 /
 * 70h〜7Ah の偶数 / A0h・A2h・A4h・A6h)。 */
int  v86g_port_passed(unsigned int port);

/* #GP で捕まえた命令を採取中に扱えるか。opsize16 = 0 は 66h 前置。
 * 戻り: V86G_ABORT_NONE / V86G_ABORT_INSOUTS / V86G_ABORT_IO32。
 * 66h 付きでも AL の IN/OUT (E4/E6/EC/EE) は 8 ビットのままなので打ち切らない。 */
int  v86g_insn_check(int opsize16, unsigned int opcode);

/* 捕まえた IN を数える (passed = 実ポートから読んだ値か)。 */
void v86g_note_in(V86Gcap *g, unsigned int port, int size, unsigned int value,
                  int passed);

/* 捕まえた OUT を積む。戻り 0 = 積んだ / -1 = 溢れた (overflow = 1、以後は
 * 積まないが seq は進める)。phase は V86G_PH_*。 */
int  v86g_note_out(V86Gcap *g, unsigned int port, int size, unsigned int value,
                   unsigned int cs, unsigned int ip, int passed,
                   unsigned int phase);

/* 実機へ通すポートの IN / OUT。幅で ops の口を選び、記録する。 */
unsigned int v86g_pass_in(V86Gcap *g, const V86gIoOps *ops,
                          unsigned int port, int size);
void v86g_pass_out(V86Gcap *g, const V86gIoOps *ops, unsigned int port,
                   int size, unsigned int value, unsigned int cs,
                   unsigned int ip, unsigned int phase);

/* AH=31h の AL / BH から並びを決め、640x480 (31kHz・30 行) の AH=30h の
 * AL / BH を返す。
 *   戻り: V86G_LAYOUT_BIT2 / V86G_LAYOUT_BIT3 = 決まった /
 *         V86G_LAYOUT_NONE = 決められない / V86G_DEC_NO31 = 印のまま。
 * その並びとして正しい値 (予約 bit が 0、30 行は 480 だけ、480 は 31kHz だけ)
 * になる並びが**ちょうど 1 つ**のときだけ決める。解像度 400 は決め手にしない
 * (NP21/W は 0597h の未設定で 200 LOWER を返す — v86_gcap_math.c)。
 * 両方・どちらでもないときは決めず、**両方の候補を試さない**。 */
int  v86g_decide(unsigned int al, unsigned int bh,
                 unsigned int *al480, unsigned int *bh480);

/* ③ (AH=30h → 480) の終わり方から ④ (AH=30h → 元のモード) を呼ぶか。
 *   set_ran = ③ が最後まで走った (打ち切り・暴走・時間切れでない。溢れは
 *             記録の欠けで ROM の動作とは関係ないので「走った」に数える)
 *   set_ah  = ③ の戻り AH
 * ③ が最後まで走って AH≠05h (ROM が引数を断った = 何も変えていない) なら
 * 呼ばない (0)。④ も断られると OS32 の表で同期を書き換えることになり、
 * 並びの誤判定と重なると 24kHz 機に 31kHz の同期を入れて画面を失う。
 * ③ が 05h を返したか、途中で終わった (何を変えたか分からない) ときは呼ぶ (1)。 */
int  v86g_need_restore(int set_ran, unsigned int set_ah);

/* ④ の終わり方から戻し方 (V86G_RST_ROM / V86G_RST_FALLBACK)。
 * 最後まで走って AH=05h のときだけ ROM で戻ったとみなす。 */
unsigned int v86g_restore_kind(int rst_ran, unsigned int rst_ah);

/* その並びで AL が 31kHz を表すか (ROM で戻れなかったときの OS32 側の戻し用)。 */
int  v86g_mode_is_31k(int layout, unsigned int al);

#endif /* __V86_GCAP_MATH_H */
