/* Build-selected display provider (第 5 段 B4): which backends a platform has,
 * how the boot preference (GFX=) picks one, and which backend is the last resort.
 * The dirty queue, back buffers, generations and surface leases stay in gfx/.
 * PC-98: platform/pc98/gfx_selector.c。PowerMac: 未対応の provider (描画は後段)。 */
#ifndef OS32_DISPLAY_PROVIDER_H
#define OS32_DISPLAY_PROVIDER_H

#include "types.h"
#include "gfx_hal.h"
/* -Iplatform/$(PLATFORM) で選ぶ。DISPLAY_FALLBACK_BACKEND = probe なしで必ず
 * 使える backend の番地 (リンク時定数。g_backend の静的な初期値にも使う)。 */
#include <display_fallback.h>

/* 共通側の「登録済みの mode が台帳の予約に収まるか」の判定。1 = 選べた。 */
typedef int (*display_select_mode_fn)(GfxBackend *backend, int legacy200);

/* GFX= の値を provider が知る値に丸める。知らない値は GFX_PREF_AUTO。 */
int display_provider_pref(int pref);

/* 希望 (GFX_PREF_*) と probe から backend を 1 つ選び、select_mode で mode を
 * 確かめて返す。ハードウェアの設定はしない。選べなければ受け皿を返す
 * (受け皿の mode も select_mode に通してから返す)。NULL は返さない。 */
GfxBackend *display_provider_select(int pref, int legacy200,
                                    display_select_mode_fn select_mode);

/* backend の台帳上の種別 (LEDGER_SF_*)。知らない backend は 0。 */
u32 display_provider_ledger(const GfxBackend *backend);

#endif
