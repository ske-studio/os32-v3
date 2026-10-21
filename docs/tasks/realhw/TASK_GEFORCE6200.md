# TASK_GEFORCE6200 — 実機 Ra266 の GeForce 6200 (PCI 0:13.0) を観測して画面ドライバの根拠を集める

> 状態: **設計中 (2026-10-21)** — 段 0〜3 は観測済み、段 4a と戻す処理の詳細設計を確定、次は実装 (§5、sol 実装 → astra レビュー)。実機の書込みは段ごとにユーザー承認・立会い。

## 0. ユーザー決定 (2026-10-20)

- **観測は Ra266 に挿してある実機のカードで直接行う。** PC-98 の PCI の割当て・レガシー VGA が無いこと・BIOS が POST しない「冷えた」カードという調べたい条件そのものを見られるため。Linux の PC/AT で nouveau を動かすのは、自前の初期化で映らないときに「正しく初期化された状態」と見比べたい場合だけの予備とする。
- **最初の段は読み取りだけ** (PCI の列挙と BAR、チップ ID、VBIOS の吸い出し、主要ブロックのレジスタの記録)。OS32 自身の小さな道具で行い、結果はシリアルで持ち帰る (画面確認の要らない試験は立会い無しで可)。書き込み (初期化の試行) に入る段からは、実機の物理操作の承認を段ごとに取る。
- **リバースエンジニアリングを明示的に禁じているもの (NVIDIA の独自ドライバ等) は解析しない** ([HW3])。根拠は、資料・ライセンスの合う公開実装 (envytools・xf86-video-nv・Haiku・nouveau の MIT の部分)・OS32 のコードでの実機観測だけ。
- **nouveau の記述は仕様として信頼する** (ユーザー決定 2026-10-20: nouveau はクリーンルーム開発を掲げている)。取得した範囲 (devinit・bios・fb・clk・dispnv04) は MIT で、[HW3] の (2) として参照してよい。資料・envytools・Haiku と食い違えば、両方を書いて実機で確かめる点にする ([HW3] の手順どおり)。
- エミュレータのダミーデバイス (NP21/W 側に「同じ結果になるはず」の最小模型、2D の意味は決定的なソフト実装、DirectX は表示だけ) は、観測で根拠がそろった後に検討する。模型は資料と観測から作り、ドライバを書く側の理解から作らない。

- この票は、外部デバイスの実装方法のモデル (専用の文書群、[ROADMAP](../../ROADMAP.md) の長期テーマ) の材料にする (ユーザー決定 2026-10-20)。

## 1. 対象

PCI 0:13.0 `10de:0221` (NVIDIA NV44A、GeForce 6200)、class 03.00、rev a1、subsys `10fc:1270`、irq 6、mem 0x24000000〜 (PLAN.md §1 の 2026-10-15 の基準構成)。OS32 は列挙だけで結線なし。

## 2. 参照資料

`~/os32-tmp/refs/geforce6200/INDEX.md` (envytools f102b82・xf86-video-nv 8234bce・Haiku af754c7c の nvidia ドライバ・nouveau c03c1a1 の devinit/bios/fb/clk/dispnv04、版とライセンス、見る所)。リポジトリには入れない。

## 3. 手順の計画 (確定 2026-10-20、Fable 起草・Opus 2 往復・PM が最終修正)

全文 (根拠の資料と行番号、合否、危険と戻し方): `~/os32-tmp/run/gf6200/PLAN.final.md`。

| 段 | 中身 | 触る物 | 立会い |
|---|---|---|---|
| 0 | PCI と BAR の割当て、C バスブリッジ (subtractive) の窓・Trident との資源の衝突を実値で照合 | 読むだけ (`pcidump`・`lspci -v`) | 不要 |
| 1 | BAR0 の冷えた値: PMC 0x000/0x200 を単独で先に読み、ブロックを 1 つずつ (読む前に番地を 1 行出す)。VGA 別名 (PRMCIO/PRMDIO) は総なめせず 3C2/3CC/3C4/3C5/3CE/3CF/3D4/3D5 をバイトで読むだけ。チップ ID は bits 20-27=0x4A・16-19=1 | 読むだけ。写像はデバイス窓の先頭 4MB の PT 1 枚を付け替え (窓 A = BAR0+0、窓 B = BAR0+0x600000)、終われば戻す | 不要 |
| 2 | VBIOS の吸い出し: PROM 窓 BAR0+0x300000 をバイトで読む (cfg 50h=0 で開いている)。ROM BAR は書きを伴うので段 4 の予備 | 読むだけ | 不要 |
| 2b | 吸い出した ROM の著作権・使用許諾の文字列をユーザーに見せる。**解析を禁じる記述があれば段 3 を止める** | ホストだけ | — (ユーザー判断) |
| 3 | VBIOS の表 (BIT/PINS・初期化 script) をホストで解析し envytools・Haiku の記述と照合 | ホストだけ | 不要 |
| 4a | 承認地点 A: BAR の大きさの測定と初期化 script の試行 (PLL・PFB・メモリ)、VRAM の読み書き確認 | 書く | 承認 |
| 4b | 承認地点 B: **PBUS 0x1854←0 を先に**、映像出力の有効化 (PBUS 0x1850←1)、合否に「A0000h のテキストが表示されたまま」 | 書く | 承認 |
| 4c | 承認地点 C: 画面モード (PCRTC/PRAMDAC、整数 PLL)、98 側との共存 (別コネクタ) | 書く | 承認・**モニタ接続はユーザー** |

道具: [TASK_KPEEK](../v3/TASK_KPEEK.md) の peek (T7 の後の最初の実装) に MMIO の許可リストを足して使う。結果はシリアルで持ち帰る。PM 決定: Haiku のライセンスは調べて記録、envytools の nvbios はホストで自前の ROM に当てて使ってよい (MIT)、BAR の大きさは段 4a で測る、cfg 54h は段 4 で扱う、固まったときの電源断は 2026-10-12 の決定 (立会い無しの試験) の範囲。

## 4. 観測の記録

### 段 0〜2 (2026-10-21、実機 Ra266 1f1d115・KDEBUG=1、KPEEK、読み取りだけ、ノートの `~/os32-tmp/evidence/2026-10-21/gf6200_s012/`)

- 段 0: Command 0x0106 (Mem+・I/O−・BusMaster+)、BAR0 0x24000000・BAR1 0x30000008 (pref)・BAR2 0x25000000、ROM BAR 0x26000000 (無効)、cfg 50h=0 (PROM の窓は開いている)・**54h=1**、IRQ 6。C バスブリッジ 0:6.0 の 50h〜5Fh は 00 01 02 0f 04 85 86 07 88 09 8a 0b 8c 0d 0e 8f。Trident・82557 と BAR は重ならない。
- 段 1: **PMC_BOOT_0=0x04A100A1** (NV44A)、PMC_ENABLE=0x03010000、PMC 004/100/140=0、PSTRAPS 23409E8F 7FEF81C3 00001E0C 00000C90、**PFB 4KB は全部 0** (冷えている)、PCRTC h0/h1・PRAMDAC h0/h1 に 0 でない語 (7/7/41/30)、VGA 別名 3D5=0x10 ほか 0。固まらず DENY 0。1 回 約 3.3 秒 (LAN)。
- 段 2: PROM の窓 128KB を 2 回読んで一致 (CRC F5ADEE03)、先頭 55 AA 7C (ROM 63,488 B、sum8=0)、後半 64KB は前半の写し。sha256 ec460969…dedd。「GeForce 6200 VGA BIOS IDG1」「Version 5.44.A2.10.00」「nv44 Board - p362h0」。
- 段 2b (使用許諾の文字列): "Copyright (C) 1996-2004 NVIDIA Corp."・"NVIDIA"・"NVIDIA Corporation" だけ。license・reverse・disassembl を含む行は無い → **ユーザー決定 (2026-10-21)**: 段 3 に進む。ROM の**データの表** (BIT/PINS・初期化スクリプト) を envytools の nvbios・nouveau・Haiku の記述と照らして読むのは可。**ROM の x86 コードの逆アセンブルはしない — 最終手段として、要るときはユーザーが判断する**。
- 気づき: PMC_ENABLE と PCRTC/PRAMDAC が 0 でないのは、PC-98 の BIOS が POST しない前提に対して何かが一部を有効にしている可能性 (電源投入時の既定値か)。cfg 54h=1 なので段 4b の前に PBUS 0x1854←0 (計画 G1)。

### 段 3 (2026-10-21、ノートで envytools f102b823 の nvbios、逆アセンブルなし、`~/os32-tmp/evidence/2026-10-21/gf6200_s3/`、s3_nvbios.txt 823 行 sha256 f31e1363…7e68)

- BIOS 5.44.a2.10.00 (08/29/05)、NV44/NV44A、BIT 0x0218 版 1.0 (表 2・B v2・C・D・I・L・t・M・P・S v2・T・U・V・i v2)。
- 表: init script 表 0x5d4c (main 5 本)・condition 0x5d86 (3)・IO flag condition 0x5daa (8)・macro 0x5d66 (4)・PLL limits 0x5bc8 (版 21、4 件、ref_clk 0 = strap から)・DCB 0x96ac (版 3.0: ANALOG OR0・ANALOG OR1・TMDS OR0 (DVI-I)・TV)・I2C 0x9715・GPIO 0x974e・CONN 0x97da (VGA・DVI_I・COMPOSITE・S_VIDEO・RGB)・TMDS 0x95ea・perf 0xdcef (Core 300MHz・Memory 200MHz の 1 段)・memory timings 0xddd1 (全部 0)。strap の上書き select ffef81c3 / value 00001e0c は PSTRAPS の実値と一致。
- init script: main 0〜4 と SUB/CALL 先を合わせて 9 本、命令 193 個・27 種類 (多い順 ZM_REG 42・NV_REG 36・IO_RESTRICT_PROG 32・TIME 13・ZM_CR 9・DONE 9・INDEX_IO 8・RESUME 6・MACRO 5・…・COMPUTE_MEM 1)。PLL は R[4004]=200MHz・R[4024]=400MHz・R[680508]=36MHz・R[402c]=c1c11002。
- **基準水晶は 27MHz** (PSTRAPS bit22=1・bit6=0、nouveau の判定 strap&0x00400040 == 0x00400000)。
- **メモリ**: RAM の組 3 (CR3C の strap bits 2-5 から)、組 3 の PFB 値 (100220=03080e0b・100224=05010405・100228=20220303・10022c=41088102・100234=00266622 ほか)。DDR 系・200MHz までの推定、量は init 3 の COMPUTE_MEM が実行時に測る (段 4a で BAR1 の大きさと合わせて確かめる)。
- 次: 段 4a (初期化 script の試行、書き込み) は 27 種類の命令の解釈器が要る → astra の設計 (nouveau の devinit/bios の MIT 部と Haiku の coldstart を根拠) と Opus のレビューを先に。書き込みの実施は段ごとにユーザーの承認。

### 映像の経路 (ユーザー 2026-10-21)

- **GeForce 6200 の映像は「抜いちゃダメボード」経由で 98 の切替回路につながっている** (1 本のモニタ)。切替回路は 98 表示・内蔵 (Trident)・PCI ボードの RGB-IN の 3 つから 1 つを選び、選ぶのは UNDOCUMENTED の 0FACh の bit0 (RGB IN のスルー / 内蔵) と bit1 (内蔵 / 98) [推] — [TASK_TRIDENT_DRIVER §3-7](TASK_TRIDENT_DRIVER.md)。Ra266 も 0FACh と同じ仕組みとして扱う (2026-10-17 のユーザー決定)。
- 帰結: 4b・4c で GeForce の映像を見るには 0FACh で RGB-IN を選ぶ。**戻す処理 (realhw_recover) の「98 の画面に戻す」は 0FACh で 98 表示を選び直すことを含む** — recover r2 の「0FACh を GeForce に使わない」は取り消した (§5 の確定設計)。入力の選び違いは黒画面として現れる (§3-7)。モニタの決定 (U3) はこの経路で決まり、4b の前に 0FACh の値と画面の切り替えを確かめる。

## 5. 詳細設計 (確定 2026-10-21、astra 起草・Opus 3 往復・PM が最終修正)

全文: 段 4a `~/os32-tmp/run/gf6200_4a/DESIGN.final.md`、戻す処理 `~/os32-tmp/run/realhw_recover/DESIGN.final.md` (往復は同じ所の r1〜r3・REVIEW)。

- **ユーザー決定**: N1 main3 の E81E (SR01 bit5 の解除) だけを cfg 50h/54h=0 の保持を条件に許す。U1 固まったら当面ユーザーが手で戻し、書込みの段は立会い。U2 A1 (BAR の測定) → A2 (準備) → main 0〜3 の script ごと → A4 (VRAM の確認) を、結果を見て段ごとに承認。U3 4a はモニタ無し、モニタは 4b の前に決める。
- **4a の実装段**: S1 fixture (27 種 193 命令) → S2 decode → S3 exec → S4 PLL → S5 bus・窓の共有 → S6 kdebug 統合 → S7 shell と KAPI 共有ヘッダ → S8 実機 A1/A2 → S9 以降 main ごと、最後に A4。各 0.5〜1 日。
- **戻す処理の段**: T1 状態機械 → T2 窓・profile・98 復帰 → T3 shell/host/deadman → T4 NP21/W → T5 実機の無書込み (+ 0FACh の初めての書込みを明示承認の 1 回) → T6 承認済み書込み → T7 復旧演習。4a の S5〜S6 は recover の descriptor とログの契約を使うので、**T1〜T3 を先に着地させる**。
- **98 の画面に戻す** (② の先頭): 0FACh を読み、bit1..0 が 00b なら書かず、違えば 00h を書いて読み戻す。経路の最終判定はユーザーの目視。4a は 0FACh を読み書きしない。
- **最後の手段**: CPU リセット (F0h / `sys_reboot`) は PCI RST# を保証しない。PG (電源コネクタの POWER GOOD を GND に落とす) は Ra266 で未確認で、付けるかはユーザー判断。PG 後は warm-residue として扱い、cold-required の解除に数えるかはユーザー決定。

### 4a S1 fixture (2026-10-21)

- 決定: [fixture](../../../tools/tests/gfinit_fixture.h) に 9 本・193 命令・27 種、main 順、表長、副作用候補 619 行を収録。NV_REG 圧縮 3 行は各 4 命令、COPY_NV_REG は E7FB。
- N1: main3/E81E だけ cfg50/54・PBUS1850/1854=0 の前後確認・保持を条件とする旗。他の表示有効化・W 行不足は静的拒否。
- 設計の欠け: T1 descriptor に phase・head/port/index の共有符号化・値制約・依存順序・N1 guard が無い。対応表を fixture に記録し、全 W 候補を拒否のまま保持。recover.h は変更せず、許可拡張は実装しない。
- 未確認・持越し: PLL 内部値・安全な読戻し mask は記号値。ROM 実バイト、PLL 表 entry 長、実機は未検証。共有 descriptor の解決・レビューまで実機 RUN 許可無し。
- ノートで `python3 tools/gfinit_rom_check.py --list` が各 script の開始・長さ・IEEE CRC32 を表示。同スクリプトに private ROM のパスを渡して照合（ROM を出力・保存しない）。期待値は txt からの復元値で、ROM 由来の検証済み値ではない。
- 証拠: 入力 `~/os32-tmp/run/gf6200_4a/nvbios/init_scripts.txt`、[照合試験](../../../tools/tests/test_gfinit_fixture.py)。ログは `~/os32-tmp/run/gf4a_s1/` の `kernel.log`・`fixture.log`・`mutate.log`・`lint.log`・`docs.log`・`ci-stab.log`（終了 rc を末尾に記録）。
- 未決: 外部 txt の置き場は `GFINIT_SOURCE` / `--source` で指定可。無い場合は検査失敗とし、ROM 照合はノートへ持越す。S2 以降の実装、コミット、配備は行わない。
