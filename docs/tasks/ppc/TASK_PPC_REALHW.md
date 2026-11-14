# TASK_PPC_REALHW — PowerMac G4 QuickSilver の実機で OS32 を起動し、QEMU の到達点を受け入れる

> 上位: [TASK_PPC_SHELL](TASK_PPC_SHELL.md)

> 状態: **受入完了 (2026-11-13)** — 実機で受入。G4 PowerMac3,5 (OF 4.2.1f2) で TFTP 起動 → 切り離し → LAN → S-B・S-C・S-D と内蔵 USB のキーボード → sh まで到達 (§2)。残りは持越し台帳 PPC-BOOT-RH・PPC-MMU-RH の絞った分と RH4 のカード。計画と詳細設計は [PPC_REALHW_G4_DESIGN](../../decisions/ppc/PPC_REALHW_G4_DESIGN.md)。

## 0. ユーザー決定 (2026-11-13)
- 次の段は「G4 実機の受入」(USB の段 1 の後の選択)。
- 起動は LAN の TFTP (OF の `boot enet:...`)。観測は右のモニターをノートのカメラで。
- G4 はノートと同じスイッチにつなぐ。TFTP とホストの道具 (AF_PACKET) はノート (PM 決定)。
- ノートの BOOTP/DHCP は G4 の MAC だけに答える形なら可。OF の NVRAM (setenv) は要る時に戻し方と一緒に先に聞く ([D2])。
- 2026-11-14 (PM の推奨を承認):
  - G4 は既定でネットワーク起動にする。次の G4 実機の確認の日に、NVRAM の `boot-device` を `enet:...` に、`auto-boot?` を true にする。前の値は `printenv` で控え、戻すのは `setenv` か `set-defaults`。同じ日に、起動先を並べたとき OF が次の起動先へ進むかを 1 回試す。
  - 更新の失敗への備えはノートの側で行う。新しい版は「試し」として渡し、link の接続が確立したら「確かな版」に上げる。確立しないまま次の起動要求が来たら、ノートは確かな版かリカバリー用の像を渡す。NVRAM は最初の 1 回の設定のあと触らない。
  - 段 RF の着手は、PLN1 から link への移行 (TASK_LAN_OPERATOR) の後。カーネルから NVRAM の起動先を書き換える案は、RF で足りなかったときに改めて相談する。

## 1. 段
| 段 | 中身 | 状態 |
|---|---|---|
| RA | 実機向けの道具: ELF の配布の検査 (PT_LOAD の終端 < load-base 等) と実機の試験の道具 (AF_PACKET) | main 0008c265 |
| RB | RH1: FB の上の帯 (0xFE200000) に段の印、切り離しの後の観測路、kernel_boot の FB・clock の別名 | main 0008c265 |
| RC | RH2: UniNorth の GMAC の clock gate の接続 (実機と QEMU の判定)、実機の PHY (ID の採取と profile) | main 0008c265 |
| RH0〜RH3 | 実機の準備 (ユーザーの物理操作・ノート) → 初回起動 → LAN → S-B・S-C・S-D | 実機で通過 (11-13、§2)。実機の直しは main 0008c265・6dd4c02f |
| RH4 | uPD720101 のカードで USB | 内蔵 USB のキーボード → sh は実機で通過 (§2)。カードは未 |
| RN | NVRAM を既定のネットワーク起動にする (ユーザーの物理操作、§0 の 11-14) | 済 (11-14、§2 の 11-14)。起動先を並べて次へ進むかは未確認 |
| RF | ノートで確かな版・試しの版を切り替える (link の接続の確立で昇格、ないときは確かな版かリカバリー用の像) | 未着手 (link の移行の後) |

## 2. 実機の結果 (2026-11-13)
PowerPC に固有の注意事項 (起動の行・BOOTP/BSDP・画面・メモリ・LAN・QEMU) のまとめは [PPC_NOTES](../../decisions/ppc/PPC_NOTES.md)。
証拠はノートの `~/os32-tmp/evidence/2026-11-13/ppc-rh/` (rh2・rh3・rh4)。起動は `boot enet:192.168.2.12,os32-sh.elf,192.168.2.201,192.168.2.12` (この OF は `;` も区切りに数え、4 つ目がゲートウェイ、5 つ目が bootp-retries)。OF は引数付きでも BOOTP/BSDP を先に出すので、ノートに G4 の MAC だけに答える返事役を置いた。

| 段 | 結果 |
|---|---|
| RH1 | OF の段の印 0・1・2 → TAKEOVER → FB の帯に段 1。OF の画面は 8bpp・640x480・linebytes 768 |
| RH2 | PHY port 0 addr 0 ID 0x0020/0x6071 (BCM5411)、PLN1 の beacon・log・5 診断が往復。RAM 1.5GiB、TB 33.29MHz、MPIC 64 本 |
| RH3 | S-C は sc7 で check_shell を無改変で通過 (log seq 0 から連続、drop 0)。S-D は sd1 で書き込み・64KiB 複写・追記・短縮・ls (応答が無い時の 5 秒の打ち切りは未確認) |
| RH4 | 内蔵 USB (00:18.0・00:19.0) の Apple キーボード 05ac:0206 で `echo kbd-ok` が sh に届いた (rh4/kbd1)。かな・IME は未 |

実機で見つけて直したもの: OF の画面 8bpp、/memory reg の空の DIMM (size 0)、像の直後に OF が持つ 32KiB の隙間 (64KiB まで許す)、切り離し前の停止の理由 (FAIL:KB:*)、リンク確立の直後のフレームの欠落 (RUNNING の後 2 秒送らない)。

### 2026-11-14: 今の版 (PLN1、link への移行の前) の受入
像は main 06223da7 の SH 像 (FB 文字端末・KCG1 の LZ4 入り、SHA-256 7014246a…)。証拠はノートの `~/os32-tmp/evidence/2026-11-14/ppc-rh/` (diag3・term1・host1・fb1・fb2)。

| 項目 | 結果 |
|---|---|
| RN | NVRAM の `boot-device` を `enet:192.168.2.12,os32-sh.elf,192.168.2.201,192.168.2.12`、`auto-boot?` を true (ユーザーが設定)。電源だけで TFTP 起動した |
| FB 文字端末 (PPC-FB-RH) | 待機の案内 → sh、白黒 (fg=255 bg=0)、段の印の帯、日本語 (東雲 16)、長い出力のスクロールと最下行、BS、clear (帯は残る) を確認。USB のキーボードの入力もユーザーが確認 |
| LAN | beacon・5 診断 (status・clock・memory・read-diag・peek) が往復、LAN 端末の sh (echo・ls・日本語)、`/host` の読み・書き・追記 |

見つけたもの (未修正):
- FB の画面で、入力の行の写しから日本語が欠ける (`sh> echo 日本語の表示` が `sh> echo` と出る。結果の行と LAN 端末は正しい)。
- ノートの `ppc_lan_host.py` を途中から付けたときの 1 回目は「waiting for beacon」のまま要求を出せず、`--boot-id` を渡すと通った。原因は未調査。
