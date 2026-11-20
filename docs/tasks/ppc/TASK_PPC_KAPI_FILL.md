# TASK_PPC_KAPI_FILL — PowerMac の KAPI の「提供者無し」190 本を埋める

> 2026-11-20 の見直しで x86 の形に改めた ([DESIGN_X86_AUDIT](../../decisions/ppc/DESIGN_X86_AUDIT.md))。

> 状態: **実装中 (2026-11-20)** — [段の計画](../../decisions/ppc/PPC_KAPI_FILL_DESIGN.md) に従い、共通の提供者で KAPI の機能を揃える。既存の二重実装はその後に整理する。
> 完了の基準: sdk/kapi_target_ppc.py の PROVIDER_GROUPS に残るのが計画の §2 で E (PowerMac に無くて正しい) とした slot だけになり、各段で「両像のリンク map と nm -l で slot → wrap の呼ぶ先 → 定義元のソースのパスが同じ」「arch/ppc・platform/powermac に x86 と同じ名前の新しい定義が 0」を確かめ、tools/ppc/test_kapi_binding.py の本数がその数に一致する。

## 0. ユーザー決定 (2026-11-18)
- 「KAPI の提供者無し 190 本を埋める計画を、先に作ってよいか。先に。ただし、x86 と重複するような実装はだめです」。
- 音: 「そもそも FM 音源がない機種は HAL のソフトウェア FM 音源の部分でつなぐ計画では？」→ PowerMac は「新しい口だけ」(旧 FM 6 口は E、sndl_chip_* → AV HAL のソフトウェア FM 音源 → PCM)。

- 2026-11-20: PowerPC の手が本線の段の待ちで空いた時は、x86 に寄せる作業 (票 TASK_PPC_ALIGN_X86) を重さの軽い順に取る (ユーザー決定 2026-11-20)。

## 1. 分担
- 本線: 共通へ移す段 (C)・口を作る段 (D)・select.mk の縛りを解く段。移す先のファイル名は本線が段の着手で決めて、ここに書き戻す。
- PowerPC: PowerMac の後端 (B) と PowerPC の公開表 (sdk/kapi_target_ppc.py)。

## 2. 段の状態
| 段 | 担当 | 状態 |
|---|---|---|
| 6a 再起動 (sys_reboot) | PowerPC | 装置手順・受入台本は実装済み (2d2736837)。製品シェルの reboot と再起動後の READY は PPC_RESET_DESIGN に従う |
| 1d・3a・4a | PowerPC | 実装済み (b1437a4a3・720abc8ce・290c0e1eb) |
| 1a・2a-i・2b・3b-i | 本線 | 未着手 (CI の手直しと CRT の後始末の後) |
| 5b | 共通表示・PowerPC | A〜C 実装済み (e407702a0・caa544473・0227413b7) |
| 5c・5d | 共通 HAL・シェル | 5c の B1〜B4 と 5c-A は実装済み。5d は PPC_SHELL_ALIGN_X86_DESIGN で owner 1 の起動・回収を揃え、通常のシェルで切替を受け入れる。|
| 6b | SFS | 実装済み (2964a2003・2f4ed7d73)。既存の SHELL_REMOTE_SFS はシェル統合で整理する。|
| 6c | filesystem・ATA | N/P/D1/D2 実装済み (D2 は 40a3e273c)。D3 は段 8 の ATA block provider の後、製品シェルで HDD の ext2 作成を確認する。|
| 7a・7b | module・VFS | 7a P1〜P4b 実装済み (1c9ed35c5)。静的結線の残段は取り消す。L1 → L2 → FEP → KCG。辞書・学習は HDD の後に x86 と同じパスで、SQLite は共通 module・VFS と段 S が揃ってから公開する。|
