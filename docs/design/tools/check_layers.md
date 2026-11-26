# tools/check_layers.py — 層の番人 (LAYER_GUARD_DESIGN: 共通・arch・platform・装置の依存の検査)

> 状態: **現行 (2026-11-17)** — 出所: 機種依存の分離の作り直しの最初の段 (ユーザー決定 2026-11-17「共通ソースが機種ヘッダを読む数 0・ビルド 1 系統・入口の骨格 1 本」)。製品のコードは触らない。
ソース: `tools/check_layers.py`、基準 `tools/layers_baseline.json`、試験 `tools/tests/test_layers.py`、登録 `tools/checks/layers-host.py`・`layers-x86.py`・`layers-ppc.py`

## 1. 目的

共通のソースが機種のヘッダを読む数を、製品の像の依存ファイル (`.d`) から機械で数え、今の違反を基準ファイルに固定する。違反が 1 件でも増えたら CI が落ち、減ったら基準ファイルを同じ変更で減らさないと落ちる (戻り止め)。後の段はこの基準ファイルを空にしていく。

根拠 (origin/main `75c7c6f69`): PowerPC の製品の像 (KBOOT_SH=1) は `sys/platform/pc98/pc98.h` を 42 TU が読む。当時あった compile だけの計測は CI で回っておらず、2026-11-17 に `sys/kern/ver_collect.c` が `x87.h` を読み始めたのを誰も止めなかった。x86 の像には同種の検査が無い。

## 2. 入力

像を組んだ後に残る `.d` (gcc の `-MMD`) だけを読む。ソースの `#include` を自前で解決しない (`-I` の順で同名のヘッダが入れ替わるため)。

| 像の名前 | `.d` の根 (`build/out/` の下) | 前提 |
|---|---|---|
| `x86-pc98` | `obj` `cd` `cirrus` `fep` `fm` `geforce` `hwdev` `kcg` `kdebug` `lan` `pcm` `sqlite-proto` `trident` `v86` | `make all` |
| `ppc-powermac` | `ppc-kboot` (CI が組む 4 像の和集合。直下の `shell`・`neg*` は、CI が組まない像の置き場なので数えない) | `make ppc-ci-image` |

`.d` の先頭の前提をその TU のソース、残りをその TU が読んだファイルの集合とする。TU は重複を除いたソースで数え、`build/out/` の下に生成されたソース (部品の import の表など) は見ない。ソースが木に無い `.d` (消した・改名したソースの残り) は数えない。パスは作業木の相対に正規化する。作業木の外は見ない。`build/out/` の下の生成ヘッダは読む側としても読まれる側としても見ない。ソースが `build/out/**/gen/kapi/kapi_generated.c` のときは中身はその場所から読み、名前は `sys/kapi/kapi_generated.c` として扱う。

**違反は「読んだ側のファイル → 読まれたヘッダ」の辺で数える** (TU ごとの平たい集合では数えない)。契約の実装 (`arch_io.h` など) が内側で読む専用ヘッダを、共通のソースの違反にしないためと、直す場所を 1 行で指すため。辺の作り方:

1. TU の集合の各ファイル (ソース自身を含む) の `#include "..."` と `#include <...>` の行を読む (コメントは除く、条件コンパイルは評価しない)。`<...>` も読むのは、`-Iplatform/$(PLATFORM)` で機種のヘッダを引く書き方 (`<platform_ime_render.h>`・`<platform_display_fallback.h>`) があるため。集合は作業木の中だけなので、システムのヘッダは辺にならない。
2. 読まれた名前を、その TU の集合の中で解決する。`"..."` は読んだ側のディレクトリからの相対がその集合にあればそれ。無ければ (`<...>` は最初から)、集合の中でパスの末尾が一致する物。2 つ以上一致したら全部を辺にする (多めに数える側に倒す)。集合に無い名前は辺にしない (その TU では条件で外れている)。
3. 同じ辺が複数の TU から出たら 1 件にまとめ、その辺が出た TU の数を付ける。

## 3. 持ち主の分類 (パスだけで決める)

| 持ち主 | パス |
|---|---|
| `arch:<a>` | `arch/<a>/**` |
| `platform:<p>` | `platform/<p>/**` |
| `device` | `drivers/**` `hwdev/**` `kdebug/**` |
| `named:<機種>` | `include/` にある機種名のヘッダ。表は道具の定数に 1 か所: `pc98.h`・`pegc.h`・`wab_xe10.h` は `pc98`、`v86_call.h` は `x86`。`sys/include/text_screen.h` (文字画面の口。旧名 tvram.h、中身は機種に依らず両方の機種が実装する) と `sys/kern/mm/paging_lowmap.h` (共通の mm が実装する低位の写像の口。旧名 include/legacy_lowmap.h) は表に入れない。`sys/include/kbd.h` (共通のキーの値) と `sys/include/legacy_guest.h` (提供者を 1 つ link する契約、無い像は `platform/none`) は共通 |
| `common` | 上のどれでもない作業木の中のファイル (`kernel` `init` `exec` `mm` `fs` `gfx` `net` `kapi` `input` `lib` `include` `sdk` ほか) |

契約の実装の固定名 (`arch/<a>/arch_*.h`、`platform/<p>/platform_*.h`) は、その像の arch・platform の物である限り違反にしない (`arch/README.md` の決まり)。`arch_` で始まらない名前は、両方の arch に同じ名前で対になっていても固定名ではないので R1 に数える (例: `sys/platform/powermac/ppc_kernel.c → sys/arch/ppc/kernel_boot.h`)。`platform/none` はどの像にとっても別の機種ではない。

## 4. 違反の規則

辺の「読んだ側のファイルの持ち主 → 読まれたヘッダの持ち主」で判定する。1 件 = (像, 規則, 読んだ側, ヘッダ) の組。

| 規則 | 読んだ側 | ヘッダ | 意味 |
|---|---|---|---|
| R1 | common | `arch:*` (固定名を除く) | 共通が CPU の専用ヘッダを読む |
| R2 | common | `platform:*` (固定名を除く) | 共通が機種の専用ヘッダを読む |
| R3 | common | `device` | 共通が装置のヘッダを直に読む |
| R4 | common | `named:*` | 共通が機種名のヘッダを読む |
| R5 | device | `arch:*` (固定名を除く) | 装置が CPU の専用ヘッダを読む |
| R6 | 何でも | 像と違う arch・platform の物 (`arch:<他>`・`platform:<他>` (none を除く)・`named:<他>`)。固定名も含む | 像に別の機種の物が混ざる |
| R7 | `arch:*` | `platform:*` (固定名を除く) | CPU の層が機種を知っている |

`named:<機種>` が像と違うかは、`pc98` は platform、`x86` は arch と比べる。1 つの組が R6 と他の規則の両方に当たるときは R6 だけに数える。像の TU のソースが別の arch・platform の下にあるときも R6 (読んだ側はそのソース、ヘッダの欄は `-`)。

## 5. 基準ファイル

`tools/layers_baseline.json`。`{"x86-pc98": [[規則, 読んだ側, ヘッダ], ...], "ppc-powermac": [...]}`、整列済み、1 組 1 行。

- 観測にあって基準に無い組 → `new violation: ...` で失敗。
- 基準にあって観測に無い組 → `stale baseline (remove): ...` で失敗。
- `--write-baseline` は観測した像の分だけを書き直す (もう一方の像の分は保つ)。片方の像だけ組んだ木では `--image <像>` を付ける (付けないと、組んでいない像の `.d` が無いので失敗して何も書かない)。
- 終了時に像ごと・規則ごとの辺の件数と、TU の数を 1 行ずつ出す (後の段の進み具合の数字)。`--verbose` で辺ごとに、その辺が出た TU の数を出す。
- `.d` が 1 本も無い根は失敗 (像を組んでいないのに合格にしない)。

## 6. ファイルの割り当て

| ファイル | 中身 |
|---|---|
| `tools/check_layers.py` | 分類・規則・基準との照合・`--write-baseline`・`--image`・`--root`。標準ライブラリだけ |
| `tools/layers_baseline.json` | 基準 (実装の段で両方の像を組んで書く) |
| `tools/tests/test_layers.py` | 偽の作業木と偽の `.d` で R1〜R7・固定名の例外・契約の実装の内側の辺が違反にならないこと・R6 の優先・相対と末尾一致の解決・生成ソースの読み替え・new と stale・空の根を確かめる。`--mutate` で規則ごとに RED |
| `tools/checks/layers-host.py` | 上の試験の登録 (`needs=[]`) |
| `tools/checks/layers-x86.py` | `x86-pc98` の実測 (`needs=['make:all']`) |
| `tools/checks/layers-ppc.py` | `ppc-powermac` の実測 (`env=['ppc-cross']`、`needs=['make:ppc-ci-image']`、道具が無いときの SKIP・FAIL は `tools/checks/ppc_registered.py` と同じ決まり) |

`basis` は `['LAYER_GUARD_DESIGN §4', 'LAYER_GUARD_DESIGN §5']`。`layers-host` の試験は偽の木 (TemporaryDirectory) だけを読むので、`tools/checks/run.py` の `PATH_AUDIT_ALLOW` に道具と試験の 2 行を載せる。

## 7. 入れた時の基準値と確かめたこと

基準を取った木は origin/main `75c7c6f69` (両方の像を組んだ後)。

| 像 | TU | 辺 | R1 | R2 | R3 | R4 | R5 | R6 | R7 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `x86-pc98` | 636 | 118 | 25 | 6 | 42 | 14 | 20 | 0 | 11 |
| `ppc-powermac` (4 像の和集合) | 266 | 74 | 25 | 17 | 13 | 0 | 0 | 9 | 10 |

- `sys/kern/ver_collect.c → sys/arch/x86/kernel/x87.h` は `x86-pc98` の R1 に載る。`sys/arch/x86/x86_flags.h`・`sys/arch/ppc/ppc_oea.h` のように契約の実装の内側だけで読まれるヘッダは、共通のソースの違反に載らない。
- `ppc-powermac` の R6 には `sys/platform/pc98/pc98.h` を読む辺が 3 件ある (`sys/hal/gfx/gfx.h`・`sys/hal/gfx/gfx_internal.h`・`sys/platform/pc98/pegc.h`)。
- 基準から 1 行消すと `new violation`、基準に無い 1 行を足すと `stale baseline (remove)` で rc≠0 (`test_layers.py` の `test_baseline_ratchet`)。
- `test_layers.py --mutate` は規則ごと・解決の決まりごと・数えない場所ごと・戻り止めごとに実行時 RED (25 本)。

`layers-x86` は build.yml の `make check-fast` で、`layers-ppc` は check.yml の ppc-compile (`--run all --env ppc-cross`) で回る (check.yml の static-check は像を組まないので回らない)。

違反を直した段は、同じ変更で `python3 -B tools/check_layers.py --write-baseline` を両方の像を組んだ木で回し、減った基準ファイルをコミットする。

## 8. 違反を 0 にした時の直し方 (2026-11-17)

基準値は両方の像で 0 (`tools/layers_baseline.json` の 2 つの一覧が空)。新しい違反は検査が止める。足す時は次の形で直す (作業の手順はスキル `.claude/skills/os32-arch-sep`)。

| 違反の形 | 直し方 | 例 |
|---|---|---|
| 置き場と持ち主の食い違い (機種のソースが共通の置き場にある、装置の接着が共通にある) | `docs/SRC_MOVE.md` の手順で持ち主の置き場へ移す。中身は変えない | 入口 `init/kernel.c` → `platform/pc98/`、V86 → `platform/pc98/legacy/`、`net/nic_*.c` → `drivers/net/`、`kernel/module/module.c` → `sys/kern/kdebug/kdebug_module.c`、`fs/fat/fatfs/diskio.c` → `sys/dev/block/fat_diskio.c` |
| 共通が装置の関数・型を使う (R3) | 共通が要る宣言だけを `include/<名>_contract.h` に置き、装置のヘッダがそれを include する (宣言は 1 か所)。共通は contract だけを読む | `geforce_contract.h`・`kcg_contract.h`・`snd_contract.h`・`lan_contract.h`・`atapi_contract.h`・`serial_contract.h`・`kbd_contract.h`・`fdc_contract.h`・`v86_contract.h` |
| 共通・arch が platform のヘッダを読む (R2・R7) で、機種ごとに実装を差し替える物 | 固定名 `platform_<名>.h` にする (`-Iplatform/$(PLATFORM)` が実装を選ぶ) | `platform_display_fallback.h`・`platform_link_inline.h`・`platform_ime_render.h`・`platform_pic.h`・`platform_of_layout.h`・`platform_kboot_layout.h` |
| 共通が arch の関数を使う (R1) | 契約 `sys/include/cpu.h` などに宣言を置く | `arch_fp_present` |
| 共通が機種名のヘッダの定数を使う (R4・R6) | 共通の側に機種に依らない名前で値を持ち、機種の側のソースに `_Static_assert` で一致を固定する | `GFX_BB_PLANES` (`sys/platform/pc98/gfx_init.c`)、`GFX_PALETTE_COMPONENT_*` (`sys/platform/pc98/video/backend_pegc.c`) |
| ドライバが arch のヘッダを読む (R5) | 共通の口を読む (`sys/kern/irq/irq.h`・`sys/include/irq_line.h`・`kernel_tick.h`) | ドライバ 10 本の `idt.h` |

contract のヘッダで消えるのは「共通が装置のヘッダを読む」ことで、リンクの時に呼ぶ関数の名前 (`geforce_foreground_poll`・`fdc_media_gen` など) は装置の名前のまま残る。実行時に装置を差し替える口 (HAL) にするのは、この検査の範囲の外。

### 8-1. 従来の検査の範囲外

ユーザーランドの依存は見ない。違反を直すことはしない。装置どうしの機種の区分 (`sys/dev/base/dev.c` が PowerPC の像で PC-98 の `sys/dev/block/fdc.h` を読む、など) は `drivers/` をまとめて `device` にしているので数えない — `drivers/` の置き場を機種で分ける段で R6 に入る。`sys/include/memmap.h` の中身 (PC-98 の番地の地図) も、ヘッダの置き場の検査では数えない。`build/out/ppc-kboot` の下に CI が組まない像 (`KBOOT_SHELL=1`・`NEGATIVE`) を手元で組んであると、その像の辺も数える。

## 9. 構成ごとの include 直辺

初回は PC-98 カーネルと部品、PowerMac core の C とプリプロセスするアセンブリについて、その版の製品コンパイルと同じ定義・探索順・生成入力で有効になった直接の「読み手 → 実際に開いたヘッダ」を得る。入れ子、条件付き、マクロによる include も解決後のファイルで判定する。翻訳単位ごとの依存集合だけでは直辺を特定できない。必須入力で親子または所属を確定できなければ構成と入力を示して ERROR。他構成・他言語は未観測と明記する。

§2・§3・§8-1 の依存集合、パスによる分類、手元の追加構成は本節の入力に用いない。従来の規則から引き継ぐ判定は §4 だけとする。製品のコンパイラが供給する外部ヘッダはツールチェーン所属として記録し、リポジトリ内の層規則の対象から外す。その配下で再び製品ヘッダを開いた場合も観測は続ける。生成ヘッダには生成規則が宣言する出力の所属・対象・役割を適用し、必須構成で特定できなければ ERROR とする。

所属宣言の kind・target・header_role で §4 の R1〜R7 を適用する。翻訳単位と辺の両端で別対象の混入（R6）を先に拒む。固定名の例外は対象が一致し、CPU または機種が同じ契約名で供給すると宣言した fixed_contract に限る。named は通常の内部ヘッダと同様に専用である。違反は構成・規則・辺を示して失敗する。許容件数は設けない。

## 10. 最終リンクの採用とシンボル解決

初回の単位は x86 PC-98 のカーネルと各製品部品の最終リンク、および PowerMac の 6 構成中 core の最終リンク。製品のリンク入力から「翻訳単位 → オブジェクト → 採用先」を得る。直接入力の所有と対象をリンク前にも照合し、別 CPU・別機種・構成で選ばない装置が混ざれば、その入力と採用先を示して失敗する。書庫の抽出メンバーと部分リンク内部の間接辺は次回以降の対象とし、初回の証跡には境界となった入力名を残す。

契約の出所は、所属の正本で共通の契約宣言として指定したヘッダ（装置の *_contract.h、CPU の共通の口など）と、対象が一致する fixed_contract のヘッダ（platform_*.h など）とする。各構成の製品定義で有効な、当該ヘッダ自身が宣言する外部リンケージの関数・データについて、コンパイラが付けるリンク名を得る。許す構成はヘッダの対象・宣言の有効条件・実装の製品選択がすべて一致する構成に限る。解決した定義がこの宣言と型・リンク名で一致する場合に契約を通る辺とする。アセンブリの供給者は、その所有者が指定した契約ヘッダの宣言を供給側の型の証拠とし、型とリンク名を照合する。宣言がなければ違反とする。それ以外の定義は供給者の内部として判定する。ヘッダ名の形や外部に見えるシンボルというだけで公開扱いしない。

参照側がアセンブリの場合は、製品入力で使う契約を明示する。GAS/NASM の extern と C の宣言は同じ契約から導く。製品の前処理で extern の出所と対象を確かめ、同じ構成の C 前処理から得た型・リンク名を実際の参照と供給者に照合する。引数位置・非復帰分岐は生成命令と起動で確かめる。

最終リンクの解決情報で「参照元 → シンボル → 選ばれた供給者」を確かめる。共通から CPU・機種・装置の内部、装置から CPU 内部、CPU から機種内部への直結を拒む。共通の契約を構成が選んだ実装へ結ぶ辺、機種初期化から採用装置への登録は許す。意図した stub は契約と適用構成を宣言して選ぶ。強い必須参照の未解決・競合、弱い代替による必須供給者の欠落は失敗。部品のロード時 import と未解決を許す弱参照は、部品の公開 import 契約または任意機能の宣言に限る。観測に必要なリンク証跡が取得できなければ ERROR。検査用にソース別の配線表を複製しない。
