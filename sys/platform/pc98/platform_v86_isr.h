#ifndef OS32_PC98_PLATFORM_V86_ISR_H
#define OS32_PC98_PLATFORM_V86_ISR_H
#ifdef OS32_NASM
#define BOOT_FUNCTION(result, name, args, attrs) extern name
#else
#include "types.h"
#define BOOT_FUNCTION(result, name, args, attrs) result name args attrs;
#endif

/* #GP の V86 経路から呼ばれる一次ハンドラ (sys/platform/pc98/legacy/v86.c)。
 * 0 を返すと V86 へ復帰、非 0 でセッション終了。 */
BOOT_FUNCTION(int, v86_gp_handler, (u32 *frame), )
/* #GP 以外の例外を V86 中に食らったときの記録。isr_stub.asm から呼ぶ。
 * 記録したらセッションは畳む (呼び出し側が v86_exit_to_kernel する)。 */
BOOT_FUNCTION(void, v86_fault_handler, (u32 *frame, u32 vector), )
/* IRQ スタブから呼ばれる割り込み反射。frame は PUSHAD 後のフレーム先頭。
 * ゲストの IVT を引いてスタックに FLAGS/CS/IP を積み、ISR へ飛ばす。 */
/* 実 IRQ をゲストに反射する。第 2 引数は **IRQ 番号** (ベクタではない)。
 * ベクタとマスクの判定は仮想 PIC (v86_pic.h) が行う。 */
BOOT_FUNCTION(void, v86_reflect_irq, (u32 *frame, u32 irq), )
/* タイマ IRQ の反射経路から呼ぶ。上限超過なら非 0。 */
BOOT_FUNCTION(int, v86_tick_and_check_timeout, (void), )
/* キーボード ISR の脱出要求を確認し、非 0 ならセッションを畳む。 */
BOOT_FUNCTION(int, v86_check_exit_request, (void), )
/* 保存したカーネル文脈へ復帰する。IRQ スタブには戻らない。 */
BOOT_FUNCTION(void, v86_exit_to_kernel, (void), )

#undef BOOT_FUNCTION
#endif
