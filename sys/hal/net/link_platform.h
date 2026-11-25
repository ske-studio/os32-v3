/* LINK_PLATFORM.H — リンク層の機械依存境界 (実体は各 platform が持つ)。 */
#ifndef OS32_NET_LINK_PLATFORM_H
#define OS32_NET_LINK_PLATFORM_H

#include "types.h"

/* 論理 100Hz の jiffy (10ms) を返す。周回をまたぐ比較は差で行う。
 * 物理の時計が速い機種は tick_clock_config で変換した tick_count を返す。
 * x86 は既存 link.c と同じ tick_count を使う。 */
u32 link_platform_now(void);

/* IRQ 状態を保存して排他に入る。区間内では待たない。
 * x86 は irq_save() と同じ動作。入れ子では各 saved を対応して戻す。 */
u32 link_platform_lock(void);

/* 対応する lock の saved を復元する (入れ子は逆順)。
 * x86 は irq_restore(saved) と同じ動作。 */
void link_platform_unlock(u32 saved);

/* 前景専用、再入禁止。IRQ 内や送信内から呼ばない。
 * 共通 NIC の poll・link 進行は tick と idle_work_run の FG1 の地点
 * (待ち・syscall 入口・net_tx の後)。link 進行は1msに1回まで、
 * RX 直後 NET_RX_ACTIVE_MS 未満の poll は毎周。
 * IF/EE=0 の起動時は従来の _idle() で割り込みを許可する。
 * idle_wait_step() は IF=1 の前景で work 後、RX が静かなら _idle()。 */
void link_platform_idle(void);

/* Register the resident NIC jiffy callback in the shared link tick slot.
 * Return the tick registry result; failed registration may be retried. */
int link_platform_tick_register(void (*fn)(void));

#endif /* OS32_NET_LINK_PLATFORM_H */
