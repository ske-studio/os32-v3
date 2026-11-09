> 出所: ~/os32-tmp/run/gf-bdf/DESIGN.final.md (2026-11-09、Claude 起草・codex astra のレビュー)。
> 状態: **現行 (2026-11-09)** — GeForce の PCI の位置を起動時の列挙で見つける の確定設計。段の状態は票 TASK_GF_BDF が持つ。

# GF-BDF final — GeForce (10de:0221) の PCI 位置を決め打ちから「バス 0 の列挙」へ
基点 main 3ebbd208。前提: シングルタスク・協調切替、整数、[C1]〜[D3]。GeForce 2 枚は考えない (最初の 1 枚、2 枚目は記録して無視)。
経緯: Ra266 で USB カードと入れ替えて GeForce が 0:12.0 になり、決め打ちの 0:13.0 (USB) を読んで `gf_production_identify` が -1。いまはユーザーが HighPoint の構成に戻し、GeForce は 0:13.0。

## r1→r2 の変更
- 探索を CPU に依らない共通の PCI の口 `drivers/pci/pci_locate.{h,c}` (`pci_locate_fn0`) に分け、`RECOVER_GF_*` は GF 側 (kdebug/recover_gf_locate.c) にだけ残す (ユーザー方針: PowerPC にも効く)。
- 像は 4 つ (vmkernel・geforce.mod・kdebug.bin・hwdev.mod) に直した。保持・初回決定・大きさの検査も 4 つで書く (P3)。
- 像のあいだの一致の根拠を書き直した: 同じ起動のあいだ PCI の配置は変わらず、対象は 1 枚で、探す順序も同じ。gf_stage_state.c:61 / gfinit_bus.c:54 は診断経路の中の照合で、像のあいだの照合ではない (P3)。
- kdebug/gf5_test.c:504 の `target=0:13.0` を実際の dev から出す (P3)。
- 試験: gfinit_bus_host.c の `dev==13` (:19,31,37)・除外 (:465)・実測 fixture (:374-376) を試験の位置に追従させ、12 番の配置は実測を残したまま別に作る (P2)。gf_production_host.c に、本物の探索 + identify + 本番の PCI の読み書きを 12 番で通す fixture と、GPU が居ないとき PCI の書きが 0 回の fixture を足す (P2)。
- 受入は今の 0:13.0 を実機で必須、0:12.0 は host fixture で必須。実機の構成の表を足した (ユーザー方針「色々な組み合わせを試す」)。§6 は PM 決定で閉じた。

## 1. 実物の確認
`RECOVER_GF_{BUS,DEV,FN,BDF}`・`GF_RES_GPU_DEV` の利用は 73 行 (hwdev 25 / kdebug 27 / drivers 4 / tools/tests 17)。定義は kdebug/recover.h:53-56、kdebug/gfinit_resources.h:4。ほかに直値で書いた所: kdebug/gf5_test.c:504 (`"target=0:13.0/10de:0221"`)、tools/tests/gf_production_host.c:112,122,189-190、gfinit_bus_host.c:19,31,37,374-376,465。

| 分類 | 像 | file:line | 使い方 |
|---|---|---|---|
| 製品 (カーネル) | vmkernel (GEFORCE_BOOT_OBJS) | hwdev/gf/resources.c:17-25 | identify: ID・subsys・rev・BAR0-2 を生読み、`.bdf=RECOVER_GF_BDF` |
| 製品 (カーネル) | 同 | drivers/video/geforce/geforce_platform.c:45 | 台帳 `ledger_resource.bdf` (実行時の複合リテラル) |
| 製品 (カーネル+module) | 同 + geforce.mod | kdebug/gfinit_resources/scan.inc:21-22,46, bounds.inc:38 (`GF_RES_GPU_DEV`) | バス 0 の除外表から GPU 自身を外す、居なければ gpu_id / gpu_missing |
| 製品 (module) | geforce.mod (GEFORCE_SOURCES) | hwdev/gf/master.c:16-42, devinit.c:127-130, bus.c:10-15,34,114-116, irq.c:94 | Command・cache line・CFG50/54・INT_LINE の読み書き |
| 製品 (受入版) | drivers/video/geforce | gf_accept.c:177-178 | `pci_cfg_read*(BUS,DEV,0,...)` (FN でなく直値 0) |
| kdebug の復旧 | kdebug.bin・hwdev.mod | kdebug/recover_profile.c:240, recover.c:76 (CRC に expected_bdf), recover_cleanup.h:162, gfinit.c:83 (**静的初期化子**),281,395,456-457, gfinit_bus.c:34-59,109-112, gf_stage_state.c:61 | profile.expected_bdf、hwdev 所有者 `gf_device.bdf`、生 config の再検証 |
| kdebug の試験口 | kdebug.bin・hwdev.mod | gf_accel_test.c:776-779, gf_command_test.c:312-314, gf_foundation_test.c:66-74, **gf5_test.c:504** | ID の照合・Command の読み・bdf の表示・GF5 の対象の表示 (直値) |
| 試験 (host) | tools/tests | gf_command_t2b_host.c:54, gf1_master_host.c:9-31, gf_accel_c1_host.c:266-278 (偽 PCI が `dev==RECOVER_GF_DEV` を assert)、gfinit_bus_host.c:19,31,37 (`dev==13`),79,328,453,470,550 (`resource_cfg[13]`),374-376 (実測 fixture `resource_ra266`: GPU=13・別カード=12),465 (除外 `d==13`)、gf_production_host.c:112,122,189-190 (13 の直値)、gf_stage_host.inc:60, gf_wc_host.c:198, gfinit_sequence_host.inc:31, recover_t2_host.inc:289-377, gfinit_budget_host.c:150, test_gf1_master.py:34-35 (変異の文字列) | |
| 文書 | docs | design/hwdev/gf/{production,resources}.md:5, design/kdebug/gfinit_bus.md:29, design/hwdev/gf/bus.md:26, design/kdebug/recover_gf.md:9, decisions/realhw/GF_FEAT1〜5・GF_ACCEL の「対象」行, RA266.md:54-57, tasks/realhw/TASK_GEFORCE6200.md:1,29, TASK_HWDEV.md:55, PLAN.md:15, DEFERRED_TESTS.md:37 ほか (GF-L2/L3/L5 は未実施) | 「0:13.0」 |

既存の PCI 表: drivers/pci/pci.c `g_pci[PCI_MAX_DEVS=32]` + `pci_find`/`pci_get`。走査はブリッジで深さ優先に潜るので、32 件で溢れるとバス 0 の後ろが落ちる。gfinit_resources/scan.inc:14 も同じ理由で「boot cache は使わず生読み」と決めている。→ **表は使わず、同じ方針でバス 0 を生読みする**。共通の口は `pci_cfg_read32` だけに頼る。PCI config の backend は機種・ホストブリッジの側 (今は PC-98 の mech#1、pci.c:76-92 のポート I/O)、CPU の原始操作 (割込みの禁止・復元など) は arch の側 (arch/README.md:7-8)。backend の分離の実装は本設計の範囲外。`pci_locate_fn0` は PowerPC 全体の列挙ではなく、backend が整った後に再利用する単一バスの探索として扱う (割込み・割り当ての共通層はユーザー方針の別の設計で扱う)。

固定の理由の確認: production.md/resources.md:5 は「ID 10de:0221・subsys 10fc:1270、Ra266 の 0:13.0 に限り BAR 長を定数 (gf_a4 の実測)」。定数はボードの測定値で、位置ではなく **ID+subsys に結びつく** (resources.c:21-22 も BDF は照合しない)。gfinit.c:80 の「固定の停止した機器だけを claim」は「10de:0221 の fn0 だけ」で保たれる。scan.inc:14 の「ブリッジは GPU の経路外」は **バス 0 に限る** ことで保たれる。→ 理由は崩れない。

## 2. 形
共通の口 (新規 drivers/pci/pci_locate.h / .c。CPU・機種・GF の名前を持たない):
```c
/* drivers/pci/pci_locate.h */
struct pci_locate { u8 found, dev, second_found, second; };
/* Raw fn0 scan of one bus for an id dword (device<<16|vendor), dev 0..31 in order.
 * Not the boot table (it can truncate). Only pci_cfg_read32 (platform/host-bridge backend). */
void pci_locate_fn0(u32 bus, u32 id, struct pci_locate *out);

/* drivers/pci/pci_locate.c */
void pci_locate_fn0(u32 bus, u32 id, struct pci_locate *out)
{
    *out = (struct pci_locate){0};
    for (u32 dev = 0; dev < PCI_DEV_COUNT; dev++) {
        if (pci_cfg_read32(bus, dev, 0, PCI_CFG_VENDOR_ID) != id) continue;
        if (!out->found) { out->found = 1; out->dev = (u8)dev; }
        else if (!out->second_found) { out->second_found = 1; out->second = (u8)dev; }
    }
}
```
GF 側 (新規 kdebug/recover_gf_locate.c。名前 `RECOVER_GF_*` はここと recover.h だけ):
```c
static struct pci_locate gf_loc;  /* found=0: 未解決。見つからない時は覚えない (pci_init 前の呼び出しで固まらない) */
u32 recover_gf_dev(void)
{
    if (!gf_loc.found) pci_locate_fn0(RECOVER_GF_BUS, RECOVER_GF_DEVICE, &gf_loc);
    return gf_loc.found ? gf_loc.dev : RECOVER_GF_DEV_FALLBACK;
}
u32 recover_gf_second(void) { (void)recover_gf_dev(); return gf_loc.second_found ? gf_loc.second : U32_MAX; }
```
kdebug/recover.h:53-56 (名前は残す。73 行の呼び出し側は無変更):
```c
#define RECOVER_GF_BUS 0U
#define RECOVER_GF_FN 0U
#define RECOVER_GF_DEV_FALLBACK 13U /* 見つからない時。ID 照合で従来どおり降りる */
u32 recover_gf_dev(void);
u32 recover_gf_second(void); /* U32_MAX = 2 枚目なし */
#define RECOVER_GF_DEV (recover_gf_dev())
#define RECOVER_GF_BDF (RECOVER_GF_DEV << 3)
```
- **保持と初回決定 (4 像)**: 像ごとに static の `gf_loc` を持つ。`pci_locate.c`・`recover_gf_locate.c` を 4 像に同じ源で入れる。
  | 像 | 初回の呼び出し |
  |---|---|
  | vmkernel | geforce_platform.c `reserve()` の identify (pci_init は boot.c:150 で先) |
  | geforce.mod | 読み込み後の master/devinit/bus の最初の config の読み書き |
  | kdebug.bin | gfinit・gf1〜gf5 の診断の最初の呼び出し |
  | hwdev.mod | HWDEV_GF の診断の最初の呼び出し |
- **像のあいだで値が一致する根拠**: 同じ起動のあいだ PCI の配置は変わらない (抜き差しは電源を切ってから)。対象は 1 枚で、4 像は同じ源・同じ順序 (dev 0→31) で同じ生 config を読む。像のあいだを突き合わせる検査は無いし、足さない。gf_stage_state.c:61 は保存した診断の identity との比較で、gfinit_bus.c:54 は同じ診断経路の profile との比較 (どちらも 1 つの像の中の照合)。kexport・KAPI の変更は無い (`pci_cfg_read32` は 3 つの module の import に既にある)。
- **見つからない時**: 13 を返す。全経路は 0:13.0 の ID を照合して -1 (今日と同じ降り方)。NP21/W (mech#1 absent) は全読みが FFFFFFFF → scan.inc は gpu_missing。書きの経路 (master/devinit) は identify に合格した後だけ通る。→ 書きは 0 回 (§5 で確かめる)。
- 静的初期化子は通らなくなる → kdebug/gfinit.c:83 を `.bdf = 0` にし、:275 の `hwdev_claim` の直前で `gf_device.bdf = RECOVER_GF_BDF;`。
- 表示 (geforce_platform.c、identify に合格した直後。reserve_fail が同じ関数で kprintf しているので可):
```c
kprintf(KLOG_ATTR, "[geforce] pci 0:%u.0\n", RECOVER_GF_DEV);
if (recover_gf_second() != U32_MAX) kprintf(KLOG_ATTR, "[geforce] second 0:%u.0 ignored\n", recover_gf_second());
```
- kdebug/gf5_test.c:504: `number("GF5 DATA stage=info target=0:", RECOVER_GF_DEV); serial_puts_polled(".0/10de:0221");` (`number` は :51、`serial_diag_u32` は 10 進。行の形は変えない)。

wt/gf-usbbar (94f458ea) との重なり: scan.inc・bounds.inc・gfinit_resources.c は**触らない** (`GF_RES_GPU_DEV` は定義の連鎖で変わる)。重なるのは tools/tests/gfinit_bus_host.c だけ → **usbbar を先に取り込み、その上で枝を切る**。

## 3. 大きさ
増分は 2 関数で ~120 B / 像 (static 4 B)、vmkernel は表示 2 行 ~60 B を加え圧縮後 <0.2 KiB (起動 FD は誤差)。geforce.mod・hwdev.mod は mkmod の予算 (GF_FEAT4 C3 の 160/160/208 KiB ほか)、kdebug.bin は現行の予算で検査。受入で vmkernel.lz4・geforce.mod・kdebug.bin・hwdev.mod の前後を記録。
解決した後の費用は分岐 1 つ。見つからない機械では呼ぶたびに 32 回の config の読み (identify で止まるので数回)。

## 4. ファイルごとの変更 (1 枝 wt/gf-bdf、段は順に。A だけではビルドが割れるので B と同じ枝で取り込む)
| 段 | ファイル | 変更 |
|---|---|---|
| A0 | drivers/pci/pci_locate.h・pci_locate.c (新規) | §2 の共通の口 |
| A1 | kdebug/recover_gf_locate.c (新規) | §2 の GF 側 |
| A2 | kdebug/recover.h | §2 のマクロと宣言 |
| A3 | build/kdebug.mk (KDEBUG_OBJS)、build/hwdev.mk (HWDEV_GF)、build/geforce-module.mk (GEFORCE_SOURCES に 2 つ、GEFORCE_BOOT_OBJS に `boot_locate.o`・`boot_pci_locate.o` を boot_scan.o と同じ規則で) | 4 像に 2 つの源を入れる (kdebug.mk の `kdebug/%.c` の規則に乗らない pci_locate.c は明示の規則を足す) |
| A4 | kdebug/gfinit.c | :83 を 0、:275 の前で代入 |
| A5 | drivers/video/geforce/geforce_platform.c | §2 の 2 行 |
| A6 | kdebug/gfinit_resources.h:6 | 注記「GPU is bus 0:13.0」→「GPU is the bus-0 fn0 found by recover_gf_dev()」 |
| A7 | kdebug/gf5_test.c:504 | §2 の表示 |
| B1 | tools/tests/recover_gf_dev_stub.c (新規) | `u32 recover_gf_test_dev = RECOVER_GF_DEV_FALLBACK; u32 recover_gf_dev(void){return recover_gf_test_dev;} u32 recover_gf_second(void){return U32_MAX;}` (偽 PCI が `dev==RECOVER_GF_DEV` を assert する試験向け。本物を入れると再帰する) |
| B2 | tools/tests/test_*.py (§1 の利用ファイルを組む約 25 本のうち未定義参照が出るもの) | コンパイル行に B1 を足すだけ |
| B3 | tools/tests/pci_locate_host.c・recover_gf_locate_host.c + test_pci_locate.py (新規) | 本物の A0・A1 を偽の config 空間 (bus×dev×fn) で試す (§5) |
| B4 | tools/tests/gfinit_bus_host.c | `GF_T_DEV` (= `recover_gf_test_dev`) を入れ、:19,31,37 の `dev==13`、`resource_cfg[13]` の 4 か所、:465 の `d==13` を `GF_T_DEV` に。実測の `resource_ra266` (:374-376、GPU=13・別カード=12) はそのまま 13 で回す。12 番用は別の表 `resource_ra266_swap` (GPU=12、USB の OHCI/EHCI=13 fn0/fn1/fn2、usbbar の BAR 表を使う) を作って `GF_T_DEV=12` で回す |
| B5 | tools/tests/gf_production_host.c・test_gf_production.py | :112,122 の 13 を `PROD_GF_DEV` に。:187-190 の偽の `gf_master_enable` を外し、**hwdev/gf/master.c の実体**を組む。B1 でなく本物の A0・A1 を組み、fixture 3 つ: GPU=13 (今の実測)、GPU=12+USB=13 (identify・BAR の読み・master.c の Command (04h) と cache line / latency (0Ch・0Dh) の読み書きの宛先がすべて 0:12.0)、GPU 無し (identify が -1、`pci_cfg_write16`/`write32` が 0 回)。test_gf_production.py:73-74 の `nm` 検査は `recover_gf_dev`・`recover_gf_second` (と `pci_locate_fn0`) だけを許し、ほかの ` recover_`・` kpeek_`・` trial_` の混入の禁止はそのまま |
| B6 | tools/tests/gfinit_owner_fixture_host.inc か test_gfinit_owner.py | claim の後の `gf_device.bdf` が `recover_gf_test_dev<<3` |
| B7 | gf5 の表示の試験 (test_gf5.py) | `target=0:12.0` / `0:13.0` が dev に追従する |
| C | docs/design/hwdev/gf/{production,resources}.md:5、design/kdebug/{gfinit_bus,recover_gf}.md、design/hwdev/gf/bus.md、decisions/realhw/GF_FEAT1〜5・GF_ACCEL の対象行、tasks/realhw/TASK_GEFORCE6200.md・TASK_HWDEV.md・PLAN.md、DEFERRED_TESTS の未実施の行 (GF-L2/L3/L5) | 「0:13.0」を「バス 0 の列挙で見つけた位置」に**書き換え** (注記で残さない)。合格済みの行 (GF-L4-*) は測った時の事実なので触らない。RA266.md §1-1 は構成の表 (§5) の採取の後に PM が更新 |

実装者: A・B は機械的で小さい → terra (1 枝)。C は PM。

## 5. 受入 → 試験・変異
host (B3、本物の A0・A1):

| fixture | 期待 |
|---|---|
| a. 0:12.0=10de:0221、0:13.0=USB (1033:0035 fn0) | dev=12、2 枚目なし |
| b. 0:13.0=10de:0221 | dev=13 |
| c. 無し | 13 (FALLBACK) を返し、2 回目の呼び出しも 32 回読む (覚えない) |
| d. 0:12.0 と 0:13.0 の両方 | dev=12、second=13 |
| e. a の後の 2 回目 | config の読みが 0 回 (覚えている) |
| f. 10de:0221 が 0:12.1 か 1:0.0 にだけ居る | 見つからない (13) |
| g. `pci_locate_fn0(1, id, …)` | バス 1 だけを読む (共通の口がバスの引数に従う) |

結合 (必須、0:13.0 と 0:12.0 の両方): B4 の資源の門が実測 (13) と swap (12) で通る、B5 で本番の identify・読み・書きが dev 12 に行き、GPU が無ければ書きが 0 回、B6 の owner の bdf、B7 の表示。test_gf_production・test_recover_gf・test_gfinit_bus は既存の case も緑。

変異 (`--mutate`、それぞれ 1 つ以上の fixture が落ちること):

| 変異 | 落ちる所 |
|---|---|
| M1 `recover_gf_dev` が常に FALLBACK | a・d・B5 の 12 |
| M2 後勝ち (`out->dev = dev` を無条件) | d |
| M3 `!gf_loc.found` の判定を消して毎回探す | e |
| M4 見つからないときも覚える | c |
| M5 走査を fn 0〜7 にする | f |
| M6 `pci_locate_fn0` が bus を無視して 0 を読む | g |
| M7 gfinit.c の `gf_device.bdf` の代入を消す | B6 |
| M8 gf5_test.c の表示を 13 の直値に戻す | B7 |
| M9 master.c の Command か cache line の宛先を 13 の直値にする | B5 の 12 |
| M10 nm の許可を ` recover_` 全体に広げる (復旧の関数を production に混ぜた版で) | test_gf_production.py:74 |

NP21/W: `make all` → 起動、`[pci] mech#1 absent`、`[geforce] boot_reserve fail stage=` が前と同じ段、`[geforce] pci` の行は出ない、kdebug の gfinit は前と同じように拒む。

実機 Ra266 の構成の表 (抜き差しはユーザー。各構成でノートが lspci・pcidump・起動ログ・割り当ての一覧を採る):

| 構成 | 並び (スロットの順は採取で埋める) | 期待 | 扱い |
|---|---|---|---|
| R1 今の構成 | HighPoint 0:12.0・GeForce 0:13.0・82557 0:11.0・Trident 内蔵 0:8.0・CS4231 | `[geforce] pci 0:13.0`、`boot_reserve fail` 無し、GeForce の表示 (カメラ)、foundation の bdf=0x68、GF5 の `target=0:13.0`、`ver` 一致 | **必須** (今回) |
| R2 入れ替え | USB 0:13.0・GeForce 0:12.0 | `pci 0:12.0`、bdf=0x60、`target=0:12.0`、USB の BAR が除外表に入る | ユーザーがスロットを入れ替えたときに採る |
| R3 GeForce を抜く | HighPoint・USB | identify が -1、98 の表示のまま起動、PCI の書き無し | 同上 |
| R4 GeForce と USB と HighPoint を全部 | スロットが足りるとき | GeForce を見つける、資源の門が通る | 同上 |

大きさ: vmkernel.lz4・geforce.mod・kdebug.bin・hwdev.mod の前後を報告。

## 6. 未決
なし (PM 決定: 実機を 0:13 に戻す作業は頼まない。今の構成が 0:13.0 なので R1 を必須、0:12.0 は host で必須、実機の 0:12.0 は R2)。

食い違い (参考、PM が §4 C で直す): RA266.md:54-57 の PCI 表 / production.md・resources.md:5 の「0:13.0 に限り」と実装 (resources.c は BDF を照合しない) の不一致 / gfinit_resources.h:6 の注記 / gf_accept.c:177-178 が FN でなく直値 0 / gf5_test.c:504 の直値。

## 確定 (2026-11-09、astra の第 2 回、PM 決定で反映)
- B5 の本番の書き込みの結合試験に hwdev/gf/master.c の実体を組み、Command と cache line / latency の宛先が 0:12.0 になることを確かめる (M9)。
- test_gf_production.py の nm 検査は追加する探索関数 (recover_gf_dev・recover_gf_second・pci_locate_fn0) だけを許し、従来の recover_* の混入禁止は維持する (M10)。
- PCI config の backend は機種・ホストブリッジの側、CPU の原始操作は arch の側 (arch/README.md:7-8)。backend の分離の実装は範囲外、pci_locate_fn0 は単一バスの探索として扱う。

## 確定の後の追記 1 (2026-11-09、os32powerpc の G4 の点検)
- `pci_locate_fn0` は CPU・機種に依らず G4 でもそのまま使える (G4 の `pci_cfg_read32` の backend が bus → host bridge を引ければ)。ただし呼び手の `RECOVER_GF_BUS 0U` は PC-98 の値。G4 の GeForce は AGP の host bridge の下で bus 番号は bus-range から決まる (0 とは限らない [未])。G4 で GeForce を探す段では bus を OF (AGP の host の bus-range) から渡すか、全部の bus を順に探す呼び手にする。今の recover・kdebug の経路は PC-98 用なので本設計は変えない。
