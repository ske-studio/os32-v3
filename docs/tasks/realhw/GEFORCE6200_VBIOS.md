# GeForce 6200 (NV44A) の VBIOS — 分かっていること

> 状態: **現行 (2026-10-21)** — 実機 Ra266 の GeForce 6200 の VBIOS について、段 0〜3 の観測と 4a S1 (fixture・ROM 照合) までで分かったことのまとめ。段 4a 以降で分かったら書き足す。

この文書は「VBIOS に何が書いてあり、それが何を意味するか」の 1 枚のまとめです。段の計画・承認・実装の進み具合は
[TASK_GEFORCE6200](TASK_GEFORCE6200.md) (票) が正典で、ここは要約と、票に散らばった事実の整理だけを持ちます。

**確かさの印** (表の「確度」の列と本文の角括弧):

| 印 | 意味 |
|---|---|
| **確** | 確認済み — 実機で読んだ値、または吸い出した ROM そのものと照合した (nvbios を ROM に当てた出力・CRC の一致を含む) |
| **資** | 資料 — nouveau (MIT、仕様として信頼するとユーザー決定 2026-10-20)・envytools・xf86-video-nv の記述。実機ではまだ確かめていない |
| **推** | 推測 — 上のどちらでもない。設計者・PM の読み |

**扱いの約束** ([HW3]、票 §0 と段 2b のユーザー決定 2026-10-21):

- VBIOS の **x86 コードは逆アセンブルしていない** し、その内容はどこにも無い。分かっているのは ROM の**データの表** (BIT・init script・条件表・PLL の表・DCB など) だけ。x86 コードの解析は最終手段で、要るときはユーザーが判断する。
- ROM の実物とバイト列はリポジトリに置かない (ノートの証拠ディレクトリだけ)。この文書もバイト列は載せず、命令は「どの script が何をするか」の水準で書く。命令の全件は [`tools/tests/gfinit_fixture.h`](../../../tools/tests/gfinit_fixture.h) にある。

---

## 1. カードと VBIOS の素性

### 1-1. カード

| 項目 | 値 | 確度 | 出所 |
|---|---|---|---|
| 位置 | PCI 0:13.0、class 03.00、rev a1、IRQ 6 | 確 | 段 0 (`pcidump`・`lspci`) |
| ID | `10de:0221` (NVIDIA NV44A、GeForce 6200)、subsys `10fc:1270` | 確 | 段 0 |
| チップの種類 | NV44A = NV40 系の AGP 版 NV44 (2005-04)。本機には AGP cap が無く、PCI 版のカード | 資 + 確 | envytools `gpu.rst` (0x4a NV44A AGP)・`pciid.rst`、cap 60h が PM だけ (段 0) |
| PMC_BOOT_0 | **0x04A100A1** (bits 20-27 = 0x4A、bits 16-19 = 1) | 確 | 段 1 |
| BAR | BAR0 0x24000000 (MMIO 16MB)、BAR1 0x30000008 (VRAM 窓、prefetch)、BAR2 0x25000000 (RAMIN 窓)、ROM BAR 0x26000000 (無効) | 確 | 段 0。大きさは未測定 (段 4a の A1) |
| Command | 0x0106 (Mem+・I/O−・BusMaster+) | 確 | 段 0 |
| cfg 50h / 54h | 50h=0 (ROM shadow 無効 = PROM 窓で EEPROM が読める)・**54h=1** (VGA レガシー窓のデコード有効) | 確 | 段 0 |
| 冷えた状態 | PFB 4KB は全部 0。ただし PMC_ENABLE=0x03010000、PCRTC・PRAMDAC に 0 でない語がある | 確 | 段 1。0 でない理由は不明 (電源投入時の既定値か — 推) |
| nouveau での扱い | `nv4a_chipset`: devinit は `nv1a`、fb は `nv44`、clk は `nv40` | 資 | nouveau `engine/device/base.c` の nv4a_chipset |

### 1-2. VBIOS

| 項目 | 値 | 確度 |
|---|---|---|
| 版 | **5.44.a2.10.00** (08/29/05)。文字列は "GeForce 6200 VGA BIOS IDG1"・"Version 5.44.A2.10.00"・"nv44 Board - p362h0" | 確 |
| 大きさ | **63,488 B** (先頭 55 AA、長さバイト 0x7C × 512)、8 ビットの和が 0 | 確 |
| 吸い出し方 | PROM 窓 **BAR0+0x300000** をバイトで 128KB 読む (cfg 50h=0 なので書き込み無しで開いている)。2 回読んで一致 (CRC F5ADEE03)。後半 64KB は前半の写し | 確 (段 2) |
| ROM の指紋 | sha256 ec460969…dedd (全体はノートの証拠に) | 確 |
| ROM BAR 経由の読み | 使っていない。cfg 30h の enable を書く (= 書き込み) ので段 4 の予備 | — |
| 使用許諾の文字列 | "Copyright (C) 1996-2004 NVIDIA Corp."・"NVIDIA"・"NVIDIA Corporation" だけ。license・reverse・disassembl を含む行は無い → 段 3 (データの表の解析) に進むとユーザー決定 (2026-10-21) | 確 (段 2b) |
| ROM の実物の置き場 | **ノートの証拠ディレクトリだけ** (`~/os32-tmp/evidence/2026-10-21/gf6200_s012/`)。リポジトリ・WSL 側には置かない | — |

PROM 窓が読める理由: envytools `bus/pci.rst` によると cfg 50h=0 の間は「ROM shadow 無効・映像出力無効・PCI ROM は EEPROM」で、
1 にすると VRAM 上の写しに切り替わり PROM の読み書き回路が止まる [資]。段 4b で 50h (PBUS 1850h) を 1 にした後は PROM 窓は読めなくなる。

---

## 2. 表の地図

nvbios (envytools f102b823、MIT、ホストで実行) を ROM に当てた出力 (ノートの `s3_nvbios.txt`、823 行) から。表の位置は ROM 内のオフセット。
**位置と中身は ROM から読んだもの [確]、用途は nouveau の記述 [資]**。

| 表 | 位置 | 中身の要約 | 何に使うか |
|---|---|---|---|
| BIT (見出し表) | 0x0218、版 1.0 | 見出し 2・B v2・C・D・I・L・t・M・P・S v2・T・U・V・i v2 | 他の表への入口。NV40 系は BMP/PINS ではなく BIT 形式 |
| init script 表 (BIT `I` から) | 0x5d4c | main 5 本 (2 B × 5) | 冷えたカードを起こす手順。§3 |
| macro 表 | 0x5d66 | 4 件 (8 B = {番地, 値}) | MACRO 命令の書き先。4 件とも PFB の 1 語に 1 を書く (§3-3) |
| condition 表 | 0x5d86 | 3 件 (12 B = {番地, mask, 値}) | CONDITION 命令の判定 |
| IO flag condition 表 | 0x5daa | 8 件 (9 B) | IO_FLAG_CONDITION の判定。CR3C の RAM の組で ROM の 1 バイト表 (0x5c59 から 8 B) を引く |
| PLL limits | 0x5bc8、版 0x21 | 4 件。ref_clk=0 (= 水晶の値を使う) | PLL の VCO・N/M の上下限。§6 |
| strap の上書き | — | select ffef81c3 / value 00001e0c | PSTRAPS の実値と一致 [確] |
| DCB | 0x96ac、版 3.0 | ANALOG OR0・ANALOG OR1・TMDS OR0 (DVI-I)・TV の 4 出力 | 出力の一覧。§7 |
| I2C / GPIO | 0x9715 / 0x974e | 中身は未要約 | DDC・GPIO の配線 |
| CONN | 0x97da | VGA・DVI_I・COMPOSITE・S_VIDEO・RGB | コネクタの種類。§7 |
| TMDS | 0x95ea | 中身は未要約 | DVI 出力の設定 |
| perf | 0xdcef | 1 段: Core 300MHz・Memory 200MHz | 動作クロック (OS32 は使う予定なし) |
| memory timings | 0xddd1 | 全部 0 | 使われていない (PFB の値は init script の表が持つ) |

- 「未要約」の表は nvbios の出力にはあるが、票にも設計にも写していない。4c (画面モード・DDC) で要るときにノートの出力から読む。
- BIT の `I` の後半にある「追加の unknown script」の欄は、非零なら 5 本だけで済ませないと設計で決めた (DESIGN.final §4)。この ROM で非零かどうかは S2 の decode で確かめる (未確認)。

---

## 3. 初期化 script 9 本

### 3-1. 一覧

main 0〜4 の 5 本と、そこから呼ばれる SUB 6・7・8 と CALL 先 1 本で **9 本・193 命令・27 種**。
fixture では CALL 先を script 番号 255 として扱う。命令数は圧縮表記の NV_REG 3 行を各 4 命令に数えた値。

| 番号 | 開始 | 長さ (B) | 命令 | 呼ばれ方 | 役割 (要約) |
|---|---|---:|---:|---|---|
| 0 | 0xdeb0 | 741 | 58 | main (1 番目) | 画面を消す、エンジンの有効化 (RESET)、strap の書換え、クロックの制御、RAM の組を CR3C に決める、コア・メモリの PLL (SUB 7・6)、PFB のタイミングの組 |
| 1 | 0xe195 | 1,166 | 53 | main (2 番目) | メモリ制御 (PFB) の組ごとの設定、PBUS 側の補助、precharge・MRS 類 (MACRO)、待ち |
| 2 | 0xe623 | 261 | 32 | main (3 番目) | 映像の PLL (36MHz)、両 head の CRTC 拡張レジスタの既定値、PRAMDAC の既定値、自動リフレッシュの開始 |
| 3 | 0xe780 | 178 | 18 | main (4 番目) | DRAM の起動手順 (precharge → refresh × 10 → SUB 8)、PRAMIN の 1 語、容量の写し、**head0 の画面オフ解除 (E81E)** |
| 255 | 0xd336 | 58 | 5 | main 3 の CALL | PBUS 1300h・1400h 台の設定 (1 語ずつと 4 語の連続書込み) |
| 4 | 0xe832 | 1 | 1 | main (5 番目) | DONE だけ (何もしない) |
| 7 | 0xe754 | 44 | 2 | main 0 の SUB | メモリ PLL (4024h) を **400MHz** に (RAM の組で選ぶ形だが 8 択とも同じ値) |
| 6 | 0xe728 | 44 | 2 | main 0 の SUB | コア PLL (4004h) を **200MHz** に (同上) |
| 8 | 0x5df2 | 178 | 22 | main 3 の SUB | head0 を選び、IO flag の条件 3 つで PFB 1002C0h〜1002CCh のビットを立てて落とす (パルス) |

- 開始・長さ・CRC32 は fixture の `gf_scripts`、確度は **確** (ROM の実物と 9 本とも一致、§8)。
- 役割の欄はレジスタ群からの読みで、各レジスタの意味は §5 の確度に従う。

### 3-2. 呼ばれる順と main・SUB の関係

| 順 | 実行される物 | 中で呼ぶ物 |
|---|---|---|
| 1 | main 0 | 末尾近くで SUB 7 (メモリ PLL) → SUB 6 (コア PLL) |
| 2 | main 1 | なし |
| 3 | main 2 | なし |
| 4 | main 3 | 先頭で CALL 0xd336 → 途中で SUB 8 |
| 5 | main 4 | なし (DONE のみ) |

- main は表の順 (0→4) に、**それぞれ execute=1・head0 から始める** [資: nouveau `init.c` の init 表の実行]。SUB・CALL は呼出し元と条件の状態 (skip) と head を共有し、DONE で呼出し元に戻る。
- OS32 の 4a では main を 1 本ずつ別コマンド・別承認で実行する (U2 のユーザー決定)。SUB・CALL・REPEAT は呼出し元の main の承認に含める。main 4 は DONE だけと静的に確かめ、書込みの承認を追加しない。
- REPEAT は main 3 に 1 個 (回数 10)、中身は MACRO 1 個。動的に数えると 211 命令になる (静的には 193)。

### 3-3. 条件と macro の中身

| 表 | 件 | 判定・書き先 | この実機の冷えた値での結果 |
|---|---|---|---|
| condition | [0] | PSTRAPS 101000h & 0x40 == 0x40 | 偽 (101000h=0x23409E8F の bit6=0) [確] |
| condition | [1] | 1310h & 0x100 == 0 | 使われていない (9 本のどれも条件 1 を参照しない) [確] |
| condition | [2] | PSTRAPS 101000h & 0x20 == 0x20 | 偽 (bit5=0) [確]。main 0 は 101000h の bit31 を立て bits 10-11 を落とすだけで bit5・6 は変えない |
| IO flag | [0]〜[7] | ROM の 1 バイト表 (0x5c59 + RAM の組) を mask して比較 | 表の 8 バイトはどれも 01 なので、組に依らず結果は同じ [確] |
| macro | [0] | 1002D4h ← 1 | nouveau の注記で **precharge** [資] |
| macro | [1] | 10021Ch ← 1 | 意味は資料に無い (MRS 類か — 推) |
| macro | [2] | 1002D0h ← 1 | nouveau の注記で **refresh** [資]。main 3 で 10 回繰り返す |
| macro | [3] | 1002D8h ← 1 | 意味は資料に無い (EMRS 類か — 推) |

### 3-4. head (CR44) の扱い

- NV40 系は CRTC が 2 つ (head0・head1) あり、VGA 互換レジスタも head ごとに別の MMIO 別名を持つ (PRMVIO・PRMCIO・PCRTC・PRAMDAC は head ごとに +2000h) [資: envytools `mmio.rst`]。
- script は **CR44 に書いた値で以後の head を切り替える**。CR44 への書込みは常に head0 に行き、値が 3 なら以後は head1、それ以外なら head0 [資: nouveau `init.c` の CR44 の処理]。
- 実物での使われ方 [確]: main 0 は CR44=3 で head1 の SR01 bit5 を立て、CR44=0 で head0 の SR01 bit5 を立てる (両 head とも画面を消す)。main 2 は head0 → CR44=3 で head1 → CR44=0 で head0 に戻して同じ CRTC 拡張レジスタ群を設定する。SUB 8 は先頭で head0 を選び直す。main 3 の E81E は head0 の SR01 bit5 だけを落とす (head1 は消したまま)。
- head0 と head1 の別名が 54h=0 (レガシーデコード無効) のままで応答するかは**実機では未確認** — 4a の A2 で確かめる。

---

## 4. 27 種の命令

名前は nouveau `nvkm/subdev/bios/init.c` に合わせた。長さはバイト (opcode を含む)、n は可変個数。件数は静的な数 (REPEAT の繰返しは数えない)。
長さと意味は 4a S2 の配置表 (`~/os32-tmp/run/gf6200_4a_s2/S2_LAYOUT.final.md`) で nouveau の各 handler と全部照合済み [資]、件数は ROM の実物から [確]。

| op | 名前 | 長さ | 件数 | 一言の意味 |
|---|---|---|---:|---|
| 32 | IO_RESTRICT_PROG | 11+4n | 32 | VGA の索引レジスタ (この ROM ではすべて CR3C) の値で n 個の 32 ビット値から 1 つを選び、MMIO に書く。RAM の組ごとの PFB 値はこれ |
| 33 | REPEAT | 2 | 1 | 次の END_REPEAT までを n 回繰り返す (実物は 10)。0 回は OS32 では静的に拒否 |
| 36 | END_REPEAT | 1 | 1 | 繰返しの終わり |
| 37 | COPY | 11 | 1 | MMIO の値をシフト・mask して VGA の索引レジスタへ写す。実物は strap → CR3C (RAM の組) |
| 38 | NOT | 1 | 2 | 条件の skip を反転 |
| 39 | IO_FLAG_CONDITION | 2 | 4 | IO flag 表で判定、偽なら以後を skip |
| 4A | IO_RESTRICT_PLL2 | 11+4n | 2 | 32 と同じ選び方で周波数 (kHz) を選び、PLL を設定する |
| 4B | PLL2 | 9 | 1 | 指定の PLL を指定の周波数 (kHz) に設定する |
| 51 | CR_INDEX_ADDRESS_LATCHED | 5+n | 2 | 2 つの CR を「番地の CR・データの CR」として使い、連続した n 個を書く。終わりに番地の CR を元に戻す |
| 52 | CR | 4 | 2 | CR ← (旧値 & mask) OR 値 |
| 53 | ZM_CR | 3 | 9 | CR に直接書く (CR44 なら head の切替を伴う) |
| 54 | ZM_CR_GROUP | 2+2n | 3 | {索引, 値} の組で CR に直接書く |
| 58 | ZM_REG_SEQUENCE | 6+4n | 3 | 先頭番地から 4 ずつ進めて 32 ビット値を直接書く (実物 3・4・4 語) |
| 5B | CALL (SUB_DIRECT) | 3 | 1 | ROM 内の絶対番地を呼ぶ (実物は 0xd336) |
| 5F | COPY_NV_REG | 22 | 1 | MMIO → MMIO の写し (シフト・mask・xor 付き)。実物は 10020Ch を読んで 1700h に書く |
| 62 | ZM_INDEX_IO | 5 | 2 | VGA の索引レジスタに直接書く |
| 63 | COMPUTE_MEM | 1 | 1 | メモリ量の計算。**NV44A では何もしない** (下の注) |
| 65 | RESET | 13 | 1 | PCI の 184Ch の一部を退避して落とし、指定レジスタ (実物は PMC 200h) に値 1 → 10µs → 値 2、184Ch を戻し、1850h bit0 を落とす |
| 6B | SUB | 2 | 3 | script 表の番号で別の script を呼ぶ |
| 6E | NV_REG | 13 | 36 | MMIO ← (旧値 & mask) OR 値 |
| 6F | MACRO | 2 | 5 | macro 表の 1 組 {番地, 値} を書く |
| 71 | DONE | 1 | 9 | script の終わり (SUB なら呼出し元へ) |
| 72 | RESUME | 1 | 6 | 条件の skip を解除 |
| 74 | TIME | 3 | 13 | µs 単位で待つ |
| 75 | CONDITION | 2 | 2 | condition 表で判定、偽なら以後を skip |
| 78 | INDEX_IO | 6 | 8 | VGA の索引レジスタ ← (旧値 & mask) OR 値 |
| 7A | ZM_REG | 9 | 42 | MMIO に直接書く。宛先が PMC 200h のときだけ bit0 を必ず立てる |

注:

- **COMPUTE_MEM (63h) は NV44A では no-op** [資]。nouveau は NV44A に `nv1a_devinit` を使い、これには meminit が無く、COMPUTE_MEM は何もしない。票の段 3 の記録にあった「量は COMPUTE_MEM が実行時に測る」は誤りで、DESIGN.final §1 の D2 で直した。容量は §6 の別の方法で確かめる。
- RESET と COMPUTE_MEM は skip 中でも実行される (force) [資]。skip 中は他のハードの読み書きと TIME は行わない。
- 実物に無い命令 (34・56・69・76・79、4C〜50・5E・8E・5C・77・73・6D・47・48、I2C・GPIO 系など) は OS32 では静的に拒否する。
- 32 と 4A は 34 個すべてが「索引 CR3C、mask 0x70、shift 4、8 択」で、RAM の組 (0〜7) で選ぶ形 [確]。4A の 2 個は 8 択とも同じ周波数。

---

## 5. 書き込みの中身

### 5-1. 619 の書込み候補の分類

fixture は 193 命令が起こしうる操作を 1 件ずつ並べ (索引レジスタの選択の書込みを含む)、**619 件の書込み候補**にした。
これを戻す処理 (recover) の許可表 (`kdebug/recover_gf.c`、target 145・rule 544) に照らした分類 (4a S1 追補、ADDENDUM.final の末尾):

| 分類 | 件数 | 中身 | 今の扱い |
|---|---:|---|---|
| 許可 (rule あり) | **513** | 定数・RMW などの書込み 257 + RAM の組で選ぶ書込み 256 (32 の 32 個 × 8 択) | rule 513 件。許可表にはこれに固定・復元の 31 件を足して 544 件 |
| PLL の内部手順待ち | **101** | 4A・4B の 3 個から展開される PLL の制御・係数・補助レジスタの書込み | rule 無し・拒否。S4 で手順を決めてから |
| COPY の式待ち | **2** | DFAF の COPY (strap → CR3C) と E7FB の COPY_NV_REG (10020Ch → 1700h) | rule 無し・拒否。S2 で式を登録してから |
| 一時値の再生待ち | **3** | RESET の 184Ch の戻し、CR57 の戻し (head0・head1 で 1 件ずつ) | rule 無し・拒否。同じ命令の中で読んだ値を戻す仕組みが S2/S5 で要る |
| 合計 | 619 | — | 残る 106 件が拒否のまま。解消まで全 ROM の実行は許可しない |

- 一度「VGA 別名の検査の欠け 1」(602858h、PCRTC head1 の通常レジスタが別名と誤認される) があったが、S1 追補で検査を実ポートに限って解消し 0 件になった。
- fixture の書込み候補は**証拠であって許可表ではない**。すべて DENY_W のまま置き、許可は recover の許可表だけが持つ。

### 5-2. レジスタ群ごとに何をしているか

番地は BAR0 からのオフセット。ブロック名は envytools `mmio.rst` の NV40 系の地図 [資]。「何をしているか」の欄は特記の無い限り **推** (レジスタの名前と nouveau の近い処理からの読み)。

| ブロック (番地) | 主に書く script | 何をしているか |
|---|---|---|
| PMC (0h〜) | 0 | 200h (ENABLE) を RESET で 2 段に書き、後で bit を足す = エンジンの有効化 [資: RESET の動き]。140h (割込み許可) ← 0 |
| PBUS (1000h〜1FFFh) | 0・1・2・3・CALL 先 | 1080h〜108Ch・1200h〜124Ch・1300h〜140Ch・1500h〜15B8h などの補助設定。184Ch・1850h は RESET の中だけ (§4)。**1854h に書く命令は無い** [確] |
| PCLOCK (4000h〜) | 0・6・7 | コア PLL (4000h/4004h)、メモリ PLL (4020h/4024h)、4008h・4028h・402Ch の制御。402Ch ← C1C11002h は生の値で周波数ではない [資: nouveau `clk/nv40.c`] |
| PCONTROL (C000h〜) | 0 | C000h・C020h・C040h・C044h。C040h はクロック源の選択 [資: nouveau `clk/nv40.c`] |
| PFB (100000h〜) | 0・1・3・8 | メモリ制御のすべて。タイミング (100220h〜100238h) を RAM の組で選ぶ、100200h〜10021Ch・100350h・1003xxh・1004xxh の設定、100210h (REFCTRL) で自動リフレッシュを止める・始める、1002C0h〜1002DCh の起動手順 [資: envytools `nv10_pfb.xml`・nouveau `ramnv40.c`]。**10020Ch (MEM_AMOUNT) に書く命令は無い** [確] |
| PSTRAPS (101000h〜) | 0 | 101000h の bit31 を立て bits 10-11 を落とす、10100Ch。strap を書き換えるので冷えた値は戻らない |
| PCRTC (600000h〜、head1 は +2000h) | 0 | 600818h・60081Ch・600858h・602858h |
| PRAMDAC (680000h〜、head1 は +2000h) | 0・2 | 680508h (映像 PLL、head0) を 36MHz に、680510h・680580h・680608h/682608h・680610h・68052Ch/68252Ch・680848h/682848h・680880h |
| PRAMIN (700000h〜) | 3 | 70FFF0h ← 0 (VRAM の末尾近くの 1 語) |
| 11014h | 3 | bit15 を立てる。この番地が何のブロックかは手元の地図に無い (不明) |
| VGA 別名: CR (PRMCIO 601000h + head) | 0・2・3・8 | CR3C (RAM の組)、CR44 (head 切替)、CR1A/21/28/2B/2C/33/38/3A/3B/43/4A〜4C/52/53/57/58/85/86 などの拡張 CR の既定値 |
| VGA 別名: SR (PRMVIO C0000h + head) | 0・3 | SR01 bit5 (画面オフ) を両 head で立て、main 3 の E81E で head0 だけ落とす |

- 4a では I/O 空間の 3xxh を使わず、VGA 互換レジスタはすべて BAR0 の MMIO 別名で触る (DESIGN.final §5)。GPU の I/O デコードは常に 0。
- strap・PLL・PFB・PRAMIN は **COLD** (戻せない) に分類した。中断しても保存値を書き戻さず、電源の入れ直しで冷えた状態に戻す。

### 5-3. N1 — main 3 の E81E (SR01 bit5 の解除)

- 中身 [確]: head0 の SR01 の bit5 を落とす 1 命令 (INDEX_IO)。VGA の SR01 bit5 は Screen Off なので、これは「head0 の画面オフの解除」= 表示を有効にする系の書込み。
- 表示を有効にする書込みは 4a では原則拒否だが、**この 1 件だけをユーザー決定 (2026-10-21) で許した**。条件は cfg 50h=0・54h=0 (PBUS 1850h・1854h=0) を実行前後に読み戻し、保持していること。
- 安全の根拠: cfg 50h=0 の間は GeForce の映像出力が止まっている [資: envytools `bus/pci.rst`]。かつ 4a の間は 98 の切替回路 0FACh が 98 表示のまま (RGB-IN を選ばない) [確: 4a は 0FACh を読み書きしない設計]。
- fixture では `GF_N1_MAIN 3`・`GF_N1_PC 0xe81e` の旗。他の表示有効化は静的に拒否する。

---

## 6. メモリと PLL

### 6-1. 水晶

| 項目 | 値 | 確度 |
|---|---|---|
| 判定の式 | PSTRAPS 101000h & 0x00400040: 0 → 13.5MHz、0x40 → 14.318MHz、0x00400000 → 27MHz、両方 → 25MHz | 資 (nouveau `engine/device/base.c`) |
| この実機 | 101000h=0x23409E8F → bit22=1・bit6=0 → **27MHz** | 確 (値) + 資 (式) |
| PLL の表の ref_clk | 0 = 水晶の値を使う → 27,000kHz | 確 (表) + 資 (`bios/pll.c`) |

### 6-2. RAM の組

- **RAM の組は 3** [確]。決まり方: main 0 の CONDITION 0 (偽) で CR3C←0、続く COPY で CR3C ← (CR3C & 0x0F) OR ((101000h << 2) & 0xF0)。101000h の bits 2-5 = 0011b なので CR3C=0x3x、以後の IO_RESTRICT は (CR3C & 0x70) >> 4 = 3 を選ぶ。
- 冷えた CR3C は 0。だから A2 (実行前) は PSTRAPS から組を導いて 3 を確かめ、CR3C=3xh の確認は main 0 の COPY の直後に行う (DESIGN.final §5)。
- 組 3 の主な PFB の値 (票 §4 と同じ) [確 (ROM の表の値)]:

| レジスタ | 組 3 の値 | 名前 (envytools) |
|---|---|---|
| 100220h | 03080E0B | MEM_TIMINGS_0 (RC・RFC・tRAS…) |
| 100224h | 05010405 | MEM_TIMINGS の続き |
| 100228h | 20220303 | 同上 |
| 10022Ch | 41088102 | 同上 |
| 100234h | 00266622 | 同上 |

- 残りの PFB の値 (100230h・100238h・1002C0h・1002C8h・1004xxh など) は fixture の全表にある。4a の合否では上の 5 つを必ず比べ、後から上書きがあるものは「その命令の直後」と「最終値」を分けて比べる。
- メモリの種類 [推]: perf 表の Memory 200MHz とメモリ PLL 400MHz から DDR 系と読んでいる。確かめていない。

### 6-3. PLL

| PLL | レジスタ | 要求 | 呼ぶ所 | 確度 |
|---|---|---|---|---|
| コア (NVPLL) | 4000h 制御 / 4004h 係数 | 200,000kHz | SUB 6 (IO_RESTRICT_PLL2、8 択同値) | 確 (要求) + 資 (役割: nouveau `clk/nv40.c`) |
| メモリ (MPLL) | 4020h 制御 / 4024h 係数 | 400,000kHz | SUB 7 (同上) | 同上 |
| 映像 head0 (VPLL1) | 680508h (680578h・680580h・1584h・C040h と組) | 36,000kHz | main 2 先頭の PLL2 | 同上 (nouveau `devinit/nv04.c`) |

- 式 [資]: `f ≈ 27000 × N1 × N2 / (M1 × M2 × 2^P)` kHz (単段なら N2=M2=1)。係数の探し方 (順・丸め・単段/二段・NV4x の段間の制約) は nouveau `clk/pllnv04.c` に従う。OS32 は整数演算だけで、二段の積の上限 27000 × 255 × 255 = 1,755,675,000 が u32 に収まることを静的に確かめる設計。
- limits の在りか: PLL limits 表 0x5bc8 (版 0x21、4 件)。VCO1/2 の上下限・入力周波数・N/M の範囲・max_p などを nouveau `bios/pll.c` の読み方で読む。4004h・4024h の limits は reg−4 (4000h・4020h) で引く [資]。**4 件が具体的にどの PLL か・1 件の長さは要約していない** (fixture でも「長さ不明」の印)。
- 要求のメモリ 400MHz と perf 表の Memory 200MHz は別の符号化の値で、片方を誤記として半分にはしない (設計の決定)。コア 200MHz と perf の Core 300MHz の違いの理由は不明 [推: 起動中は低いクロックで回す]。
- PLL の手順 (4000h/4004h・4020h/4024h・4038h/403Ch・4600h・C040h の書く順と退避・復元) は S4 の作業で、まだ決まっていない。

### 6-4. 容量 (10020Ch)

| 事実 | 確度 |
|---|---|
| 10020Ch は PFB の MEM_AMOUNT。nouveau `fb/ramnv44.c` は bits 31-24 を容量とする (xf86-video-nv は bits 31-20) | 資 |
| **9 本のどの script も 10020Ch に書かない**。main 3 の COPY_NV_REG が読んで 1700h に写すだけ | 確 |
| 冷えた状態では 10020Ch を含む PFB 4KB が全部 0 | 確 |
| COMPUTE_MEM は NV44A では何もしない (§4) | 資 |
| したがって script を全部走らせても 10020Ch が意味のある値になる保証は無い | 推 |

- 4a の扱い (N2 の PM 決定): 10020Ch が 0 や不合理なら「容量不明」と記録し、容量の照合 (2 冪境界での別名検出) だけを省く。BAR1 の先頭 1KiB での最小の読み書き試験は 10020Ch に依らず行う。BAR1 の窓の大きさを実装 VRAM 量とは呼ばない。

---

## 7. 映像の出力とつながり方

### 7-1. VBIOS が知っている出力

| 表 | 中身 | 確度 |
|---|---|---|
| DCB 3.0 | ANALOG (OR0)・ANALOG (OR1)・TMDS (OR0、DVI-I のデジタル側)・TV | 確 (ROM の表) |
| CONN | VGA・DVI_I・COMPOSITE・S_VIDEO・RGB | 確 (ROM の表) |

- カードの物理的なコネクタとの対応 (どの ANALOG がどの口か)、DDC の I2C の割当ては要約していない (未)。
- 98 側から見て、このカードの映像は「抜いちゃダメボード」経由で 98 の切替回路の **RGB-IN** に来ている (ユーザー 2026-10-21) [確 (ユーザーの構成の説明)]。どの DCB 出力が RGB-IN につながっているかは未確認 (ANALOG のどちらか — 推)。

### 7-2. 98 の切替回路 (0FACh)

| 項目 | 内容 | 確度 |
|---|---|---|
| 選ぶ物 | 98 表示・内蔵 (Trident)・PCI ボードの RGB-IN の 3 つから 1 つ | 確 (ユーザーの説明) |
| 選び方 | 0FACh の bit0 (RGB-IN のスルー / 内蔵) と bit1 (内蔵 / 98) | 推 (UNDOCUMENTED の記述、[TASK_TRIDENT_DRIVER §3-7](TASK_TRIDENT_DRIVER.md)) |
| 98 の画面に戻す | 0FACh を読み、bit1..0 が 00b なら書かない、違えば 00h を書いて読み戻す。最終判定はユーザーの目視 | 設計 (recover の確定設計) |
| 4a での扱い | 0FACh を読みも書きもしない | 設計 |
| 選び違い | 黒画面として見える | 資 (Trident 票 §3-7) |

### 7-3. cfg 50h/54h と PBUS 1850h/1854h

| cfg | PBUS の鏡 | 意味 [資: envytools `bus/pci.rst`] | 実機の値 | OS32 の扱い |
|---|---|---|---|---|
| 50h | 1850h | 0: ROM shadow 無効・**映像出力無効**・PCI ROM は EEPROM。1: shadow 有効・映像出力有効・PROM の回路停止 | 0 [確] | 4a の間は 0 のまま。1 にするのは 4b (承認地点 B) |
| 54h | 1854h | VGA レガシーの I/O・メモリ窓のデコード (1 で有効) | **1** [確] | 4a の A2 で最初に 1854h ← 0 (計画 G1)。以後 1 に戻さない |

- PCI の config 空間は MMIO 1800h〜18FFh にも写っていて、cfg 50h などは config 書込みでは届かないことがあるので **PBUS の鏡から書く** [資]。
- 54h=1 のまま映像出力を有効にすると、C バスブリッジ (subtractive デコード) の下でカードが A0000h〜BFFFFh を取り、98 のテキスト VRAM や planar A8000h への書込みを奪う恐れ [推 (計画の G1)]。だから 4b の前に必ず 54h=0、4b の合否に「A0000h のテキストが表示されたまま」を入れた。
- RESET 命令は 1850h の bit0 を落とす (出力を止める側) [資]。script の中で 1850h を 1 にする・1854h を非零にする命令は無い [確]。

---

## 8. 確かめた方法と証拠

### 8-1. 段ごとの記録

| 段 | 日付 | 何をしたか | 書込み | 証拠の所在 (ノート) |
|---|---|---|---|---|
| 0 | 2026-10-21 | PCI と BAR、C バスブリッジの窓、他装置との重なり | なし | `~/os32-tmp/evidence/2026-10-21/gf6200_s012/` |
| 1 | 2026-10-21 | PMC・PSTRAPS・PFB・PCRTC・PRAMDAC・VGA 別名 (8 ポートのバイト読み) の冷えた値。固まらず DENY 0、1 回 約 3.3 秒 | なし | 同上 |
| 2 | 2026-10-21 | PROM 窓の 128KB を 2 回読んで一致、ヘッダ・和・sha256 | なし | 同上 |
| 2b | 2026-10-21 | 著作権・使用許諾の文字列を抜き出してユーザーに見せた → 段 3 へ | — | 同上 |
| 3 | 2026-10-21 | ノートで nvbios (envytools f102b823) を ROM に当てた。逆アセンブルなし | — | `~/os32-tmp/evidence/2026-10-21/gf6200_s3/` (`s3_nvbios.txt` 823 行) |
| 4a S1 | 2026-10-21 | nvbios の init script の部分 (ノートが貼り返した写し) から fixture を作り、ROM の実物と照合 | — | `~/os32-tmp/evidence/2026-10-21/gf6200_romcheck/` |

道具は OS32 の KPEEK (kdebug の読み取り専用 peek、MMIO の許可リスト付き)、結果はシリアル (LAN) で持ち帰った。実機は Ra266 1f1d115・KDEBUG=1。

### 8-2. ROM と fixture の一致

- 写し: `~/os32-tmp/run/gf6200_4a/nvbios/init_scripts.txt` (WSL 側、211 行) — nvbios の出力の init script の部分。一部の行は圧縮表記なので fixture のバイト列は写しから復元した値。VBIOS 由来なので公開リポジトリには置かない (PM 判断 2026-10-21)。
- fixture: [`tools/tests/gfinit_fixture.h`](../../../tools/tests/gfinit_fixture.h) — 9 本の開始・長さ・CRC32 (`gf_scripts`)、193 命令 (`gf_instructions`)、619 の書込み候補 (`gf_effects`)、表の位置と大きさ (`gf_table_spans`)、main の順。ROM のバイトは init script のデータの範囲だけで、x86 コードは含まない。
- 照合: [`tools/gfinit_rom_check.py`](../../../tools/gfinit_rom_check.py) をノートで ROM の実物に当て、**9 本の範囲の CRC32 がすべて一致** (rc=0、605ae6a、2026-10-21)。ROM を出力・保存せず、9 つの範囲だけを比べる。`--list` で期待値 (開始・長さ・CRC) を ROM 無しで表示できる。
- 写しと fixture の一致を見る試験は `make check-gfinit-fixture-host` ([`tools/tests/test_gfinit_fixture.py`](../../../tools/tests/test_gfinit_fixture.py))。これは CI で回らない PM のローカル検査 ([DEFERRED_TESTS](../DEFERRED_TESTS.md) の GF4A-S1-FIXTURE)。CI では `check-recover-gf` が fixture と許可表の照合だけを見る。

### 8-3. 使った資料

| 資料 | 版 | ライセンス | 使い方 |
|---|---|---|---|
| envytools (nvbios・docs/hw・rnndb) | f102b82 | MIT | nvbios をホスト (ノート) で自前の ROM に当てた。レジスタの地図と意味 |
| nouveau (devinit・bios・fb・clk・dispnv04) | c03c1a1 | MIT (取得範囲に GPL 無し) | 命令の意味・PLL・水晶の判定。仕様として信頼する (ユーザー決定 2026-10-20) |
| xf86-video-nv | 8234bce | MIT | 容量・水晶の別の読み方の照合 |
| Haiku nvidia ドライバ | af754c7c | 要確認 (ファイルに license の明記なし) | 挙動の照合だけ。構造・コードは使わない |

一式の所在と見る所は `~/os32-tmp/refs/geforce6200/INDEX.md` (リポジトリ外)。

---

## 9. まだ分からないこと・次に確かめること

| # | 分からないこと | 確かめる段 | 補足 |
|---|---|---|---|
| 1 | BAR0・BAR1・BAR2 の大きさ (BAR1 の VRAM 窓が何 MB か) | 4a A1 | BAR を 1 本ずつ測ってすぐ戻す |
| 2 | 54h=0 (レガシーデコード無効) のまま、head0・head1 の VGA 別名 (SR・GR・MISC・CR) が MMIO で応答するか | 4a A2 | 応答しなければ 4a を止めて設計し直す (54h=1 や I/O デコードで迂回しない) |
| 3 | PMC_ENABLE・PCRTC・PRAMDAC が冷えた状態で 0 でない理由 | — | 「POST 済み」の証拠にはしない。電源投入時の既定値か [推] |
| 4 | BIT `I` の追加 script の欄が非零か | 4a S2 (decode) | 非零なら 5 本で完了とせず取り込んでレビュー |
| 5 | PLL limits 表 4 件の中身と、PLL の書込み手順・係数 (101 候補) | 4a S4 | nouveau `pllnv04.c`・`devinit/nv04.c` に従う |
| 6 | COPY 2 件の式・一時値の再生 3 件の仕組み | 4a S2/S5 | 解消まで拒否のまま |
| 7 | main 0〜3 を実際に走らせて固まらないか、組 3 の PFB の値が readback で一致するか | 4a A3-0〜3 | main ごとに承認・立会い |
| 8 | VRAM が読み書きできるか、容量 (10020Ch が意味のある値になるか) | 4a A4 | 容量不明でも BAR1 先頭 1KiB の最小試験はする |
| 9 | 11014h が何のブロックか、10021Ch・1002D8h (macro 1・3) の意味 | — | 資料に無い。実機の挙動だけで判断することになる |
| 10 | どの DCB 出力が RGB-IN につながっているか、DDC の配線 | 4b・4c | I2C・GPIO・CONN の表をノートの出力から読む |
| 11 | 0FACh の bit の意味 (bit0・bit1) が Ra266 で資料どおりか | recover T5 (0FACh の初めての書込みを明示承認で 1 回) | 98 の画面に戻す処理の前提 |
| 12 | メモリの種類 (DDR か) | 4a A4 の後 | perf 表と PLL からの推測だけ |
