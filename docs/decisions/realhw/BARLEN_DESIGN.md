# BARLEN — 他の装置のメモリ BAR の長さ (GeForce A1 の資源の確認) — 設計

> 状態: **現行 (2026-10-23)** — 確定 (astra 起草・Opus 2 往復・PM が最終修正、ユーザー承認 2026-10-23「上限で照合する」)。段の状態は票 TASK_HWDEV が持つ。
> 出所: ~/os32-tmp/run/barlen/DESIGN.final.md を 2026-10-23 に写した (往復の版と REVIEW はそこに残る)。本文の r2 の見出しと『r1→r2 の変更』は往復の記録として残す。


## r1→r2 の変更

- PM: 既知長を優先し、整列と隣の割当による上限を併用。基準構成を資料待ち・書込みなしで照合する。
- B1: U1を任意の精度改善へ変更し、Tridentのみの測定と使用中82557の測定を分ける。
- B2: TridentのCommandだけ0000hにした82557単独の2区間試験を6aに追加する。
- B3: PCI writeの回数を数えるstubを追加し、変異をリンク成功後のassertで検出する。
- B4: Intel PDFのSHA256とTable 3の短い原文をREFERENCES.txtに追加する。
- B6: HW3未充足という記述を、Tridentの正確な長さの値の根拠が未取得という記述へ直す。
- 起草日: 2026-10-23。対象: Ra266、GeForce A1、hwdev.mod。
- 推奨: 既知長表＋保守的な上限で非重複を証明する。他装置の PCI データには書かない。
- 状態: 基準構成は上限で照合可能。Trident の資料・実測を S8 の前提から外す (実装・試験は未実施)。
- 正確な長さと除外用の上限を区別する。BIOS の割当を信じる範囲は §2、任意の精度改善は §7。
- パス略記: `R` = `/home/hight/os32-v3`、`F` = `/home/hight/os32-tmp/ref/freebsd-main`。
- `E` = `/home/hight/os32-tmp/evidence/2026-10-15/ra266_snapshot/ra266_snapshot_2026-10-15.txt`。
- `S` = 本票と同じディレクトリの `sources`。取得元・SHA256 は `S/REFERENCES.txt:1`。

## 1. 実物の確認


| 確認項目 | 実物と設計上の扱い | 根拠 file:line |
|---|---|---|
| 票とコード | BRIEF の拒否条件の説明と一致。食い違い **0 件** | `R/docs/tasks/realhw/TASK_HWDEV.md:54`、`R/kdebug/gfinit_resources.c:79` |
| 列挙 | bus 0 の 32 device × 8 function を生読み。boot cache は使わない | `R/kdebug/gfinit_resources.c:64` |
| 82557 | Command=0147h、BAR0=20410008h、BAR1=6001h、BAR2=20500000h | `E:185` |
| Trident | Command=0002h、BAR0/1/2=20000000h/20400000h/20800000h | `E:163` |
| NEC | 0:7.0 は Command=0003h でも BAR 全零なので長さ拒否なし | `E:141`、`R/kdebug/gfinit_resources.c:83` |
| 最初の拒否 | 列挙順から 0:8.0 BAR0 で終了。82557 の拒否はその後に潜在 | `R/kdebug/gfinit_resources.c:67` |
| ROM | Trident と 82557 の ROM は非零だが enable=0。既存判定で許可 | `E:166`、`E:188`、`R/kdebug/gfinit_resources.c:89` |
| 長さと重なり | resources は除外区間の収集。GPU 測定後の重なり検査は別関数 | `R/kdebug/gfinit.c:238`、`R/kdebug/gfinit.c:222` |
| 旧 host 例 | ID=12298086h でも BAR0=20410000h を拒否する試験あり。新表では属性不一致として引き続き拒否 | `R/tools/tests/gfinit_bus_host.c:258` |
| 部品の所在 | GF 部分は M3c で hwdev.mod へ移動済み。旧本文の kdebug 表記には追記あり | `R/docs/decisions/realhw/GF6200_4A_DESIGN.md:239` |


- 82557 は現在 LAN SerialFS で使用中という依頼の前提を採用する。`E:306` の当時の declined 表示を現在の未使用証明にしない。
- 単一利用者・単一タスク・協調切替を維持。9801 対応・権限分離・攻撃者対策は追加しない。
- 資源収集中は yield せず、装置の追加・再割当てもしない。PCI config の index/data 排他は既存 API に任せる。

## 2. 長さの得方

| 方式 | 利点 | 制約・採否 |
|---|---|---|
| (a) 既知長表＋上限照合 | 82557 の通信・DMA・割込み・BAR を変更せず、Trident は保守的上限で照合 | **推奨**。既知長を優先し、上限との矛盾は拒否 |
| (b) all-1 測定 | 実装された aperture を取得できる | Mem decode 停止・所有者の静止・復元が必要。使用中の 82557 に自動適用しない |
| (b') 組合せ | 82557 は表、所有者なし・BM=0・IO=0 の Trident だけ1回測定 | 上限で足りるため今回は不採用。将来の任意の精度改善として分離 |
| vendor:device / BAR | 長さ・属性 | 根拠と確度 |
|---|---|---|
| 8086:1229 / 0 | **0x1000 B**、memory32、prefetchable | Intel rev.1.3 §4.1.10 Table 3、確認メモ `S/REFERENCES.txt:12` |
| 8086:1229 / 2 | **0x100000 B**、memory32、non-prefetchable | 同 Table 3、`S/REFERENCES.txt:14`。Flash 実容量ではなく aperture |
| 8086:1229 / 1 | I/O 32 B、メモリ除外には入れない | 同 Table 3、`S/REFERENCES.txt:13` |
| 1023:9660 / 0 | 正確値未確定、基準構成の上限 **4 MiB** | MIT 実装は BAR0 を framebuffer として使うだけ (`S/netbsd-trident.c:2294`) |
| 1023:9660 / 1 | 正確値未確定、基準構成の上限 **64 KiB** | BAR1 選択 `S/netbsd-trident.c:2313`、64 KiB map は使用量 (`S/netbsd-trident.c:1568`) |
| 1023:9660 / 2 | 正確値未確定、基準構成の上限 **8 MiB** | 上記ドライバには本 BAR の長さを保証する根拠なし |
| 1033:0009 | 基準構成ではメモリ BAR なし。長さ表不要 | `E:142` |

- Intel の一次資料: [8255x Software Developer Manual](https://www.intel.com/content/dam/doc/manual/8255x-10-100-mbps-ethernet-controller-software-dev-manual.pdf)、印刷頁15–17。 BSD 対照: `F/sys/dev/fxp/if_fxpreg.h:31` は MMBA=10h/IOBA=14h、同:37 は CSR。**このヘッダ単独では BAR 長を証明しない**。
- `F/sys/dev/fxp/if_fxp.c:310` は MMBA を memory resource として確保する。固定長の根拠は上の Intel Table 3。
- Trident のメーカー資料所在は `S/REFERENCES.txt:17`。HTTP 403 で本文未確認。正確な長さの値の根拠が未取得。 `S/netbsd-trident.c:176` は同じ PCI ID を複数型で使う。ID だけで全系列の同一 aperture を保証しない。
- Trident 表を有効にする条件: 3 本それぞれの aperture と適用 revision をメーカー資料で確認し、BSD/MIT 実装と対照した file:line を追記する。 BAR 間隔と整列は以下の条件で**上限**に使う。VRAM量・ドライバのmap長を正確なBAR長とはしない。
- Intel PDF の SHA256・Table 3 原文は `S/REFERENCES.txt:21`、同:22。取得バイトをhash化しPDF自体は保存しない。 memory32 の非零baseに対し `align_cap = base & (0U-base)`。長さは2冪かつbaseを整列させるので、実長 <= align_cap。
- `next` はbaseより大きい最小の割当済みmemory BAR/ROMのbase、`cap = min(align_cap, next-base)`。隣がなければalign_capを使う。 隣候補は同一装置の別BARも含む。Command Mem・prefetch・ROM enableに関係なく非零割当を採る。I/Oは除外、属性ビットは除去する。
- **信頼範囲**: Ra266の起動BIOSが非GPUの独立したBAR/ROM窓を測定し非重複に配置したこと、起動後再割当てなしを前提とする。alias配置・未知の再割当てでは隣による縮小を使わず拒否する。 GPU自身のBAR/ROMとbridge forwarding窓は隣候補にしない。GPUとの非重複を検査する結論を縮小の前提に使わない。
- 上限方式の許可は基準Trident 1023:9660、rev=d3、endpoint BAR0〜2、attr=0 に限定。未知装置・未知BARは従来どおり拒否する。 基準Trident: BAR0→20400000hで4MiB、BAR1→20410000hで64KiB、BAR2→無効ROMの21000000hで8MiB。除外終端は最大21000000h (半開) 。
- 82557の上限はBAR0が64KiB、BAR2が1MiB。既知長4KiB/1MiBを優先。表長>capなら割当/表の矛盾として拒否する。
- BSD/MIT ソースのみ保存。メーカー資料は閲覧と要約のみ。GPL/Linux は根拠・挙動確認・構造参照に不使用。
(b)/(b') は今回実装しない。将来採用するときの条件:

- (b') Tridentのみ: OS32ドライバなし (`RA266.md:26`、レビューB1)、Command=0002hを再確認。所有者なし・BM=0・IO=0を条件に、cliの有限区間でMem停止→1本ずつall-1/読取/即復元→全BAR readback→Command16復元/readback。途中log/yieldなし、rev d3と測定証拠を結び付ける。 (b) 82557も測る: hsync停止・LAN以外の採取路・所有ドライバのCU/RU/DMA/IRQ停止を期限付きで証明してからI/O/Mem/BM停止。cliだけをDMA停止証明にしない。
- 両案ともユーザーの対象BDF別実機承認が必要。復元不一致ではdecode/BMを戻さずA1禁止、cold復帰。Commandは16bit、Statusへの書込みを避ける。 BSD対照: `S/freebsd-pci.c:3237` (decode停止)、同:3259 (all-1)、同:3272 (復元)、`S/netbsd-pci_map.c:149` (splhigh)。稼働NICの停止保証を含むものではない。

## 3. 照合の形 — 部品の境目

- 公開入口 `gfinit_resources(exclude,count)` は不変。成功時だけ count を確定し、失敗時 count=0。
- 既知長表と helper は `gfinit_resources.c` の static。新しい object・KAPI・core import slot は不要。
- 下は境目の設計コード。数値定数は同ファイルの Local Layer に名称付きで定義する。
```c
/* id=device<<16|vendor、bar=0-based、len=正確長、attr=下位nibble */
struct resource_bar_spec { u32 id, bar, len, attr; };
static const struct resource_bar_spec resource_bar_specs[] = {
    {GF_RES_FXP_ID, GF_RES_BAR0, GF_RES_FXP_CSR_LEN, PCI_BAR_MEM_PREFETCH},
    {GF_RES_FXP_ID, GF_RES_BAR2, GF_RES_FXP_FLASH_LEN, GF_RES_ATTR_MEM32}
    /* Trident は正確長の表に入れず、別の許可判定で上限を使用。 */
};
struct resource_bounds { u32 base[RECOVER_RANGE_MAX]; u32 count; };
static int resource_collect_bounds(struct resource_bounds *b); /* 第1走査 */
static u32 resource_cap(const struct resource_bounds *b, u32 base);
static const struct resource_bar_spec *resource_spec(u32 id, u32 bar);
static int resource_memory_bar(struct recover_range *exclude, u32 *used, const struct resource_bounds *b, u32 id, u32 dev, u32 fn, u32 header, u32 bar, u32 raw);
/* gfinit_resources のcount=0設定後、既存の除外収集(第2走査)より前 */
struct resource_bounds bounds = {0};
if (resource_collect_bounds(&bounds)) return -1;
/* gfinit_resources.c:81 のループ内、従来の無条件拒否を置換 */
if (raw && !(raw & PCI_BAR_SPACE_IO)) {
    if (resource_memory_bar(exclude, &used, &bounds, id, dev, fn, header, bar, raw))
        return -1;
}
/* gfinit.c:238 の収集呼出しと同:222 の recover_bar_probe 呼出しは変更なし。 */
```

| 定数の置き場 | 内容 |
|---|---|
| `drivers/pci_decode.h:88`、同:148 | BAR0 offset/count、IO/type/prefetch/address mask を既存定義から利用 |
| `kdebug/gfinit_resources.c` Local | FXP_ID=0x12298086、BAR0=0、BAR2=2、CSR_LEN=0x1000、FLASH_LEN=0x100000 |
| 同 Local | ATTR_MASK=0xf、ATTR_MEM32=0、BAR_STRIDE=4、TRIDENT_ID=0x96601023、REV=0xd3、ROM_ADDR_MASK=0xfffff800。revision offsetは既存PCI定義を利用 |
| `kdebug/recover.h:46` | 既存 RECOVER_RANGE_MAX を利用。終端は既存 RECOVER_ADDRESS_END を利用 |
| `kdebug/gfinit_resources.h:4` | 既存 GPU ID/BDF と入口。コメントだけ既知表の方針へ更新 |

- 第1走査は既存と同じbus0全function、header別BAR本数とROM位置で候補を収集する。GPUは除外、非零memory32とROM baseを重複除去して保存、256件超は `memory_bar_count`。64bit BARは上位も読み飛ばして候補にせず、範囲外headerは既存どおり拒否。 第2走査は既存のMem有効判定を維持。2走査間にyield/割当変更なし。第1走査のdisabled ROM候補採用と、第2走査のenabled ROM拒否は別の処理。
helper の処理順:

1. memory32 以外を `memory_bar_type` で拒否。64bit の上位 DWORD を別 BAR として扱わない (下位で即終了)。
2. endpointのID:BAR表を検索。未登録でも§2のTrident許可条件に合えば上限を使う。それ以外は `memory_bar_length`。
3. 下位nibbleが表/上限許可条件のattrと異なれば `memory_bar_attr`。base非零を確認してcapを算出する。
4. 表長は非零・2冪・base整列を検査し、表長>capは `memory_bar_bound`。表なしはlen=cap (距離由来のcap自体に2冪/整列は要求しない)。非零と64bit整数のbase+len<=2^32を検査、不一致は `memory_bar_range`。
5. used >= RECOVER_RANGE_MAX は `memory_bar_count`。合格した `{base,len}` を1本ずつ append。
6. 表内の ID:BAR 重複と len の不正は host の表検査でも拒否。合計数・上限は整数だけで計算する。

- 未知装置は「有効な非零 memory BAR がある」場合に拒否。BAR 全零・I/O だけ・Mem無効まで装置IDだけで拒否しない。 既知装置でも表/上限許可にないmemory BARは拒否。表にないI/O BARは従来どおり読み飛ばす。
- 長さとアドレスを分離し、82557 の現配置を定数にしない。正しく整列した移動先にも同じ長さを適用する。
- Trident の将来の表が特定 revision 限定なら spec に revision/mask を追加する。その照合を省いて広い ID 許可にしない。
- ROM enable、bridge window の type/high/range 検査は `gfinit_resources.c:37`、同:87 のまま保持する。
- 他装置どうしの区間重複は本変更では判定しない。bridge窓とその子の重複・alias で誤拒否しない。
- GPU 対各除外区間は正確長または上限を使った半開区間 `[base,base+len)`。`a < b_end && b < a_end` のときだけ拒否、隣接は許可。
- 比較は `R/kdebug/recover.c:1997` に既存、全加算は64bit整数。GPU長はA1で測り、snapshotのBAR値から作らない。
- trace は `GF A1 reject=%s bdf=0:%u.%u reg=%x raw=%x` を維持 (`gfinit_resources.c:30`)。
- 上記新 reason でも reg は `PCI_CFG_BAR0 + BAR_STRIDE*bar`、raw は元の32bit値。部分配列は count=0 で無効化する。
- 重なりの失敗は既存 recover の access_error と cleanup 経路を使う。架空の resource_reject 発生を試験で期待しない。
- 成功 trace の追加は行わない。既存 host の kprintf mock は reason 引数前提なので不要な書式追加を避ける。

## 4. 部品の大きさの見積もり


| 項目 | hwdev.mod 増分 (未ビルドの見積もり) |
|---|---:|
| 表・許可条件 | 82557の2行=32 B、Trident ID/revision/BAR条件の定数は別途 |
| 2走査・候補収集・cap・lookup・検査・append | text 約900〜1,800 B |
| reason文字列・定数・整列・再配置 | 約150〜350 B |
| 合計 | **約1.1〜2.2 KiB**、上側を丸めて +2,560 B を予算にする |


- 基準 hwdev.mod 131,488 B に対し予算後 134,048 B。リンク結果の実測を受入記録に残す。
- exclude配列は既存256枠。heap・恒久bss増分0、boundsは局所1,028 B。実装時にA1呼出し鎖のstack使用量を確認する。
- kdebug.bin は109,060 Bのまま、設計上増分0。recover.c は変更せず、GF helper は hwdev.mod 内だけ。
- import/ABI を増やさない。実装時は同じ設定の前後で map/サイズを測り、予算超過なら内訳を再提示する。

## 5. ファイルごとの変更一覧


| 区分 | ファイル (R 相対) | おおよその変更行数 |
|---|---|---:|
| 変更 | `kdebug/gfinit_resources.c` 表・helper・ループ置換・根拠コメント | +130〜180 / -4 |
| 変更 | `kdebug/gfinit_resources.h` 入口契約コメント | 2〜4 |
| 変更 | `tools/tests/gfinit_bus_host.c` 基準写し・境界・trace・連結試験 | +140〜190 |
| 変更 | `tools/tests/test_gfinit_bus.py` RESOURCE_MUTATIONS 更新 | 12〜20 |
| 新規 | `tools/tests/gfinit_resources_ra266.h` 生dumpの00〜3fh、7台分、出典行付き | 45〜65 |
| 変更 | `tools/check_map.d/check-gfinit-host.yaml` fixture追加 | +1 |
| 変更 | `docs/decisions/realhw/GF6200_4A_DESIGN.md` BARLEN契約への参照 | 3〜5 |


- gfinit.c、recover.c、drivers/pci_decode.h、build/hwdev.mk は変更不要。新objectを作らず M4 との接点を小さくする。
- fixture は E の7台の pcidump から転記し、それ以外の function は vendor=ffffh。raw attribute bit を保持する。
- この起草では上記リポジトリファイルを変更していない。書いたのは本ディレクトリの設計票と参照用資料だけ。

## 6. 受入 → 試験と変異、実施の段

| 受入項目 | host / ゲストの試験 | 1行の変異 (入力変異または実装変異) |
|---|---|---|
| 基準構成が通る | host: E写しで成功、Trident上限4MiB/64KiB/8MiB＋82557既知長4KiB/1MiBの5区間と完全一致 | 実装: `min(align_cap, gap)` をalign_capだけに変更 → 5区間比較が落ちる |
| B2の6a独立試験 | host: E写しのTrident Commandだけ0000h、成功・除外2本のbase/len完全一致 | 実装: CSR_LEN=0x2000 または FLASH行bar=3 (各1変異) → 配列/成功検査が落ちる |
| 上限がGPUへ届く | host: 他の候補を零にしTrident BAR0=20000000hだけ残す。cap=512MiB、GPU BAR0とのprobe連結で拒否 | 実装: 上限区間のappendを省く → 衝突試験が落ちる |
| 隣なし・表との矛盾 | host: 隣なしでもalign_capで判定、別例でFXP BAR2の直後+0x80000にROM候補を置きmemory_bar_bound | 実装: `spec->len > cap` 検査を削除 → 矛盾試験が落ちる |
| 無効ROMも候補 | host: 候補配列に無効ROMの21000000hがあり、GPU候補なし。重複と256件上限も検査 | 実装: ROM enable=0を候補から除く → 候補配列検査が落ちる |
| 長さによるGPU衝突 | host: 82557 BAR2を24000000hへ移し、GPU BAR0=24000000h/mask=ff000000hの recover_bar_probe 連結試験で拒否 | 入力: BAR2=0x24000000 (表長1MiBでGPU区間と交差) |
| 長さを無視しない | host: 除外をGPU窓内に包含させても拒否、両端一致の隣接は許可 | 実装: recover の重なり条件の一方を false → 衝突試験が落ちる |
| 未知装置拒否 | host: 基準構成の82557 IDだけ変更して memory_bar_length、count=0 | 入力: ID DWORD=0x12348086 |
| 表のBAR番号違い拒否 | host: 既知IDでも BAR2用の長さを別番号へ流用しない | 実装: FXP_FLASH行のbarを2から3へ変更 → 基準試験が落ちる |
| 属性・型拒否 | host: BAR0 prefetch欠落、mem64、below1M、予約型を各拒否 | 入力: 82557 BAR0=0x20410004 |
| 整列・容量・範囲 | host: 不整列、len=0/非2冪、used=256、終端2^32と超過をhelperで検査 | 実装: `base & (len-1)` の検査を除去 → 不整列試験が落ちる |
| 通知とcount | host: reason全文・bdf・reg・raw、先にappendしてから失敗してもcount=0 | 実装: reasonを"unknown"に固定 → trace比較が落ちる |
| 既存の走査維持 | host: function7、Mem無効、I/O、ROM、bridge high/type/無効窓の既存試験 | 実装: fn上限を1へ変更 → function7試験が落ちる |
| 他装置へ書かない | host: pci_cfg_write16/32を既存宣言と一致する回数計測stubとして追加、各試験でresetしwrite_count==0 | 実装: helperにBAR書戻し1回を挿入 → リンク成功後write_count検査が落ちる |
| 実機の通信維持 | ゲスト: 承認済A1の前後で82557 Command/BAR同値、hsync継続、GPU測定値で非重複を確認 | host代替変異: write stubを呼ぶ行を追加し検出 (実機に故障注入しない) |

- 衝突 fixture の GPU mask はテスト用合成値であり実測値ではない。2026-10-15 dump から測定済みとはしない。
- 上限の5区間を期待値とし、正確なTrident長と表示しない。6aの基準構成試験は資料取得を待たず常時実行する。 6a (半日〜1日): 表＋上限helper、基準fixture・B2変種・write stub・拒否/境界/trace、既存mutation追随。
- 6b (半日〜1日): probe連結試験・変異・実像回帰・サイズ計測。M4と共有するhost/owner回帰は直列に統合する。
- M4の持ち主/終端コードと resources 本体は別。gfinit_bus_host.c・test_gfinit_bus.py・check-map は競合し得るため先に差分を取り込む。
- 実装時の検査: `make check-gfinit-host`、GF owner/plan の既存host検査、hwdev実像検査、`make check-changed` と `make check-c-dialect`。
- 実像検査は既存 `tools/tests/test_hwdev_image.py` を使い、核にGFが戻らないこと・import不変・hwdevのサイズを確認する。
- ゲストは T4b のNP21/WでGPU成功を要求しない。実機A1はS8の段別承認後、hsyncと復元値を併記して受け入れる。
- 本票作成時はビルド・host試験・実機測定を実行していない。上記は実装後の受入仕様である。

## 7. 未決 — ユーザー判断が要るものだけ

- **U1: 正確なTrident長を追加取得するかは任意。推奨は今回取得せず、(b)/(b')を不採用とする。S8の前提にはしない。**
- (a) 希望時にユーザーがブラウザでPDFを取得し、表の精度を改善する。上限で既に通る基準構成の承認待ち項目にはしない。 (b') S8立会いでTridentだけ1回all-1測定しID/rev d3へ登録する案は、将来上限が広すぎて拒否された場合の選択肢。追加書込みの承認は別途必要。
- (b) 使用中82557も測定する案は非推奨。既知長で足りるためLAN停止とBAR書換えを追加しない。

反論: なし。

## 確定 (2026-10-23、Opus 第 2 回の条件付き承認、PM が反映)
- C1 (P2): §2 の「隣の候補」は **Command の Mem が有効な装置の、非零の memory32 BAR だけ** に絞る。Mem 無効の装置の BAR・無効の ROM の番地は候補にしない (BIOS の割当の前提の外)。§6 の r2:173 の試験は逆にし、「無効の ROM や Mem 無効の装置の BAR では上限が縮まない」を期待値にする。
- C2 (P2): 別の装置・別の BAR の間で base が等しければ `memory_bar_alias` で拒否する。§3 の重複除去は同じレジスタの重複だけにする。それ以外の alias の検出はしない。
- C3 (P2): §6 に負例を足す — Trident の rev を d2 にする / Trident の BAR3 に非零 / attr=8 (prefetch)。期待は `memory_bar_length` か `memory_bar_attr`、count=0。
- C4 (P3): 「隣を選ぶ」計算の変異を 1 行足す (`next` を最小でなく最初に見つかった候補にする) — 基準構成の 5 区間の比較で殺す。
- C5 (P3): 局所の bounds 1,028 B は実装の段で A1 の呼出し鎖の stack を測り、足りなければ static の bss に置く (+1 KiB を予算に)。
- PM 決定: U1 は任意の精度改善に下げ、取らない。(b')・(b) (BAR への書込み) は採らない。上限が広すぎて実機で拒否されたら改めてユーザーに上げる。DEFERRED_TESTS には載せない (基準構成は上限で通る)。

## 取り込みの後の名前の変更 (2026-10-25、票 TASK_MAGIC_NUM の M4)

- 境目の設計コード (`resource_bar_specs`) の `GF_RES_FXP_ID` は `((PCI_DEVICE_82557 << 16) | PCI_VENDOR_INTEL)`、`GF_RES_ATTR_MEM32` は `PCI_BAR_MEM_TYPE32` (どちらも `drivers/pci_decode.h`) に置き換えた。値と動作は同じ (機械語の差 0)。
