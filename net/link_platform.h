/* LINK_PLATFORM.H — リンク層の機械依存境界 (実体は各 platform が持つ)。 */
#ifndef OS32_NET_LINK_PLATFORM_H
#define OS32_NET_LINK_PLATFORM_H

#include "types.h"

/* 共通 tick_count の 10ms tick を返す。周回をまたぐ比較は差で行う。
 * x86 は既存 link.c と同じ tick_count を使う。 */
u32 link_platform_now(void);

/* IRQ 状態を保存して排他に入る。区間内では待たない。
 * x86 は irq_save() と同じ動作。入れ子では各 saved を対応して戻す。 */
u32 link_platform_lock(void);

/* 対応する lock の saved を復元する (入れ子は逆順)。
 * x86 は irq_restore(saved) と同じ動作。 */
void link_platform_unlock(u32 saved);

/* 前景専用、再入禁止。IRQ 内や送信内から呼ばない。
 * x86 は idle_work_run() 後に _idle() (不可分の IRQ 許可・待機)。 */
void link_platform_idle(void);

#endif /* OS32_NET_LINK_PLATFORM_H */
