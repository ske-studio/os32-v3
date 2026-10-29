# 実機 (PC-9821Ra266) の票の索引

> 索引: 実機 (Ra266) の票を並べる親 (子の票は `> 上位:` でここを指す)

> 状態: **実装中 (2026-10-22)** — 実機の票の入口。機体の構成・基準構成と実機の方針 (82557・PCI・Trident・1.44MB・しないこと) は決定の文書 [RA266.md](../../decisions/realhw/RA266.md) (2026-10-22 にこの票の本文を節の番号のまま移した)。段の状態は各票の状態行。

実機は**エミュレータが嘘をついている箇所**を暴くために使う (位置づけは RA266 §0)。HDD 起動のままの更新はスキル `os32-realhw-update`。

| 票 | 中身 |
|---|---|
| [TASK_LAN_82557.md](TASK_LAN_82557.md) | 内蔵 Intel 82557 の LAN (PCI の最初の利用者、RA266 §5・§6) |
| [TASK_TRIDENT_DRIVER.md](TASK_TRIDENT_DRIVER.md) | 内蔵 Trident の画面ドライバ (RA266 §7) |
| `docs/archive/realhw/TASK_PEGC480_REALHW.md` | 実機の PEGC 640x480 のずれ — 受入完了 (2026-09-30)、アーカイブ。今の仕様は [05_drivers.md §5-5](../../05_drivers.md) |
| [TASK_FD144.md](TASK_FD144.md) | 1.44MB フロッピー (RA266 §3) |
| [TASK_GEFORCE6200.md](TASK_GEFORCE6200.md) | 増設の GeForce 6200 (0:13.0) |
| [TASK_GF_STAGES.md](TASK_GF_STAGES.md) | GeForce の実機の試験を段ごとの小さなバイナリ + 共通部分に分ける — 計画 (2026-10-29 の反省会) |
| [TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) | 実機試験を決まった状態へ戻す処理 (realhw_recover) |
| [TASK_HWDEV.md](TASK_HWDEV.md) | 実機の調査・試行・復帰の核と装置ごとの部品 (HWDEV) |
| [TASK_CCU2_MOUSE.md](TASK_CCU2_MOUSE.md) | 2nd CCU (2 本目のシリアル) でシリアルマウスを使う — 計画、余暇でやる (資料集めは済み、手元のマウスは PS/2 専用の見込み) |
| `docs/archive/realhw_v3/TASK_FDRECOV.md` | FD 起動から LAN で HDD 一式を復旧する (FDRECOV-1) |

残件 (票の無いもの): ディスクの健全性と 8GB 全域の読み書きの検証 (RA266 §2)。
