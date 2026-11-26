# userland/shell/hdprep_plan.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 1 (§1-v3 N4〜N6) の契約を今のソースと照らして要約
`userland/shell/cmd_hdprep.c` が確認と整形を担当する。サイズとマウントの判定は `userland/shell/hdprep_plan.c`、PC-98 の幾何・空ディスク検査・計画・区画表生成は `sys/platform/pc98/install_plan.c`、セクタの書込みと照合は `sys/platform/pc98/install_hdprep.c` にある。

hd0 (DA 80h) に OS32 の置き場 (区画 + ext2) を作るときの「断る条件」と「計画」の純粋関数。シェルの `hdprep` と
インストーラ (`userland/system/inst_hdd.c`) が同じものを使う。I/O は呼び手 (KAPI) が行う。

## 断る条件 (`HDPREP_E_*`)

| 関数 | 条件 | 戻り値 |
|---|---|---|
| `hdprep_check_geom` | IDENTIFY に答えない | `E_NO_ATA` |
| | IDENTIFY の総数が 0 | `E_NO_TOTAL` |
| | BIOS 幾何なし (問い合わせていない / 規則外 / CX・DH・DL が 0 / ヘッドかセクタが 255 超) | `E_NO_BIOS` |
| | BIOS のセクタ長 (BX) ≠ 512 | `E_SECLEN` |
| | ATA の方式が LBA28 でも現在の CHS でもない (既定の CHS では書かない) | `E_ADDR` |
| `hdprep_check_disk` | LBA 1 に空でない区画項目が 1 つでもある (どの添字でも、OS32 自身でも) | `E_PT_USED` |
| | LBA 0 の末尾が 55AA | `E_MBR_SIG` |
| `hdprep_check_mounts` | hd0 がルート | `E_ROOT` |
| | hd0 がどこかにマウント中 | `NEED_UMOUNT` (1、外してから進む) |
| `hdprep_parse_mb` | 大きさが 8〜256 (MiB) の外・数字以外 | `E_SIZE` (空なら既定 256) |

hdprep は**空のディスク専用**で、`--force` は無い。

## 計画 (`hdprep_plan(g, mb, out)`)

- 幾何は BIOS 幾何 (区画表と IPL の CHS)。cyl = heads × spt。
- 開始 = LBA 1632 (`HDPREP_BOOT_RESERVE_LBA`) 以上の最初のシリンダ境界 (8/17 なら 1632、16/63 なら 2016)。
- 上限 = BIOS の CX × cyl、IDENTIFY の総数、ATA の方式で指せる範囲 (LBA28 なら 2^28、現在の CHS ならその容量) の最小
  (`sys/dev/ata/ide_addr.c` と同じ規則)。上限 ≤ 開始なら `E_TOO_SMALL`。
- 長さ = min(mb MiB, 上限 - 開始) をシリンダへ切り下げ。8MiB 未満になれば `E_TOO_SMALL`。終了シリンダが 16 ビットを
  超えれば `E_CYL`。
- `probe_lba` = 予定域の最終セクタ。計画した区画は `pc98pt_make_os32` と固定点 (`ext2_layout_plan`) をそのまま通る。

## `hdprep` の順序 (`cmd_hdprep.c`)

幾何 → ディスクが空か (LBA 0/1 の生の中身を見せる) → マウント → 計画と表示 → `yes` の打鍵 (rshell の `/api/cmd` からは
答えられない) → アンマウント (失敗・外しても残るなら断る) → 探り (`probe_lba` へ書いて読み戻す) → `ext2_format_at` →
区画表を**最後に**書いて読み戻し比較 → 通常のマウントで確認。書いた後の失敗は「未完了」と出して止まる。

## 確かめ

`make check-hdd-stage1-host` (`test_hdd_stage1.py (旧版 6201b26bccf0)`) のケース `hdprep_geom`・`hdprep_disk`・`hdprep_mounts`・`hdprep_plan`。
`make check-hdprep-command-host MUTATE=1` は登録された実コマンドをメモリ媒体に対して実行する。空ディスク検査と確認拒否で全セクタが等しいこと、試し書きの照合失敗で整形前に止まること、成功時の試し書き・整形・表更新・マウント検読の順を確認する。
