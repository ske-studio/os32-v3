#ifndef V86_CONTRACT_H
#define V86_CONTRACT_H
/* KAPI の表 (kapi/kapi_generated.c) が指す V86 の口。実体は V86 の機能 (FEATURE_V86) を持つ機種が出す。 */
#include "types.h"
#include "os32_kapi_shared.h"   /* V86Gcap */

/* Phase 1 スモークテスト: 最小の 16bit コードを V86 で実行し、
 * カーネルが生存したまま戻れることを確認する。戻り値は v86_exit_reason。 */
int v86_smoke_test(void);

/* ディスクイメージからゲストをブートする。戻り値は v86_exit_reason。 */
int v86_boot(const char *path);

/* 2 イメージ版。path がブートドライブ (slot 0)、second は 2 台目 (slot 1、NULL 可)。 */
int v86_boot2(const char *path, const char *second);

/* ディスクテスト: 0=一致、負=失敗。 */
int v86_disk_test(const char *path);

/* KAPI v68 v86_gdc_capture の本体。mode = V86G_MODE_ROM / V86G_MODE_SELFTEST。
 * 記録を out へ写し、status (V86G_ST_*、>= 0) を返す。始められなければ負
 * (OS32_ERR_INVAL / OS32_ERR_BUSY / OS32_ERR_NOSPC、out は触らない)。 */
int v86_gdc_capture(int mode, V86Gcap *out);

#endif
