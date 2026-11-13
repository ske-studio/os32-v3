# PPC_REALHW_G4_DESIGN — PowerMac G4 QuickSilver の実機の初回起動と受入 (RH0〜RH4) の計画と詳細設計

> 状態: **確定 (2026-11-13)** — LAN の TFTP で起動し、右のモニターをカメラで観測、FB の上の帯に段の印を出し、UniNorth の clock gate と実機の PHY を足して LAN のログをノートへ。QEMU の合格を実機の合格に数えない。段の状態は票 TASK_PPC_REALHW が持つ。

> 出所: ~/os32-tmp/run/ppc-rh/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き確定、PM が N1〜N5 を反映)。ユーザー決定 2026-11-13: TFTP 起動・カメラ・ノートと同じスイッチ・BOOTP/DHCP は G4 の MAC だけ・NVRAM は先に聞く。

## r1→r2 の変更
- R1: D7 に RAM root 同梱済みを明記し、二重の blob・mount・build 追加を削除。
- R2: load-base 採取と PT_LOAD 終端・ELF 全長上限の検査を追加。
- R3: 静的 IP 形を未検証候補とし、BOOTP/DHCP の第二経路と U4 を追加。
- R4: FB を上端の帯だけの 256KiB slot に限定し、VA と丸め込みの上限を明記。
- R5: model と /uni-n の reg で clock profile を選び、QEMU の無呼出しを試験化。
- R6: init/ppc_kernel.c・kernel_boot.h の接続と既存 clock VA の再利用を追加。
- R7: 停止後は電源ボタンを 5 秒押して切り、再投入して OF に戻る。
- R8: RH2 初回は FB で PHY ID 採取、適合 profile を加えた 2 回目で受入。
- R9: RH_USER_EXIT の RED 判定を偽 Host の先行試験へ移す。
- R10: devalias と enet の local-mac-address 採取を追加。
- R11: OF の GMAC/DMA 使用と quiesce 前の TX/RX・PCI Command 採取を追加。
- R12: 未知 PHY 拒否の参照を gem_phy.c:95 に訂正。
- 追記 ESCC: init/ppc_kernel.c:153-154 の停止経路も診断構成の対象へ追加。
- PM/U1〜U5: U2 は PM の日程通知、U3 はノートで確定、U1/U4/U5 は §7 に残す。

- 基点: PACK 指定の main cf36ed92。設計のみ。実機の電源・ネットワークには未接続、未試験。
- 決定: TFTP / OF `boot enet:...`、右モニターをノートのカメラで観測、BSD を手本とする。
- 単一タスク・協調 poll・整数演算を維持。QEMU 合格と実機合格は別の証拠欄にする。
- 本文の `RH ...` は追加する印の仕様。既存の印である `TAKEOVER`・`LAN STATE` と区別する。
- 読んだ範囲: PACK 指定文書・関連する実コードの検索行・ローカル BSD 手本。GPL/QEMU ソース・archive は不使用。
- ノートへの依頼は `.claude/skills/os32-realhw-update/SKILL.md` §1 の順序（版、手順、停止、証拠、短い報告）を借りる。
- PC-98 の hsync・NP21/W・ディスク更新手順を G4 へ流用しない。この設計ではノートへの実依頼は送らない。

## 1. 実物の確認

| 台帳 | 実機で残る受入 | 実コードの所在 / 足りないもの |
|---|---|---|
| PPC-BOOT-RH (`DEFERRED_TESTS.md:37`) | Apple OF 4.x の ELF/NOTE、0x20000 非衝突、screen、非ページ境界の map、ch-a open | `platform/powermac/boot.c:36` に OF greeting、同:42 に tree/open/map 失敗。screen の実機確認は未実施 |
| PPC-MMU-RH (:38) | 7450/7455 HIGH_BAT・HID0、DEC pending、cache/WIMG、ESCC 配線 | `platform/powermac/mmu_boot.c:253` は takeover 前の OF 出力。切離し後の実機表示を足す |
| PPC-KBOOT-RH (:39) | 1MiB load/memsz、macio ranges、MPIC LE、TB、cache、HPT、1kHz の実時間 | `kboot_capture.c:127,163,177` は property/TB 採取、`arch/ppc/kernel_boot.c:180,185` は vector/HPT。観測は実機で要る |
| PPC-LAN-RH (:40) | clock、PHY、DMA/cache、PCI domain、AF_PACKET | 下表。QEMU の sungem 合格では閉じない |

| ID | 現物照合で訂正する前提（食い違い 7 件） | 根拠と対応 |
|---|---|---|
| D1 | 「clock gate が無い」ではなく関数は有るが未接続 | `gmac_clock.c:21-28` は native RMW/readback。`kernel_boot.c:193-203` の alias は GMAC/config だけ、`pci_config.c:82-94` は clock 未呼出し |
| D2 | 「OF の画面の印が無い」ではない | `kernel_boot.c:228-232` の `TAKEOVER` は detach の前。これだけでは切離し成功を示さない |
| D3 | 「ID を取っていない」ではなく取った未知 ID を拒否している | `drivers/net/gem/gem_phy.c:88-95` は ID 保存、:26 の表は BCM5201、:95 は未知を拒否。外部へ ID を出す口が不足 |
| D4 | 「切離し後も OF 画面でログが読める」ではない | `kernel_boot.c:231` で detach、`kboot_console.c:63-65` は画面能力なし。FB の段表示が必要 |
| D5 | 「USB PowerMac の殻はこれから全部作る」ではない | `kboot_capture.c:197-198` は sh 像で USB 採取、`usb_boot.c:41-54` に OCR/IR 静止、`usb_platform.c:16` に専用 pool |
| D6 | 「カメラが G4 を見ている」ではない | PACK の現状は左の PC-98。RA266 §3-1 の約 5fps の観測を右へ向けてから開始 |
| D7 | RAM root は既に ELF に同梱済み | `arch/ppc/kernel.ld:20-22`、`build/ppc-kboot.mk:36,90`、`root_image.c:5-8`、`root_boot.c:4`。追加実装不要 |

- 上記の食い違いは設計上の前提の訂正数。実機の故障を検出した数ではない。
- `gmac_tree.c:122` の root_pa は PCI bridge。clock の `0xf8000020` を root_pa+0x20 としてはいけない。
- `gmac_tree.c:178` の DMA 先頭 16MiB 窓、`gmac_dma.c:36-46` の bus 変換は実機の到達性を別途証明する。
- `lan_service.c:198-205` の準備/公開、:207-224 の PHY 待ち/再確認を保ち、poll に長い同期待ちを足さない。
- `ppc_lan_host.py:96-110` に AF_PACKET 実装済み。:405 の `--peer` は必須で G4 の実 MAC が必要。
- ESCC 失敗停止は `kernel_boot.c:212` と `init/ppc_kernel.c:153-154` の両方。外部端子無しと ch-a open 失敗を区別し、診断構成では両経路を扱う。
- `boot enet` では OF が直前まで GMAC/DMA を使用する。直接起動の QEMU 証拠では detach〜early_quiesce の安全性を閉じない。

## 2. RH0〜RH4 の実施計画

### RH0 準備

| 区分 | 操作・入口・合格・失敗時 |
|---|---|
| 入口 | PM が U1/U4/U5 の回答と U2 の日程通知を揃え、U3=ノートで準備。成果物の SHA、ELF SHA256、構成、BSD 根拠、QEMU 結果を添付 |
| ユーザー操作 1 | G4 の内蔵 Ethernet をノートと同じスイッチに挿す (U1 決定) |
| ユーザー操作 2 | PM の通知した日程でカメラを右モニターへ向け、OF の大きな印が画面全体とともに写るよう固定する |
| ユーザー操作 3 | G4 を電源投入し Command-Option-O-F で OF に入り、まだ boot は入力しない |
| ユーザー操作 4 | OF で `dev /`、`.properties`、`dev /openprom`、`.properties`、`printenv load-base`、`devalias`、`dev enet`、`.properties`、`dev /memory`、`.properties` (available — load-base から先の連続して使える域) を順に入力する |
| ノート操作 | 有線 iface/MAC/IP、enet の実パスと local-mac-address（G4_MAC）、load-base、スイッチ port を記録。TFTP 読取 root と証拠ディレクトリを作る |
| ノート操作 | TFTP GET を別クライアントから行い SHA256 一致。UDP 69 と転送中の応答を capture する |
| ノート操作 | root で AF_PACKET の bind/送受信を確認し、`tcpdump -i "$IFACE" -e -w rh0.pcap 'ether proto 0x88b6'` を用意 |
| 観測 | カメラの OF prompt と個体写真、TFTP の取得ログ、iface の carrier、L2 の試験 frame が揃う |
| 失敗時 | carrier 無しはユーザーへ配線確認、TFTP 無応答は iface/IP/サーバ設定をノートで直す。OS 起動へ進まない |

- 推奨アドレス例: 独立した開発用セグメントでノート 192.168.98.1/24、G4 192.168.98.2/24。
- この例を使う前にノートが既存経路との衝突を確認。採用値で下の OF 行を展開し、一行ずつ渡す。
- TFTP は PM 決定でノート（AF_PACKET と同じ L2）。U4 承認時だけ G4 MAC 限定 BOOTP/DHCP で server/next-server と client IP を供給。共用 LAN の既設 DHCP と競合させない。
- ノート依頼の冒頭: 「G4 の TFTP 起動の準備。成果物 SHA=<sha>、RH=<段>、OS ディスクの更新は無し」。
- 依頼の末尾: 「失敗した段で止める。証拠は ~/os32-tmp/evidence/<日付>/ppc-rh/、rc・最終印・pcap・frame 名を返す」。

### RH1 OF 受理から切離し

| 区分 | 操作・入口・合格・失敗時 |
|---|---|
| 入口 | RH0 合格、段印付き boot/mmu/kboot の 3 像を用意。同一像で先行試験済み |
| ユーザー操作 1 | OF で `boot enet:192.168.98.1,os32-boot.elf,192.168.98.2` を入力する |
| ユーザー操作 2 | 結果保存後に電源ボタンを 5 秒押して切る→再投入し Command-Option-O-F で OF へ戻り `boot enet:192.168.98.1,os32-mmu.elf,192.168.98.2` を入力する |
| ユーザー操作 3 | 結果保存後に電源ボタンを 5 秒押して切る→再投入し Command-Option-O-F で OF へ戻り `boot enet:192.168.98.1,os32-kboot.elf,192.168.98.2` を入力する |
| ノート操作 | 各 TFTP 転送とカメラ frame を保存。ELF/NOTE、load 範囲、OF 版を試験票へ対応付ける |
| 観測 | boot は greeting と OPEN/MAP 完了印、mmu/kboot は OF の準備完了印と切離し後の FB 段 1 が見える |
| 観測 | `TAKEOVER` だけは途中到達。FB 段 1 は DR=1・自前写像での書込み成功を示す追加印 |
| 失敗時 | TFTP 完了前は配信、OF の ELF 拒否は header/NOTE、最後の OF 印より後の停止は MMU/alias を順に調べる |

- 各表の静的 IP 形は未検証候補で BSD 手本の裏付けはない。失敗時はエラーを保存し、U4 承認済みなら第二経路 `boot enet:,os32-kboot.elf`（各段でファイル名を置換）を試す。
- BSD 根拠は NetBSD `share/man/man8/man8.macppc/ofwboot.8:384-390` の BOOTP と `boot enet:0`。第二経路のファイル名指定も個体 OF で受理を確認し、通った行を後段で使う。U5 未承認では `setenv` をしない。
- カメラで小さい十六進数の正読は要求しない。段番号は大きく残す。細かい値は RH2 の LAN 採取で閉じる。
- 0x20000 の boot 像と 1MiB の kboot 像を別試験にする。後者の成功で前者の非衝突を済ませない。
- ch-a の open/map が失敗する場合は OF 画面へ理由を出す。ESCC 無し経路は screen/LAN が成立する診断構成として追加する。

### RH2 LAN ログと到達点②

| 区分 | 操作・入口・合格・失敗時 |
|---|---|
| 入口 | RH1 合格。clock の profile/alias、PHY 採取表示を組んだ像。初回は ID 採取が目的、適合 profile を加えた 2 回目で nonce を含む受入 |
| ユーザー操作 1 | OF で `boot enet:192.168.98.1,os32-kboot.elf,192.168.98.2` を入力する |
| ユーザー操作 2 | ノートが連続往復の開始を知らせた後、LAN ケーブルを一度抜いて挿し直す |
| ノート操作 | `sudo python3 tools/ppc_lan_host.py --iface "$IFACE" --peer "$G4_MAC" --out "$EVIDENCE"` を boot 前に起動 |
| ノート操作 | clock/readback→quiesce 前 PCI Command・GEM TX/RX enable 採取→Master 解除確認→PHY→nonce TX/RX→cache の順に実施。STATUS/CLOCK/MEMORY/READ_DIAG/PEEK の全応答を保存 |
| 観測 | FB 段 2=clock、3=PHY 採取、4=link、5=TX、6=RX。追加 `RH CLOCK`・`RH PHY` の数値を LAN で照合 |
| 観測 | 既存 `LAN STATE state=5 link=1`、boot_id/seq/drop、KBOOT 全印と tick 継続。pcap と受信ログの byte 一致 |
| 失敗時 | 段 2 前は clock/PCI、段 3 は ID/MDI、段 4 後は DMA/descriptor、wrap 後だけなら cache 同期へ絞る |

- QuickSilver 内蔵 Gigabit LAN の PHY は BCM54xx の見込み（推測）。初回は LAN OFF を想定し FB の port/address/ID を目視採取、未知なら停止。brgphy の DSP 処置を含む適合 profile を実 ID/revision で選び、2 回目の通信合格で閉じる。
- 既存の PHY 待ちログは LAN 起動前には届かない。FB と RAM の採取記録が必要である。
- CLOCK をホスト monotonic の 10 秒区間で前後取得し、TB/DEC の差と往復時間を保存する（目標誤差 ±5%）。
- 物理 DEC 1kHz と論理 tick 100Hz を別計数で出し、台帳の「1kHz」を論理 tick で誤判定しない。
- nonce 1 枚ずつの往復後に 64 回以上の ring wrap、異なる payload、欠落数、抜差し復帰を確認する。

### RH3 S-B・S-C・S-D

| 区分 | 操作・入口・合格・失敗時 |
|---|---|
| 入口 | RH2 合格。ARCH_BRINGUP_ORDER の RAM root→CLI→USER→入れ子→sh→保存先の順を守る |
| ユーザー操作 1 | OF で `boot enet:192.168.98.1,os32-sb.elf,192.168.98.2` を入力する |
| ユーザー操作 2 | S-B 合格後に OF で `boot enet:192.168.98.1,os32-sh.elf,192.168.98.2` を入力する |
| ユーザー操作 3 | 保存試験後、hostfs を動かしたまま G4 の電源ボタンを 5 秒押して切る→再投入し Command-Option-O-F、同じ sh の boot 行を入力する |
| ノート操作 | S-B は LAN 端末で hello 出力/exit、fault 復帰を保存。S-C は nest 親子復帰→製品 sh 直接起動を照合 |
| ノート操作 | host に `--terminal --root "$HOSTROOT" --allow-write /save` を追加し専用保存域で S-D（許可パスは実装契約と照合） |
| 観測 | rd0 読取 root・rd1 /tmp、hello/exit、fault 後 prompt、子から親への復帰、sh 入出力が LAN に残る |
| 観測 | /host の作成/追記/短い上書き/64KiB 複写/列挙、再起動後の保存、root 書込拒否、tmp 消失 |
| 失敗時 | 直前の合格像へ戻す。FS 不在は期限内に端末へ復帰し再開できるかを確認、LAN 故障と USER fault を分ける |

- RAM root は同梱済み（D7）。RH3 は既存像の大きさ・予約範囲の検査と実機照合だけで、blob/mount/build の二重実装はしない。CD/ディスクは前提にしない。
- RAM root 起動後に TFTP サーバだけ停止しても cat/exec が動くことを確認する。hostfs は別プロセスで継続する。
- QEMU の shell_run/sb_run/sc_run/sd_run の期待値を実機 runner へ移す。QMP・serial での入力を実機の証拠に混ぜない。

### RH4 uPD720101 のカード

| 区分 | 操作・入口・合格・失敗時 |
|---|---|
| 入口 | RH3 合格、USB 1P と共通 OHCI の先行試験済み、カード現物の電圧/キー/空き slot を確認 |
| ユーザー操作 1 | G4 を電源断し、適合する slot に uPD720101 を挿す（PC-98 の HighPoint 入替指示とは別） |
| ユーザー操作 2 | 起動後 OF で `boot enet:192.168.98.1,os32-sh.elf,192.168.98.2` を入力する |
| ユーザー操作 3 | 指定したカード側 port へ USB キーボード/マウスを接続し、指定キー入力と抜差しを行う |
| ノート操作 | PCI domain/BDF と 1033:0035×2・1033:00e0×1、BAR、OCR/IR、pool/HC 状態を LAN 保存 |
| 観測 | カメラは段継続。LAN の USB 入力ログに押下/解放/抜去、2 OHCI の独立動作と端末待ち中の service 継続 |
| 失敗時 | EHCI の所有権/port routing→OHCI→DMA の順で確認。失敗 HC を隔離し、他 HC と LAN の継続を観測 |

- USB_DESIGN 末尾の 1P は入力ログまで。USB キーで sh を操作できることを、既存実装済みと誤記しない。
- EHCI の高速転送は RH4 の必須にしない。ただし companion OHCI へ渡すための EHCI 静止/CONFIGFLAG は必要。

## 3. 要るコードと境界

### RH1: OF の段印・切離し後の最小表示

```c
/* 新規 platform/powermac/rh_observe.h: 所有者は rh_observe.c */
struct pm_rh_fb { u32 pa, bytes, stride, width, height, depth; };
int pm_rh_capture(int stdout_ih); /* OF が生きている間に表示と translate を採取 */
int pm_rh_of_mark(unsigned stage); /* stdout に大きい段名、detach 後は呼ばない */
void pm_rh_bind(u32 fb_va);       /* arch が作った I/G alias、DR=1 でのみ使う */
void pm_rh_stage(unsigned stage);/* 固定矩形/数字、最後の段を RAM にも保持 */
void pm_rh_phy(u32 port, u32 addr, u32 id1, u32 id2); /* 大きい ID ページ */
```

- OF の stdout が display か確認し address/width/height/depth/linebytes を保存。address を無条件に PA としない。
- OF MMU の translate で上端の帯だけ PA/連続性を解決。stride×表示行数と範囲の桁溢れを検査し、PA の頁内 offset を加えた頁丸め長 ≤256KiB とする（全画面は写さない）。
- 最初は既知の 8/16/32bpp のみ (8bpp は G4 実機の OF の画面、640×480・linebytes 768、11-13 に確認)。未知形式では OF 段印と採取値を残して停止、GPU モード設定は足さない。
- `kernel_boot.c:212` の ESCC 準備前に OF 印 0、capture 後に 1、reserve 後に 2、既存 TAKEOVER を最後に出す。
- `kernel_boot.h` に `PPC_BOOT_FB_VA=(MEM_DEVICE_APERTURE_BASE + 0x200000U)` (=0xFE200000〜0xFE23FFFF、静的な PT がある先頭の 4MiB の中の空き。既存の別名は 0xFE300000 から上)、`kernel_boot.c:35` の slot 表に同 VA/256KiB を追加し、既存窓との非重複を検査して I/G 写像。devices に fb_va/clock_va を渡し、`init/ppc_kernel.c` の paging 後・ESCC 初期化前で bind→stage(1)、OF 呼出し増分は 0。
- detach〜paging の間に停止した場合は TAKEOVER が最後。FB 段 1 を偽造せず、その区間を未通過と扱う。
- boot/mmu の診断像にも同じ表示口を接続するが、mmu 像の写像寿命に合わせ独立に I/G 窓を作る。
- 毎フレーム描画せず段が進んだ時だけ更新し、上端の帯に全到達段を残す（PHY ID も同じ帯の固定ページ）（約 5fps で短い遷移を見失わない）。

### RH2: UniNorth clock と PCI の順序

```c
/* lan_boot.h の snapshot に追加。値の所有者は gmac_tree.c */
struct pm_clock_boot { u32 pa, mask, valid; }; /* 実機 profile で pa=0xf8000020, mask=2 */
/* kernel_boot.c: 既存 PPC_BOOT_GMAC_CLOCK_VA に1頁 I/G slot を追加・検証 */
int pm_gmac_quiesce_setup_rh(const struct pm_gmac_boot *b,
                           const struct pm_pci *p, u32 clock_va);
/* pci_config.c の early_quiesce: 呼出し順の仕様（擬似コード） */
/* clock(valid profile) -> readback -> PCI ID -> Command.Master=0 -> readback */
/* allocator 開始 -> DMA pool/ring 準備 -> ring 公開時だけ Master=1 */
```

- 手本: `/home/hight/os32-tmp/ref/netbsd/sys/arch/macppc/pci/uninorth.c:135-142`、gmac 子を確認して native `|=0x02`。
- `gmac_clock.c:8-28` の native アクセス/RMW/readback を再利用。PCI/GEM の LE32 に変換しない。
- clock profile は DMA の `profile=1` と独立：採取 model が検証済み QuickSilver の値に一致し、`/uni-n` の node/reg が 0xf8000000 と +0x20 を含む範囲を示す時だけ有効。条件を満たさなければ clock を呼ばずに先へ進む (QEMU も実機の未知の model も同じ。停止しない、固定番地へ書かない)。model の値は RH0 で採った文字列を入れる。
- `kernel_boot.c` の alias 計画・重複検査・walk 検証へ clock を追加。clock と bridge config の PA を混同しない。
- early_quiesce の所有者は `pci_config.c`。`init/ppc_kernel.c:172-173` が dev->clock_va を setup に渡す。clock 後・quiesce 前の PCI Command と GEM TX/RX enable を RAM に保存して LAN 開通後に出す。採取のため静止を遅らせず service/GEM IO から clock を二重に呼ばない。
- clock 失敗は `RH CLOCK rc=...` と FB で示して LAN を無効化。BM 解除の readback=1 は既存どおり停止。

### RH2: PHY の採取と対応

```c
/* gem.h / gem_phy.c: 状態は各 struct gem が持つ。追加口 */
struct gem_phy_sample { u32 port, addr, id1, id2, bmcr, bmsr, anar, anlpar; };
int gem_phy_sample(struct gem *g, struct gem_phy_sample *out);
/* gem_mif_init は ID を保存してから戻る。未知でも sample は読取専用で使える */
/* lan_service.c: MIF 終了直後、成功/未知の両方で pm_rh_phy と記録を呼ぶ */
/* 未知: BMCR 読取、BMSR 2回（後者を採用）、ANAR/ANLPAR 読取だけ。PHY 書込みなし */
```

- 手本: `ref/openbsd/sys/dev/ic/gem.c:249-293,1260-1268` の MDI 選択/MIF、既存 gem_phy.c:77-95 と照合。
- link の読みは `ref/netbsd/sys/dev/mii/ukphy_subr.c:68-96` の BMSR 2回/BMCR/広告。link と速度確定は別の事実。
- BCM5201 は `ref/openbsd/sys/dev/mii/bmtphy.c:236-240` の標準 reset。既存 500ms reset/5秒 AN の期限を保持。
- BCM5400/5401/5411 等なら実 ID/revision を照合し `ref/openbsd/sys/dev/mii/brgphy.c:764-782,1002` の固有処置だけ別 profile に追加。
- ID を先に採り、搭載 PHY を推測で BCM5411 等と決め打ちしない。未知を標準 PHY として自動運転させない。
- 未知の標準レジスタ読取りは診断に限定し、既存設計 §8 の未知拒否を維持。対応 profile を足した回に再起動して受入する。
- 追加ログ: `RH PHY port=... addr=... id1=... id2=... bmsr=... rc=...`。RAM 記録を link 後に既存 log queue へ一度送る。

### RH0/RH1/RH3: TFTP 像とホストの口

- 配布形は raw binary でなく ELF32・MSB・EM_PPC・実行形式。entry は executable PT_LOAD 内、offset/VA の alignment を整合させる。
- PT_LOAD は filesz≤memsz、BSS/stack/予約領域を含む memsz の終端まで OF に確保させる。NOTE は既存の OF 用を保持する。
- boot=0x20000、kboot=1MiB はそれぞれ link map/readelf の実測で確かめる。OF 4.x の受理自体は RH1 の実測条件。
- Apple OF の一時ロード先 load-base（通例 0x800000、実機採取優先）から PT_LOAD へ移す前提で、全 PT_LOAD の PA 終端（memsz 込み）< load-base を realhw_image.py が必須検査する。
- ELF 全長（TFTP filesz、segment の p_filesz とは別）の上限は、実機で確認した load-base 以降の連続利用可能域の終端との差。load-base+全長の溢れ・予約域との交差も拒否し、上限不明では配布しない。setenv で逃げず像を縮める。
- linker の同梱範囲は D7 で確認済み。NOTE 等の全適合は未判定で配布検査を要する。既存 root 像・予約/DMA 池の非衝突も照合する。
- AF_PACKET は既存 Packet を使う。socket の htons と bind の protocol 表現を混ぜて「修正」せず pcap で wire の 0x88b6 を証明する。
- ノートで実 iface の MAC=getname、G4 peer 一致、PACKET_OUTGOING 除外、短い frame、別 EtherType、boot_id 更新を試す。
- `def run_rh(host, case, evidence): ...` を新規 tools/ppc/realhw_run.py に置く。Host を使い回し判定結果/期限/証拠だけ所有する。
- root socket 作成失敗は RH0 失敗。UDP の代替試験を AF_PACKET 合格と記録しない。

### RH4: USB 1P の実機差分

- `usb_boot.c:41-54` の OCR→IR 待ち→IRQ 無効→RESET を現物で追う。OF keyboard が使った HC の静止を省かない。
- `int pm_usb_rh_snapshot(struct pm_usb_rh *out)` を usb_rh_diag.c に置き、採取済み PCI/HC 状態を LAN 診断へ渡す（再初期化しない）。
- USB 状態/pool は既存 1P の所有のまま。GMAC 不在でも USB config/BAR alias を構成し、専用 64KiB I/G pool を使用。
- 2 OHCI と EHCI の BDF/domain を保持。EHCI 静止と companion routing が未対応なら usb_boot 側に限定して足す。
- tick slot と terminal poll の TB 10ms 協調処理を維持。DEC だけが tick_count を更新し、入力は既存のログ観測まで。

## 4. 大きさの見積もり

| 段 | 追加 C/Python（概算、試験別） | 像/RAM の概算 |
|---|---|---|
| RH1 | 表示/採取 250〜400 行、arch 接続 80〜130 行 | text/rodata +8〜16KiB、記録 ≤4KiB、FB は既存 VRAM の alias のみ |
| RH2 | clock 接続 70〜120 行、PHY 採取/profile 100〜220 行、段ログ 40〜70 行 | +4〜10KiB、clock 1頁の MMIO 窓、既存 DMA pool 再利用 |
| RH0/RH3 | ELF 検査/runner 200〜350 行、root 同梱の追加 0 行 | 既存 ELF 内 root ISO の実バイト数を計測、BSS を TFTP バイト数に算入しない |
| RH4 | 既存 1P の診断/必要差分 60〜180 行 | +2〜6KiB、既存 USB 64KiB pool を二重確保しない |

- 現在の像サイズは未実測。総増分 text 等は約 14〜32KiB（root ISO は既存分）、別に alignment padding を見込む。
- 配布時に `readelf -h -l -n`、size、map、stat、sha256sum を保存し、filesz と memsz と TFTP 秒数を別欄へ記録。
- 既存 root を含む PT_LOAD 終端が load-base 制限を超えれば配布を止め像を縮小する。DMA 窓や root 配置をその場で動かして回避しない。

## 5. ファイルごとの変更一覧

| 段/束 | ファイル（新規は +） | 1 ファイルの責務 / 依存 |
|---|---|---|
| 共通契約 | +platform/powermac/rh_observe.h、lan_boot.h、gem.h | 各ヘッダは自分の境界だけ。先に契約を固定 |
| RH1-A | +platform/powermac/rh_observe.c | OF 採取・持続段表示。clock/PHY と独立に作業可能 |
| RH1-B | arch/ppc/kernel_boot.c | OF 印の呼出し、FB/clock slot・alias・検証の統合。統合担当 1 人 |
| RH1/RH2 接続 | arch/ppc/kernel_boot.h、init/ppc_kernel.c | 前者は FB VA と devices の fb_va/clock_va、後者は FB 段 1・ESCC 診断分岐・clock_va を quiesce setup へ渡す |
| RH1-C | platform/powermac/boot.c、mmu_boot.c | 各診断像の段印接続。ファイルごとに別差分 |
| RH2-A | platform/powermac/gmac_tree.c | clock profile の採取だけ |
| RH2-B | platform/powermac/pci_config.c | early_quiesce の clock→PCI 順。gmac_clock.c の既存処理を使用 |
| RH2-C | drivers/net/gem/gem_phy.c | 標準読取 sample/採取 ID に合う profile、core に画面処理を入れない |
| RH2-D | platform/powermac/lan_service.c | PHY 採取の通知・nonce/段ログ、既存 poll budget 維持 |
| RH0-A | +tools/ppc/realhw_image.py | ELF/NOTE/link 範囲の配布検査。未知 NOTE 内容を捏造しない |
| RH0-B | +tools/ppc/realhw_run.py | AF_PACKET を使う実機試験/証拠、QEMU runner と結果を分離 |
| RH4-A | +platform/powermac/usb_rh_diag.c | PCI/HC の読取 snapshot |
| RH4-B | platform/powermac/usb_boot.c | 必要な場合だけ NEC EHCI 静止/companion、既存 takeover 再利用 |

- 表の独立束は契約確定後に並列で出せる。今回は設計だけで、担当生成・実装・リポジトリ変更はしていない。

## 6. 受入項目 → 試験と変異

| 受入 | 先行 host/QEMU 試験 | 実機だけで閉じる試験 | 一意の変異アンカー案 → RED 条件 |
|---|---|---|---|
| BOOT ELF/NOTE/map | header/segment・load-base/全長上限検査、boot fixture | RH1 の 3 像、Apple OF/0x20000/ch-a/map | RH_ELF_MEMSZ: memsz を縮小 → 検査失敗 |
| OF/FB 観測 | 偽 CI の呼出し順、偽 FB と範囲 guard | 右画面の段、detach 後 stage(1) | RH_FB_DETACH: detach 後 CI 呼出し → CI カウント不一致 |
| MMU/HID0/HIGH_BAT | 7450/7455 の既存 mmu/kboot 受入 | 実 PVR、DEC pending、HPT/BAT、cache/WIMG | RH_FB_ATTR: I/G を落とす → page walk 属性検査失敗 |
| KBOOT ranges/MPIC/TB | property fixture、②全印 | 個体の tree、MPIC LE、TB と実時計、1kHz/100Hz | RH_CLOCK_RATE: DEC divisor を変更 → 計数比が不一致 |
| UniNorth clock/domain | MMIO 模型で native RMW/順序、実機/QEMU/未知 tree で clock 呼出し可否 | readback→PCI ID、正しい domain | RH_CLOCK_ORDER: clock 呼出し削除 → PCI 先行を模型が検出 |
| PHY ID/標準読取 | 既知/未知/0/ffff、BMSR latch 模型 | ID/rev、固有処置、100FD と再接続 | RH_PHY_SECOND: 2回目を削除 → latch fixture が不一致 |
| 未知 PHY | 書込監視つき fixture、既存拒否試験 | 大きい ID 表示と採取票 | RH_PHY_UNKNOWN: 未知を既知扱い → PHY 書込検出 |
| DMA/cache | nonce/guard、QEMU ring wrap | cache 有効で多数 wrap、pcap と payload | RH_DMA_SYNC: buffer sync を除去 → 呼出し契約模型 RED、実機再現は別 |
| AF_PACKET/PLN1 | codec/Packet socket 模型 | root 実 iface、入出力/peer/0x88b6 | RH_PACKET_OUT: outgoing 除外削除 → 自送信の混入検出 |
| S-B/S-C | sb_run/sc_run の期待値/既存変異 | LAN 端末で hello/fault/nest/sh | RH_USER_EXIT: exit 値を変更 → realhw_run.py の偽 Host で exit 応答の期待値不一致（実機不要） |
| S-D | sd_run、hostfs 境界検査 | TFTP 停止、保存/再起動/tmp 消失/FS 不在期限 | RH_ROOT_KEEP: root 書込を許す → 書込拒否ケース失敗 |
| USB | usb_run、EE=0 待ち/時刻源の既存変異 | NEC 2 OHCI、抜差し、LAN と同時動作 | RH_USB_OCR: OCR 削除 → IR を保持する模型で start 拒否 |

- 新規試験は CHECK に入力を宣言し、各変異は実行時 RED を確認。危険な変異像を実機へ配る必要はない。
- 既存の product 行を変える実装時は tools/tests の mutation anchor を検索・移動し、触った試験だけ --mutate。
- 実装時は指定 base の preflight、影響する PPC 各像のビルドと既存先行受入、lint を最終差分後に完走する。
- full mutation/check-changed は回さず CI に委ねる。今回の設計のみでは preflight --fix-fonts/ビルド/実機試験は未実行。
- 証拠の単位は像 SHA256 + 実機個体/OF/PVR + boot_id + 時刻 + camera frame 名 + pcap + raw log + 判定表。
- 各 RH 台帳は項目ごとに閉じる。未観測の HIGH_BAT/PHY/cache 等を段全体の成功だけで一括完了にしない。

## 7. 決定 (2026-11-13、ユーザー 3 件・PM 2 件)

| ID | 決定 |
|---|---|
| U1 | (ユーザー) G4 はノートと同じスイッチにつなぐ。共用の LAN なので peer はノートの MAC に限る |
| U4 | (ユーザー) ノートの BOOTP/DHCP は G4 の MAC だけに答える形なら可。IP の直接指定が弾かれた時の第二経路。既設の DHCP (ルーター) と競合したら pcap で応答元を記録して止める |
| U5 | (ユーザー) OF の NVRAM (setenv) は既定では変えない。要る時は PM が具体的な変更と戻し方を示して先に聞く ([D2]) |

- U2 は PM が各段の日程を決め、右へのカメラ切替時期をユーザーに知らせる。観測中は右へ固定し、PC-98 の画面受入と時間を分ける。
- U3 は PM 決定済み：TFTP はノート、AF_PACKET と同じ L2。証拠の担当も同じ Remote Control セッションに揃える。
- IP/iface/実 PHY/OF 引数受理/FB 形式は採取で解く技術項目。ユーザーに仕様選択を求めない。
- RH4 のカード取り付けは計画上のユーザー操作。カード適合と空き slot は実物で確認し、不適合時だけ代案を上げる。
反論: 0 件。

## 確定 (2026-11-13、Opus 5.5 の第 2 回の条件付き確定を PM が反映)
- N1 (P1): FB の VA を 0xFE400000 (静的な PT の外、install_alias → leaf で PDE が無く ppc_stop) から、先頭の 4MiB の中の空き `MEM_DEVICE_APERTURE_BASE + 0x200000` へ。実装の段で既存の slot との非重複を検査に入れる。
- N2 (P2): clock の判定は「条件を満たさなければ呼ばずに先へ進む」(QEMU も実機の未知も同じ)。
- N3 (P2): RH0 の操作 1 と §7 を U1 の決定 (同じスイッチ) に合わせた。
- N4 (P3): RH0 の操作 4 に `dev /memory` の available を足した (ELF 全長の上限)。
- N5 (P3): §7 を決定の表に替え、既設の DHCP との競合の止め方を足した。

## 実機の受入で決めたこと (2026-11-13、PM)
- OF の画面は 8bpp (640x480、linebytes 768) だった。FB の帯は 8/16/32bpp を受け付け、8bpp は Apple の CLUT の 0 (白)・0xff (黒)・0x23 (赤) で描く。
- /memory の reg は空の DIMM を size 0 で並べる。kboot_capture は size 0 を飛ばし、全部空なら MEMORY-EMPTY で止める (mmu_boot と同じ扱い)。
- OF は読み込んだ像の直後に 32KiB を持ったまま available に戻さない (available は 0x192000 から)。kboot_reserve は像の直後の 64KiB までの隙間を窓から外し、image_end をその先に置く。
- 切り離し前の停止は全部 OF の画面に `FAIL:KB:<理由>` を出す。RESERVE には理由の番号・像の範囲・RAM と available の一覧を添える。
- リンク確立の直後に送ったフレームが届かなかった (beacon と log 0〜2)。lan_service は RUNNING の後 2 秒は送らずに受けるだけにした。
- 実機の結果と証拠の所在は票 TASK_PPC_REALHW の §2。

