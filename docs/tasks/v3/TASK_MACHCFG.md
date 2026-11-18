# TASK_MACHCFG — 機種の固有の設定を独立した設定ファイル (/etc/machine.cfg) で切り替える

> 状態: **計画 (2026-11-18)** — 設計は確定 ([MACHCFG_DESIGN](../../decisions/settings/MACHCFG_DESIGN.md))。段 G-a・G1・B・B1・C・M・M1 は完了、ほかは未着手。本線の CI の手直し (票 TASK_CI_REWORK) の後に出す。
> 完了の基準: 両機種で同一入力の設定試験が全件一致し、7450 の既定・明示 0・明示 1 の HID0 読戻しが順に 0・0・1。7455 の既定は 1。NP21/W で設定あり・なしともシェル到達。common-compile 成功、check_layers.py の新旧違反 0、起動像の既存サイズ検査合格。

## 0. ユーザー決定

- 2026-11-17 (PowerPC の PM 経由): 「機種の固有の設定はコンフィグファイルで設定出来るように」「既存ファイルというより独立させた設定ファイルのほうが良い。既に作ってあるものはリファクタリング計画での改修項目として記録」。
- 2026-11-18: G4 (埋め込みの RAM root) で設定を切り替える手段は「当面は ELF の再生成でよい」。/host から後で読む形は今回の外。

## 1. 詳細設計 (確定) の要約

全文は [MACHCFG_DESIGN](../../decisions/settings/MACHCFG_DESIGN.md)。

- 形: 既定は機種ごとのビルド時の表 (PowerMac は PVR → platform 既定、PC-98 は platform 既定の 1 行)。root が読めた後に `/etc/machine.cfg` (パスの正典は `include/config.h` の `SYS_MACHINE_CFG`) で上書きして 1 回だけ確定する。ファイルが無い・壊れた・FS の無い像では既定で確定し、起動は止めない。
- 読みは既存の `init/sysconfig.c` を使う (新しいパーサは作らない、PM 決定)。sysconfig.c から gf_production の 2 関数を `init/gf_production_cfg.c` に出して、sysconfig.c を共通 (`C_COMMON_EXEC`) に移す。machine.cfg の厳しい検査は一時読取の間だけ効き、system.cfg の解釈は変えない。
- 最初の利用者: PowerPC 7450 の NAP (既定 0。早くに下ろし、確定後に 1 の時だけ立てる。`tools/ppc/cpu_nap_host.c` の期待を対で直す)。性能の段の要求値 `perf.profile` (暫定の既定 0 = 互換、PM 決定。自動の扱いは票 PERF_PROFILE_PLAN の判断)。
- 契約: 内部契約 AC-BOOT-3 を第一候補 ([ARCH_COMPAT_CONTRACT](../../decisions/ARCH_COMPAT_CONTRACT.md))。公開 KAPI と hwdev の ABI は変えない。

## 2. 段

設計の §6 の表 (A〜N1) が 1 ファイル 1 段。受入と変異は設計の §7。

| 段 | 状態 |
|---|---|
| G-a・G1 | **完了 (2026-11-18、e196afb70、PowerPC の PM)**。7450 の NAP を設定に依らず下ろす。G4 実機で、設計の追記 P-R4 の順の 1 手目 (NAP=0) で固まりが無くなった (記録は票 TASK_PPC_REALHW §2) — 2 手目 (DPM と POW も外す) は要らない |
| B・B1・C・M・M1 | **済 (2026-11-19、terra、astra のレビューの直し 1 回)**。契約 `include/machcfg_contract.h`・`include/config.h` の `SYS_MACHINE_CFG`・`kernel/machcfg.c`・host 試験 `machcfg-host`。kernel/machcfg.c はまだビルドの一覧に入れていない (段 J)。設計の欠け 2 つは設計の末尾の「段 B・C の実装で埋めた設計の欠け」 |
| D (起動時の診断の kprintf を含む、PM 決定: 段 D を出す時に設計者に追記させる)・E・F・PVR の優先と読み超過の変異 (D・F の受入) | 未着手 |
| A・B2〜B4・G-b・H・I・J〜L・N・N1 | 未着手 |

## 3. 改修項目の記録

移さない既存の設定 (system.cfg の GFX・GUI・BOOTSERIAL・E82557・KDEBUG、SFS_LAN の system.cfg と netcd.cfg の二重、sync.cfg、netcd.cfg) は、票 PERF_PROFILE_PLAN の §6 に改修項目として記録した。
