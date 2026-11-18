# TASK_BLOCKDEV_PERF — ブロックデバイス (IDE・FD・CD・ext2/FAT) の遅い所を直す

> 状態: **受入完了 (2026-11-04)** — 全段が決着した (L1・L2・D1・D2・D4・D5a・N1・L3・D6 は main、D3 は改善しない・D5b はやらない、各段は下の表)。実機 Ra266 の ee4f3f42 で `cp` 1.5MB (HDD → /tmp) 約 5 倍 (7.07〜8.45 → 1.48〜1.50 s)、CD 10MB → /tmp 約 4 倍 (40.61 → 10.18 s)、diff は一致 (2026-11-01、証拠はノートの `~/os32-tmp/evidence/2026-11-01/realhw-ee4f3f42/`)。
> 上位: [ROADMAP](../../ROADMAP.md)。調べの原文: `~/os32-tmp/run/blockdev-kernel/REPORT.md` (資料の略号・file:line・見込みの計算)。

## ユーザーの指示と決定 (2026-10-31)

- 「IDE と FD、CD などのブロックデバイス周りのボトルネック」を、OS32 のカーネルについて、リファレンスのハードウェアの資料と照らし合わせて調べる (astra)。
- 「上がった 12 は何にせよ手を入れなければならないので実装計画を立てる」「まず軽いものを設計し terra か sol で実装」。
- D3 (FD のまとめ読み・書きの結線) は「改善しなくて良い」(見送りではなく閉じる)。
- 資料 (`docs/hw/undocumented/io_ide.md` 42–44) の「PC-9800 の IDE インターフェースではデータ転送に DMA を使用しない」は**仕様**として扱う (ユーザー)。DMA は PCI の別の IDE (RAID) カードを使うときまで実装しない — RAID を使う予定は無い。

## 調べの結論

待ち時間の定数を一律に減らす根拠はない (FD の DMA・まとめ読み・トラックのキャッシュ、CD の 32KB の先読みは既にある)。遅いのはソフトの読み方: ext2 が同じ表・同じブロックを読み直す、IDE が複数セクタを 1 セクタずつのコマンドに分ける、FD の Device API の経路にまとめ読みが結ばれていない。

## 段

| 段 | 項目 (調べの番号) | 中身 | 大きさ | 担当 | 状態 |
|---|---|---|---|---|---|
| L1 | #3 (小) | ext2 のブロック (1KB) の読み取りを 512B×2 の要求から count=2 の 1 要求に (`fs/ext2/ext2_super.c`、読み取りだけ) | 小 | terra | 済み (main e2fe4686) |
| L2 | #9 (小)・#10・#11 | ATAPI (`drivers/ata/atapi.c`): PIO のループの偶数長の高速路 (奇数長・読み捨ては今のまま、rep は使わない)・BSY/DRQ の待ちを最初は 10µs ごと→長い待ちで 100µs・SRST の保持を校正済みの 10µs 待ちに (解除後 2ms と BSY 待ちは残す) | 小 | sol | 済み (main e2fe4686) |
| D1 | #1・#5 | ext2 の間接表・inode・直近データの保持 | 設計 | astra 起草 | 済み (main ba09ac7c) |
| D2 | #2・#3 (中) | IDE の既存 READ/WRITE の Sector Count を使った複数セクタ、ext2 の連続ブロックを束ねる | 設計 | astra 起草 | 済み (main e2fe4686) |
| D3 | #4・#7 | FD の Device API の経路にまとめ読み (fdc_read_sectors) を結ぶ、書き込みの同一トラックのまとめ | 設計 | astra 起草 | **改善しない (ユーザー決定 2026-10-31)** — Device API で FD を count>1 で読み書きする呼び手が無く、FatFs の FD は読みのまとめ・トラックのキャッシュが既にあり、書き込みは 1 クラスタ=1 セクタで束ねる相手が無い (Opus レビュー) |
| D4 | #6 | FAT の読み取りハンドル・位置の保持 (毎回の開き直しと先頭からの lseek をやめる) | 設計 | astra 起草 | 済み (main e2fe4686) |
| D5 | #8 (D5a 済み、main e2fe4686)・#9 の rep (D5b、手本・置き場が決まってから) | CD のディレクトリのまとめ読み、rep insw/outsw の適合の確認 | 設計 | astra 起草 | D5a 済み (main e2fe4686)。**D5b はやらない** (2026-11-01、sol の資料確認: PC9800Bible/2-9-1_IDE_PIO.md §5.3:259 が rep insw を避けるよう指示、§7.3:344–348 がタイミングの問題と手書きのループの中での調整を記載。報告 `~/os32-tmp/run/rest/d5b.md`) |
| N1 | #12 | SET MULTIPLE MODE + READ/WRITE MULTIPLE (新機能、ユーザー「良い機能」2026-10-31 で実装する) — D2 の後。IDENTIFY word 47 で対応を確かめ、対応しないドライブは今の読み方 | 中〜大 | 設計は D2 の後 | 済み (D6 段 3、main 4599cfd7、sol 実装・astra 取り込み可) |
| L3 | 調べの主因 (書き込み) | ext2 の 1KB の書き込みを count=2 の 1 要求に (`ext2_raw_write_block`) | 小 | terra | 済み (main ce72c861、D1 の試験の目印の直し 6564c069) |
| D6 | 調べの主因 (書き込みのメタデータ) | 段 1: ビットマップ・inode・間接表の保持とまとめ更新 / 段 2: データの連続区間の範囲書き / 段 3: N1 | 設計 | astra 起草、確定版 | 段 1 済み (main 0b0d46c6、sol 実装・Opus 取り込み可、取り込みの直し 11535a5f)、段 2 済み (main 36d74183、sol 実装・astra 取り込み可、64KiB の新規連続で書きの要求 69→7)、段 3 済み。取り込みの直し 48754cbf |

## 軽い段の受入

- 2026-11-01: **Ra266 を ee4f3f42 (D1・L3・N1・D6 の全部) に更新** (ノートのセッション、証拠 ノートの `~/os32-tmp/evidence/2026-11-01/realhw-ee4f3f42/`): HDD 起動・Image CRC 8968e651・部品 pair 一致・leftover=0・audit_fail=0・selftest 1702/0・db_test 9/9。速さ (e2fe4686 の後 → ee4f3f42): `cp` 1.5MB → /tmp 7.07〜8.45 → **1.48〜1.50 s (約 5 倍)**、CD 10MB → /tmp 40.61 → **10.18 s (約 4 倍)**、SQLITE_INTERNAL_READY の tick 1751 → 1707。diff は両方とも一致、消して再起動も正常。selftest の pass が 1738 → 1702 (NP21/W も 1748 → 1712) に減ったのは、D1・D6 の保持でカーネルの BSS が 12 ページ増え、`slack_cycle` の空きのページが 388 → 376 (1 ページ 3 件 × 12 = 36) になったため — 試験の抜けではない (`~/os32-tmp/run/selftest-cmp/`)。
- 2026-11-01: D6 段 2 の main 36d74183 を NP21/W で起動、guest_tests 21/21、cp・上書き・rm の後の再起動で中身一致、`e2fsck -fn` は起動前・書き込みと guest_tests の後とも clean (`~/os32-tmp/run/d6s2-guest/`)。次は Ra266 で D6 の前 (e2fe4686 の記録) と比べる速さの測り直し。
- 2026-11-01: D6 段 1 の main 0b0d46c6 を NP21/W で起動、guest_tests 21/21、cp・上書き・rm・diff の一致。`e2fsck -fn` は起動だけ・書き込みの後・guest_tests の後のどれも clean (`~/os32-tmp/run/d6s1-fsck/`)。**測り方の罠**: `nhd_deploy.py deploy-nhd`・`pull` の後は手元の NHD がマウントされたまま残る — その像を e2fsck すると書き出し前の集計差が出る。e2fsck は NP21/W を止めて Windows 側の NHD を別の場所に写してからかける。
- 2026-11-01: L3・N1 の入った main 4599cfd7 を NP21/W で起動、guest_tests 21/21 PASS (`~/os32-tmp/run/n1-guest/`)。NP21/W の IDE が MULTIPLE の経路を通ったかは起動のログに出ず未確認。実機の速さは D6 の後にまとめて測る。

- ホスト: 既存の試験 (cd_read・ide・ext2 の像の試験) が変わらず通る。L1 は 1KB の読み取りの要求が 1 回になること、L2 は待ちの間隔と SRST の保持が規定の値であることを試験で見る。
- ゲスト (NP21/W): 起動 (ext2 の root)・`ls /cd0` とファイルの読み取り・guest_tests。実機 (Ra266) は次の実機の回で起動と CD の読み取り。
