# TASK_GEFORCE6200 — 実機 Ra266 の GeForce 6200 (PCI 0:13.0) を観測して画面ドライバの根拠を集める

> 上位: [ROADMAP](../../ROADMAP.md)

> 状態: **設計中 (2026-10-21)** — 段 0〜3 は観測済み、段 4a と戻す処理の詳細設計を確定、次は実装 (§5、sol 実装 → astra レビュー)。実機の書込みは段ごとにユーザー承認・立会い。

## 0. ユーザー決定 (2026-10-20)

- **観測は Ra266 に挿してある実機のカードで直接行う。** PC-98 の PCI の割当て・レガシー VGA が無いこと・BIOS が POST しない「冷えた」カードという調べたい条件そのものを見られるため。Linux の PC/AT で nouveau を動かすのは、自前の初期化で映らないときに「正しく初期化された状態」と見比べたい場合だけの予備とする。
- **最初の段は読み取りだけ** (PCI の列挙と BAR、チップ ID、VBIOS の吸い出し、主要ブロックのレジスタの記録)。OS32 自身の小さな道具で行い、結果はシリアルで持ち帰る (画面確認の要らない試験は立会い無しで可)。書き込み (初期化の試行) に入る段からは、実機の物理操作の承認を段ごとに取る。
- **リバースエンジニアリングを明示的に禁じているもの (NVIDIA の独自ドライバ等) は解析しない** ([HW3])。根拠は、資料・ライセンスの合う公開実装 (envytools・xf86-video-nv・Haiku・nouveau の MIT の部分)・OS32 のコードでの実機観測だけ。
- **nouveau の記述は仕様として信頼する** (ユーザー決定 2026-10-20: nouveau はクリーンルーム開発を掲げている)。取得した範囲 (devinit・bios・fb・clk・dispnv04) は MIT で、[HW3] の (2) として参照してよい。資料・envytools・Haiku と食い違えば、両方を書いて実機で確かめる点にする ([HW3] の手順どおり)。
- エミュレータのダミーデバイス (NP21/W 側に「同じ結果になるはず」の最小模型、2D の意味は決定的なソフト実装、DirectX は表示だけ) は、観測で根拠がそろった後に検討する。模型は資料と観測から作り、ドライバを書く側の理解から作らない。

- この票は、外部デバイスの実装方法のモデル (専用の文書群、[ROADMAP](../../ROADMAP.md) の長期テーマ) の材料にする (ユーザー決定 2026-10-20)。

### ユーザー決定 (2026-10-21)

- **N1・U1〜U3** (E81E の条件付き許可、固まったら当面は手で戻し書込みの段は立会い、段ごとの承認の順、4a はモニタ無し): 原文の意味は §5 と [TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) §0。
- **書込みは段ごとに結果を見て承認する** (U2。上の 2026-10-20 の「段ごとの承認」を 4a の A1 → A2 → main 0〜3 → A4 に具体化)。
- **VBIOS の x86 コードは逆アセンブルしない — 最終手段で、要るときはユーザーが判断する**。データの表を公開実装と照らして読むのは可 (§4 段 2b)。
- **nouveau はクリーンルーム開発を謳うので仕様として信じる**、**リバースエンジニアリングが明示的に禁じられたものはしない** — どちらも上の 2026-10-20 の決定を 2026-10-21 に改めて確認したもの。
- **映像は抜いちゃダメボード経由で 98 の切替回路 (0FACh) に入る** (§4「映像の経路」)。戻す処理の「98 の画面に戻す」は 0FACh を 98 表示へ戻すことを含む。
- **起動 FD に kdebug を載せない**。KPEEK・gfinit・recover は HDD 起動 (KDEBUG=1) の中だけで使う。 理由: 戻す処理が kdebug.bin に入って 64.6KB → 103KB になり、起動 FD (2HD) の空きが下限 64KB を割った (71KB → 33KB)。kdebug は開発用で FD (レスキュー兼インストーラ) に不要、FD からインストールした実機は hsync boot で入る。CD には残す。詳細は [08_build.md](../../08_build.md) の「起動 FD と MINIMAL」と [TASK_DBGREF](../v3/TASK_DBGREF.md) §3-9。
- 戻す処理の起点の依頼 (「実機テストの時に特定の手順でリセットや画面復帰など特定のステータスに戻す処理の設計案を追加で作って…」) と、PG リセットの扱いの未決は [TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) §0。

## 1. 対象

PCI 0:13.0 `10de:0221` (NVIDIA NV44A、GeForce 6200)、class 03.00、rev a1、subsys `10fc:1270`、irq 6、mem 0x24000000〜 (PLAN.md §1 の 2026-10-15 の基準構成)。OS32 は列挙だけで結線なし。

## 2. 参照資料

`~/os32-tmp/refs/geforce6200/INDEX.md` (envytools f102b82・xf86-video-nv 8234bce・Haiku af754c7c の nvidia ドライバ・nouveau c03c1a1 の devinit/bios/fb/clk/dispnv04、版とライセンス、見る所)。リポジトリには入れない。

VBIOS について分かっていること (表の地図・init script 9 本・27 種の命令・書込みの分類・メモリと PLL・出力) のまとめ: [GEFORCE6200_VBIOS.md](GEFORCE6200_VBIOS.md)。

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
- **メモリ**: RAM の組 3 (CR3C の strap bits 2-5 から)、組 3 の PFB 値 (100220=03080e0b・100224=05010405・100228=20220303・10022c=41088102・100234=00266622 ほか)。DDR 系・200MHz までの推定、量は init 3 の COMPUTE_MEM が測ると読んだが、NV44A では COMPUTE_MEM は何もしない (4a 確定設計 D2 で訂正) — 量は段 4a で BAR1 の大きさと VRAM の試験で確かめる。
- 次: 段 4a (初期化 script の試行、書き込み) は 27 種類の命令の解釈器が要る → astra の設計 (nouveau の devinit/bios の MIT 部と Haiku の coldstart を根拠) と Opus のレビューを先に。書き込みの実施は段ごとにユーザーの承認。

### 映像の経路 (ユーザー 2026-10-21)

- **GeForce 6200 の映像は「抜いちゃダメボード」経由で 98 の切替回路につながっている** (1 本のモニタ)。切替回路は 98 表示・内蔵 (Trident)・PCI ボードの RGB-IN の 3 つから 1 つを選び、選ぶのは UNDOCUMENTED の 0FACh の bit0 (RGB IN のスルー / 内蔵) と bit1 (内蔵 / 98) [推] — [TASK_TRIDENT_DRIVER §3-7](TASK_TRIDENT_DRIVER.md)。Ra266 も 0FACh と同じ仕組みとして扱う (2026-10-17 のユーザー決定)。
- 帰結: 4b・4c で GeForce の映像を見るには 0FACh で RGB-IN を選ぶ。**戻す処理 (realhw_recover) の「98 の画面に戻す」は 0FACh で 98 表示を選び直すことを含む** — recover r2 の「0FACh を GeForce に使わない」は取り消した (§5 の確定設計)。入力の選び違いは黒画面として現れる (§3-7)。モニタの決定 (U3) はこの経路で決まり、4b の前に 0FACh の値と画面の切り替えを確かめる。

## 5. 詳細設計 (確定 2026-10-21、astra 起草・Opus 3 往復・PM が最終修正)

全文: 段 4a [GF6200_4A_DESIGN.md](../../decisions/realhw/GF6200_4A_DESIGN.md) (S2 の引数配置は [GF6200_4A_S2_LAYOUT.md](../../decisions/realhw/GF6200_4A_S2_LAYOUT.md))、戻す処理 [REALHW_RECOVER_DESIGN.md](../../decisions/realhw/REALHW_RECOVER_DESIGN.md) と追記 [ADDENDUM_R2](../../decisions/realhw/REALHW_RECOVER_ADDENDUM_R2.md)・[ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) (2026-10-21 に `~/os32-tmp/run/` の確定版を写した。往復の r1〜r3・REVIEW は ~/os32-tmp に残る)。戻す処理の段・決定・送り項目の票: [TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md)。

- **ユーザー決定**: N1 main3 の E81E (SR01 bit5 の解除) だけを cfg 50h/54h=0 の保持を条件に許す。U1 固まったら当面ユーザーが手で戻し、書込みの段は立会い。U2 A1 (BAR の測定) → A2 (準備) → main 0〜3 の script ごと → A4 (VRAM の確認) を、結果を見て段ごとに承認。U3 4a はモニタ無し、モニタは 4b の前に決める。
- **S7 以降の段の並べ替え (2026-10-22)**: 装置ごとの部分を別の部品に分ける段 (M1〜M5) を先に入れた。順序は [TASK_HWDEV.md](TASK_HWDEV.md) §3 が正。
- **4a の実装段**: S1 fixture (27 種 193 命令) → S2 decode → S3 exec → S4 PLL → S5 bus・窓の共有 → S6 kdebug 統合 → S7 shell と KAPI 共有ヘッダ → S8 実機 A1/A2 → S9 以降 main ごと、最後に A4。各 0.5〜1 日。
- **戻す処理の段**: T1 状態機械 → T2 窓・profile・98 復帰 → T3 shell/host/deadman → T4 NP21/W → T5 実機の無書込み (+ 0FACh の初めての書込みを明示承認の 1 回) → T6 承認済み書込み → T7 復旧演習。4a の S5〜S6 は recover の descriptor とログの契約を使うので、**T1〜T3 を先に着地させる**。
- **98 の画面に戻す** (② の先頭): 0FACh を読み、bit1..0 が 00b なら書かず、違えば 00h を書いて読み戻す。経路の最終判定はユーザーの目視。4a は 0FACh を読み書きしない。
- **最後の手段**: CPU リセット (F0h / `sys_reboot`) は PCI RST# を保証しない。PG (電源コネクタの POWER GOOD を GND に落とす) は Ra266 で未確認で、付けるかはユーザー判断。PG 後は warm-residue として扱い、cold-required の解除に数えるかはユーザー決定。

### 4a S1 fixture (2026-10-21)

- 決定: [fixture](../../../tools/tests/gfinit_fixture.h) に 9 本・193 命令・27 種、main 順、表長、副作用候補 619 行を収録。NV_REG 圧縮 3 行は各 4 命令、COPY_NV_REG は E7FB。
- N1: main3/E81E だけ cfg50/54・PBUS1850/1854=0 の前後確認・保持を条件とする旗。他の表示有効化・W 行不足は静的拒否。
- S1追補: [recover GF profile](../../../kdebug/recover_gf.c) に target 145・rule 544（script 513＋固定/復元31）。619候補は rule 513／PLL 101／COPY 2／一時再生3／別名検査の欠け0に分類。残る106候補は拒否。
- 持越し: S2の実opcode layout・COPY/一時再生式、S4 PLL、S5 accessor。全ROM RUNは未許可。共有検査は実VGA別名portの重なりだけを拒否し、indexed EXPR READには全経路/実行時の先行SELECTを要求。固定SIZE_BARSは測定/復元trace、PREPAREはCR11のbind/実行まで確認。ROM実バイト・実機は未検証。
- ノートで `python3 tools/gfinit_rom_check.py --list` が各 script の開始・長さ・IEEE CRC32 を表示。同スクリプトに private ROM のパスを渡して照合（ROM を出力・保存しない）。期待値は txt からの復元値で、ROM 由来の検証済み値ではない。
- 証拠: 入力 `~/os32-tmp/run/gf6200_4a/nvbios/init_scripts.txt`、[照合試験](../../../tools/tests/test_gfinit_fixture.py)。ログは `~/os32-tmp/run/gf4a_s1/` の `kernel.log`・`fixture.log`・`mutate.log`・`lint.log`・`docs.log`・`ci-stab.log`（終了 rc を末尾に記録）。
- 追補の証拠: [host試験](../../../tools/tests/test_recover_gf.py)、`~/os32-tmp/run/gf4a_s1c/` の kernel/host/mutate-recover/mutate-gf/lint/docs ログ。設計決定は recover の [ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md)「4a S1 追補で設計者が決めたこと」。

### 4a S2 decode (2026-10-21)

- [checked decode](../../../kdebug/gfinit_decode.c) と実27種のlayout、RMW/COPY式、命令内capture、ポート別SELECT記録を追加。e18の1850はliteral 0を維持。
- S1の193命令を940 nodes・2418 args・19 ctx・5 entriesへ展開（main別220/459/122/138/1）。PLL内部101候補は含まず、2048上限内。
- COPY 2行・一時再生3行とPLL入口3件は未結線inventoryとしてbind拒否。S3 executor、S4 PLL、S5 bus/cleanup、S6結線を本段で完了扱いしない。
- 共有変更はprofile版3、capture/CRC、index保存分類と探索cache。設計決定はrecoverの [ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md)「4a S2 の実装で設計者が決めたこと」、T3送り項目はS6前へ維持。
- 証拠: [合成host試験](../../../tools/tests/test_gfinit.py)、`~/os32-tmp/run/gf4a_s2/` の `final-build.log`・`final-candidates.log`・`final-tests.log`・`final-mutate.log`・`final-lint.log`・`final-docs.log`・`final-size.log`。CIは外部ROM/一時資料を読まない。

### 4a S3 executor (2026-10-21)
- `gfinit_exec` はS2のplanをrecover経由でmain単位に実行。条件・call/repeat・skip/force・待ち・動的予算を検査する。
- S2送り項目はlayoutのheadフラグ、表位置定数、skip CR44拒否、重複処理等の整理と負の試験へ反映。固定結果CR44 RMWもbindで検査。
- 設計者判断は [ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) のS3節。PLL/COPY/採取の本番未結線8行は引き続き拒否。
- 検証証拠: `~/os32-tmp/run/gf4a_s3/`。27種の独立期待trace・各予算境界は完全mockで検証。実機・NP21/Wは本段対象外。

### 4a S4 PLL (2026-10-21)
- `gfinit_pll` は0x21表のchecked parser・27MHz/u32 solver・NV44A低位/高位列を実装。MIT表示を保持し、32bit部品リンクの未解決は0。
- PLL101候補は76件を26書込みruleへ対応付け、25件はチップ分岐で不要。PLL未解決0、COPY2・一時再生3の拒否はS5へ残す。
- 展開972 node（上限2048）。S3送りの読取り前検査・静的/実行時READ課金・4件の実行器変異と順序変異を試験へ反映。
- 設計者判断は [ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) のS4節。同値時も有限列を実行し、PLL一時captureは中断後に再生しない。
- 部品増分はtext/rodata約7.33KiB（PLL単体3.37KiB）、recover BSS 116 byte、decoder所有領域580 byte。kdebug結線はS6。
- 検証証拠: `~/os32-tmp/run/gf4a_s4/`。実accessor・実機・NP21/W・S6の時計/poll設定は対象外。

### 4a S5 bus・共有窓 (2026-10-21)
- T2/peekの窓を共有し、READはRWなし・WRITEだけRW、PTE復元失敗はboot寿命で停止。peekの幅/許可表/2ページ跨ぎ/LAN契約を維持。
- GF accessorは生PCI ID・Mem・BARを照合、Command16とBAR probe、PRMVIO/PRMCIOのhead別名をdescriptor経由で扱う。PCI imports・入口・shellはS6以降。
- COPY2・一時再生3行を結線、局所CR11/CR1F/CR44→元index復元を追加。COLD DATAは終端再生しない。N1の4対象guardは維持。
- S4送り3件を修正。設計者判断は [REALHW_RECOVER_ADDENDUM.md](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) の「4a S5 の実装で設計者が決めたこと」。
- 証拠: `~/os32-tmp/run/gf4a_s5/` の最終build・候補・変異・錨・lint・docsログ。実機/NP21/W未実施、version.chip・T3送り・S6時計/結線は未確認のまま。

### 4a S6 (2026-10-21)
- 実装: checked REC・試行op・session/phase・cleanup・RUN_ONE・A4、private imports末尾追加。決定は[追記](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md#4a-s6-の実装で設計者が決めたこと-2026-10-21)。
- S5のCR1F読書値・cleanup順・負のbus試験、S3の60秒/poll境界を反映。
- 検証証拠: `/home/hight/os32-tmp/run/gf4a_s6/`（最終結果は実装報告）。
- 未確認: NP21/W・実機は未実施、S7の公開入口とS8以降の承認・立会いを維持。
- A1前提: 他装置の有効memory BAR/ROM長が不明なら書込み前に拒否、実機の長さ根拠をPMが揃える。
