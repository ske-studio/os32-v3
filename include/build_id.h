/* ======================================================================== */
/*  BUILD_ID.H — ビルドした git のコミット ID                                */
/*                                                                          */
/*  実体は**生成物** $(BUILD_OUT)/build_id.c (tools/gen_build_id.py)。       */
/*  `git rev-parse --short HEAD`、作業ツリーに追跡中の変更があれば "-dirty"、 */
/*  git が無ければ "unknown"。生成器は中身が変わったときだけ書き直すので、    */
/*  コミット ID が変わっても作り直すのは build_id.o 1 つとリンクだけ。        */
/*  票: docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-4 (ユーザー指示)       */
/* ======================================================================== */

#ifndef BUILD_ID_H
#define BUILD_ID_H

/* NUL 終端、最長 BUILD_COMMIT_MAX - 1 文字 (生成器が保証する) */
#define BUILD_COMMIT_MAX   24
extern const char os32_build_commit[];

#endif /* BUILD_ID_H */
