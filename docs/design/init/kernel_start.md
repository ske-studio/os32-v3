# init/kernel_start.c — カーネルの入口の骨格

> 状態: **現行 (2026-11-17)** — 機種依存の分離 (入口の骨格を 1 本に)。
ソース: `init/kernel_start.c`・`init/kernel_start.h`

起動の順を持つ所は、機種に依らないこの 1 本だけ。局面の中身は platform が実装する。

```c
void kernel_start(void)            /* 割り込み禁止で入る。戻らない */
{
    platform_boot_machine();       /* 局面 1: 割り込み禁止のまま、機器・CPU・メモリ・時計・割り込みコントローラ */
    _enable();                     /* arch_io.h。割り込みを有効にするのはここだけ */
    platform_boot_services();      /* 局面 2: 割り込みが要る立ち上げ */
    platform_boot_run();           /* 局面 3: 常駐シェル。通常は戻らない */
    for (;;) _halt();
}
```

| 機種 | arch の入口 (ここを呼ぶ所) | 局面の実装 |
|---|---|---|
| x86-pc98 | `arch/x86/kernel/kentry.asm` → `kernel_main(mem_kb, boot_drive)` (`platform/pc98/kernel.c`、ローダの 2 つの引数を覚えて `kernel_start()` を呼ぶだけ) | `platform/pc98/kernel.c` |
| ppc-powermac | `platform/powermac/kernel_boot.c` の `ppc_kernel_real_start()` (ページングと例外の受け口の後) | `platform/powermac/ppc_kernel.c` |

## 契約

- `kernel_start` は引数を持たない。ローダ・ファームウェアからの受け渡しは、platform が自分の入口で受けて持つ。
- 局面 1 から戻った時点で、割り込みを有効にしてよい状態 (割り込みコントローラと時計の用意が済み) になっている。局面 1 の中で割り込みを有効にしない。
- 局面 2 と 3 は割り込みが有効な状態で呼ばれる。
- 局面をまたいで要る状態は platform のファイルの中の static に置く (x86 の `boot_context`、PowerPC の `lan_log_rc`・`root_ready`)。
- `init/` には像の構成 (`KBOOT_*`) や機種の `#if` を置かない。像ごとの違いは platform の局面の中に閉じる。

## 局面の切れ目

| 局面 | x86-pc98 | ppc-powermac |
|---|---|---|
| 1 | スタックの印・ブート情報の写し・メモリの検出・CPU と例外・入力/FD/PCI/ディスク・ヒープ・root のマウント・部品の読み込み・時計・`exec_init`・IME・音源・`boot_token_init` | ESCC のログ・GMAC の静止・共通の mm (`memory_boot_init`・`shm_init`・ヒープ)・トラップ/ガード/flush の実証・時計・decrementer・MPIC |
| 2 | `services_boot`・スプラッシュ・表示の準備と部品・共有ライブラリ・起動のログの保存 | MPIC の配送と時計の実証・`boot_token_init`・LAN・root・FB のコンソール・HostFS・USB・入力 |
| 3 | `kernel_shell_loop` | `pm_exec_boot` と常駐シェル (`pm_shell_boot` / `pm_cli_boot`)。シェルの無い像は LAN の poll で居続ける |

## 確かめ

x86 は NP21/W で起動して `ver`・`services: SERVICES_READY`・起動のログの `fail=0` を確かめた (2026-11-17、`70409b7` に骨格を足した木)。PowerPC は QEMU の検査 (`ppc-*`)。
