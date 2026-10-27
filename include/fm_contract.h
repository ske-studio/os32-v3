/* Shared FM contract; device registers remain in drivers/. */
#ifndef FM_CONTRACT_H
#define FM_CONTRACT_H
#include "types.h"
/* ======== 型定義 ======== */
typedef unsigned char uchar;

/* FM and SSG each have channels 0 through 2 on YM2203. */
#define OPN_CHANNEL_COUNT 3
#define FM_SEMITONES_PER_OCTAVE 12

/* ======== ノート番号ヘルパー ======== */
#define NOTE(oct,key)  ((uchar)((oct)*FM_SEMITONES_PER_OCTAVE+(key)))
#define N_C  0
#define N_CS 1
#define N_D  2
#define N_DS 3
#define N_E  4
#define N_F  5
#define N_FS 6
#define N_G  7
#define N_GS 8
#define N_A  9
#define N_AS 10
#define N_B  11

/* ======== 公開API ======== */

/* OPN低レベル */
void opn_init(void);
void opn_write(uchar reg, uchar val);

/* FM音源 */
void fm_set_tone(int ch, const uchar *tone_data);
void fm_set_tone_num(int ch, int tone_num);
void fm_note_on(int ch, int note);
void fm_note_off(int ch);
void fm_all_off(void);
int fm_quiet(void); /* confirmed silence for boot rollback */

/* SSG音源 */
void ssg_tone(int ch, u16 period);
void ssg_volume(int ch, uchar vol);
void ssg_noise(uchar period);
void ssg_mixer(uchar mask);
void ssg_all_off(void);

/* 高レベル */
void fm_startup_sound(void);       /* 起動ジングル */
void fm_play_mml(const char *mml); /* 簡易MML再生 */

void fm_play_mml_kapi(const char *mml);


#endif
