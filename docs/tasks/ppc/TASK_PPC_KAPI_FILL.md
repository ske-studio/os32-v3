# TASK_PPC_KAPI_FILL — PowerMac の KAPI の「提供者無し」190 本を埋める

> 状態: **実装中 (2026-11-18)** — 計画は確定 ([PPC_KAPI_FILL_DESIGN](../../decisions/ppc/PPC_KAPI_FILL_DESIGN.md))。本線の差し込みは CI の手直しの S0 と CRT の後始末の後 (1a・2a-i・2b・3b-i から)。PowerPC が本線を待たずに進める段は 1d (shm)・3a (ESCC)・4a (セル 5 本)・6a (再起動、wt/ppc-reset で実装中)。
> 完了の基準: sdk/kapi_target_ppc.py の PROVIDER_GROUPS に残るのが計画の §2 で E (PowerMac に無くて正しい) とした slot だけになり、各段で「両像のリンク map と nm -l で slot → wrap の呼ぶ先 → 定義元のソースのパスが同じ」「arch/ppc・platform/powermac に x86 と同じ名前の新しい定義が 0」を確かめ、tools/ppc/test_kapi_binding.py の本数がその数に一致する。

## 0. ユーザー決定 (2026-11-18)
- 「KAPI の提供者無し 190 本を埋める計画を、先に作ってよいか。先に。ただし、x86 と重複するような実装はだめです」。
- 音: 「そもそも FM 音源がない機種は HAL のソフトウェア FM 音源の部分でつなぐ計画では？」→ PowerMac は「新しい口だけ」(旧 FM 6 口は E、sndl_chip_* → AV HAL のソフトウェア FM 音源 → PCM)。

- 2026-11-20: PowerPC の手が本線の段の待ちで空いた時は、x86 に寄せる作業 [TASK_PPC_ALIGN_X86](TASK_PPC_ALIGN_X86.md) を重さの軽い順に取る (ユーザー決定 2026-11-20)。

## 1. 分担
- 本線: 共通へ移す段 (C)・口を作る段 (D)・select.mk の縛りを解く段。移す先のファイル名は本線が段の着手で決めて、ここに書き戻す。
- PowerPC: PowerMac の後端 (B) と PowerPC の公開表 (sdk/kapi_target_ppc.py)。

## 2. 段の状態
| 段 | 担当 | 状態 |
|---|---|---|
| 6a 再起動 (sys_reboot) | PowerPC | 実装中 (wt/ppc-reset、確定設計 PPC_RESET_DESIGN) |
| 1d・3a・4a | PowerPC | 未着手 (本線を待たない) |
| 1a・2a-i・2b・3b-i | 本線 | 未着手 (CI の手直しと CRT の後始末の後) |
| ほか | 計画の §3 の順 | 未着手 |
