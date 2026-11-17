#ifndef OS32_KERNEL_START_H
#define OS32_KERNEL_START_H
/* カーネルの入口の骨格。機種に依らない 1 本 (init/kernel_start.c)。
 *
 * arch の入口 (x86: kentry.asm → platform/pc98 の kernel_main、PowerPC:
 * platform/powermac/kernel_boot.c) が、ページングと例外の受け口を張った後に 1 回だけ呼ぶ。
 * 割り込みは禁止のまま入る。戻らない。
 *
 * 起動の順は 3 つの局面で、順序と割り込みの有効化はここが持つ。局面の中身は
 * platform が実装する (platform/<機種>/)。ローダからの受け渡しは platform が
 * 自分の入口で受けて持つので、ここには引数が無い。 */
void kernel_start(void) __attribute__((noreturn));

/* 局面 1 (割り込み禁止): 機器の束縛・CPU・メモリ・時計・割り込みコントローラ。
 * 戻った時点で、割り込みを有効にしてよい状態になっている。 */
void platform_boot_machine(void);
/* 局面 2 (割り込み有効): 割り込みが要る立ち上げ (設定・ファイル・LAN・表示)。 */
void platform_boot_services(void);
/* 局面 3: 常駐シェルの実行。通常は戻らない。戻ったら kernel_start が停止する。 */
void platform_boot_run(void);

#endif
