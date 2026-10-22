# 実機 (PC-9821Ra266) の票の索引

> 索引: 実機 (Ra266) の票を並べる親 (子の票は `> 上位:` でここを指す)

> 状態: **実装中 (2026-10-22)** — 実機の票の入口。機体の構成・基準構成と実機の方針 (82557・PCI・Trident・1.44MB・しないこと) は決定の文書 [RA266.md](../../decisions/realhw/RA266.md) (2026-10-22 にこの票の本文を節の番号のまま移した)。段の状態は各票の状態行。

実機は**エミュレータが嘘をついている箇所**を暴くために使う (位置づけは RA266 §0)。HDD 起動のままの更新はスキル `os32-realhw-update`。

| 票 | 中身 |
|---|---|
| [TASK_LAN_82557.md](TASK_LAN_82557.md) | 内蔵 Intel 82557 の LAN (PCI の最初の利用者、RA266 §5・§6) |
| [TASK_TRIDENT_DRIVER.md](TASK_TRIDENT_DRIVER.md) | 内蔵 Trident の画面ドライバ (RA266 §7) |
| [TASK_PEGC480_REALHW.md](TASK_PEGC480_REALHW.md) | 実機の PEGC 640x480 のずれ |
| [TASK_FD144.md](TASK_FD144.md) | 1.44MB フロッピー (RA266 §3) |
| [TASK_GEFORCE6200.md](TASK_GEFORCE6200.md) | 増設の GeForce 6200 (0:13.0) |
| [TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) | 実機試験を決まった状態へ戻す処理 (realhw_recover) |
| [TASK_HWDEV.md](TASK_HWDEV.md) | 実機の調査・試行・復帰の核と装置ごとの部品 (HWDEV) |
| [TASK_FDRECOV.md](TASK_FDRECOV.md) | FD 起動から LAN で HDD 一式を復旧する (FDRECOV-1) |

残件 (票の無いもの): ディスクの健全性と 8GB 全域の読み書きの検証 (RA266 §2)。
