# 票 H4 — 配備マニフェストと世代の確認 (ホスト TDD の記録)

- 票: [`docs/archive/shell/TASK_H4.md`](../../docs/archive/shell/TASK_H4.md)
  (§2-1 形式 / §2-2 書く側 / §2-3 読む側 / §2-3-1 保証しないこと / §4 受入)
- 決裁: **D1 = 行指向の平文 (`.deploy/manifest.txt`)**、ユーザー決裁 2026-09-16
- 設計: レビュー 3 往復で Approve、`10a498a` で設計凍結
- 基点: `feat/gui` の `10a498a`
- 実行: `python3 -B tools/tests/test_h4_manifest.py --target --mutate`
  + `python3 -B tools/tests/test_hostdrv_manifest.py --mutate`
  (`make check-h4-manifest-host` が両方を回す)
- 日付: 2026-09-16

## 0. 正直に書く ([V4])

**RED は先に取った。** 実装前の `hsync.c` に対して、この票の受入をそのまま
書いた試験を回し、**330 件中 161 件が落ちる**ことを確かめてから実装へ入った
(§2)。ホスト側 (書く側) も同じで、実装前は `hd.manifest_line` が無くて
試験が途中で止まる = RED だった。

ただし**完全な先出しではない** 2 点を書いておく:

1. ゲスト側の RED 測定では、まだ存在しない内部変数 (`g_man_valid` など) を
   仮置きした写しを使った。振る舞いを見る検査 (表示・終了コード・書き込み
   回数) はすべて本物の `hsync.c` に当てているが、**内部変数を読む検査だけは
   RED の時点では意味を持っていない**。
2. 受入表に無い検査 (上限ちょうどの名札が通る、H1〜H3 の非退行) は実装と
   同時に書いた。

その埋め合わせとして、**変異試験で 24 件すべての赤を取った** (§4)。
落ちなければ「試験が規則を見ていない」ので、そこは数えて報告する。

## 1. 何を確かめる試験か

| ファイル | 中身 |
|---|---|
| `tools/tests/h4_manifest_host.c` | 実物の `userland/system/hsync.c` を 1 行も写さず `#include` し、KernelAPI だけを贋物に差し替える。贋 FS の `/host/.deploy/manifest.txt` に**任意の本文**を置けるので、票が挙げた壊し方をそのまま注入できる |
| `tools/tests/test_h4_manifest.py` | 上のビルドと実行、`--target` のクロスコンパイル確認、`--mutate` の否定側、**書く側と読む側が同じ名札を指していること**の静的突き合わせ ([C4]) |
| `tools/tests/test_hostdrv_manifest.py` | 実物の `tools/hostdrv_deploy.py` を import し、`HOSTDRV_DIR` / `PROJ_DIR` / 配備定義だけを一時ディレクトリへ向ける。**実ファイル系 (`/mnt/c/os32`) には触らない** |

### 偽の緑を踏まないための観測窓

「その経路が実際に走ったか」を**文言ではなく数**で見る。

| 観測窓 | 何を押さえるか |
|---|---|
| `fk_write_calls` / `fk_rename_calls` / `fk_mkdir_calls` が 0 | **「1 件も書かない」** (M6b / M6c / M8 / M11)。「エラーが出た」だけでは、書いたあとに出たのか前に出たのか分からない |
| `g_content_compares` | **内容比較が走った** (M10)。名札の CRC で省いていたら増えない |
| `g_man_lookups` | **名札の表を実際に引いた**。集計が 0 なのは「引いて無かった」のか「引いていない」のか、これで分かれる |
| `g_man_valid` / `g_man_count` | 壊れた名札を**捨てた** (半端な表を残していない) |
| `fk_log` の中身 | 表示が出たこと (`DEPLOY build=…` / `manifest invalid:` / `reason=…`) |
| 書く側: `open` で開いたパスの記録 | **最終パスを書き込みで開かない** = 一時ファイル + 置き換え。「落ちたあと名札が無い」だけでは直書きと区別がつかない (後始末で消えるので緑になる) |

最後の 1 つは実際に踏んだ穴で、最初の変異 (`no_temp_file`) が **GREEN のまま
通ってしまった**。そこで観測窓を足して赤にしている。

## 2. RED (実装前、`10a498a` の `hsync.c` に対して)

```
330 checks, 161 failures
```

代表的な落ち方:

```
  FAIL M6 [形式版が違う] manifest invalid を表示する
  FAIL M6b [形式版が違う] reason=manifest_invalid
  FAIL M12 [形式版が違う] 絞り込みなら成功する
  FAIL M7 一致なら同期する            (--expect-build が unknown option)
  FAIL M9 名札どおりなら manifest_extra=0
```

書く側:

```
  ok   配備そのものは成功する
  FAIL 名札が書かれる
  FAIL **既にある名札を消す**
  AttributeError: module 'hostdrv_deploy' has no attribute 'manifest_line'
```

## 3. GREEN (現状)

```
MANIFEST PASS (名札 .deploy/manifest.txt, format=1, HS_MAN_MAX=320,
               HS_MAN_PATH_CAP=NAME_CAP=64, man ページに記載)
HOST GNU89 -Werror COMPILE PASS (tools/tests/h4_manifest_host.c)
358 checks, 0 failures
EXIT h4_manifest_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/system/hsync.c)
```

```
42 checks, 0 failures        (tools/tests/test_hostdrv_manifest.py)
```

途中で実際に踏んだ RED が 1 件ある。**名札そのものも同期対象**なので、
名札を置いた配備元では `copied` が 1 増える (`a`, `b`, `.deploy/manifest.txt`
の 3 件)。試験側が 2 件と決め打っていて 21 件落ちた。除外の規則を足して
黙らせるのではなく、**試験の期待を実際の振る舞いへ合わせた** (H1 / H2 / H3 の
規則を変えないため。詳細は §5)。

## 4. 変異 (否定側) — すべて赤

### 4-1. 読む側 (`userland/system/hsync.c`) — 19 件

| 変異 | 崩した規則 | 結果 |
|---|---|---|
| `expect_build_ignored` | `--expect-build` の門を素通り | RED |
| `gate_not_wired` | 門を `main` に繋がない | RED |
| `invalid_manifest_is_match` | **壊れた名札を「一致」と扱う** (往復 1 所見 2) | RED |
| `missing_manifest_is_match` | 名札が**無い**のを「一致」と扱う (M6c) | RED |
| `absent_refused_when_narrowed` | 名札が無いときだけ絞り込みでも断る (**直す前の非対称**) | RED |
| `mismatch_excused_when_narrowed` | 不一致を絞り込みで見逃す (確かめた結果と確かめられないを混ぜる) | RED |
| `narrowed_sync_refused` | 絞り込みでも断る (往復 2 所見 1 / M12) | RED |
| `trust_manifest_skip_compare` | **名札を信じて内容比較を省く** (M10) | RED |
| `broken_manifest_used` | 壊れた名札を捨てず半端な表で使う | RED |
| `too_many_entries_truncated` | 大きすぎる名札を切り詰めて使う (M9b) | RED |
| `count_mismatch_ignored` | `count` より行が多いのを見逃す | RED |
| `build_compare_prefix` | `build` を前方一致で見る | RED |
| `build_compare_case_insensitive` | `build` の大小を無視する | RED |
| `extra_not_counted` | `manifest_extra` を数えない (M9) | RED |
| `manifest_self_counted` | 名札自身の写しを extra に数える | RED |
| `deploy_line_dropped` | 世代の表示を落とす | RED |
| `invalid_line_dropped` | 壊れた名札を黙って捨てる | RED |
| `crc_uppercase_accepted` | CRC の大文字を通す | RED |
| `absolute_path_accepted` | 絶対パスを読む | RED |

`absolute_path_accepted` は**最初 no-op だった**。`p[0] == '/'` を外しても
空要素の検査が同じものを弾いていたので、規則が 2 重に守られていて変異に
ならなかった。両方を外す形に直して赤を取っている。

`gate_not_wired` も最初は「コンパイルが通らない」赤 (未使用関数) で、
規則を見た赤ではなかった。`(void)man_gate();` を残す形に直してある。

### 4-2. 書く側 (`tools/hostdrv_deploy.py`) — 5 件

| 変異 | 崩した規則 | 結果 |
|---|---|---|
| `stale_manifest_kept` | 失敗しても古い名札を消さない (§2-2) | RED |
| `no_temp_file` | 一時ファイルを使わず最終パスへ直書き | RED |
| `no_manifest_ignored` | `--no-manifest` でも書く (M4) | RED |
| `count_not_lines` | `count` を行数と別に数える (M1) | RED |
| `space_in_path_allowed` | パスの空白を見逃す (§2-1) | RED |

## 5. 迷った点と決めたこと

### 5-1. 名札そのものが同期対象になる

`/host/.deploy/manifest.txt` は `hsync` の列挙に現れるので、全体同期で
`/.deploy/manifest.txt` へ写る。**除外の規則は足していない** — 票は除外を
求めておらず、H1 / H2 / H3 の規則を変えない方針だから。

ただし**名札自身の写しは `manifest_extra` に数えない**。名札は自分の CRC を
自分に書けない (中身が決まる前に CRC は出せない) ので、数えるとまっさらな
ゲストで必ず `manifest_extra=1` になり、本当の食い違いが埋もれる。
この判断は変異 `manifest_self_counted` で固定してある。

### 5-2. 「確かめられない」ときの扱い (PM 決裁で訂正)

初版は票の字義どおり「名札が**無い**ときは絞り込みでも断る」にしていたが、
M12 (壊れている + 絞り込み → 続行) との非対称が筋の通らないものだった。
**PM 決裁 2026-09-16 で統一**:

| 状況 | 全体同期 | 絞り込み |
|---|---|---|
| 読めて**不一致** | 断る (`build_mismatch`) | **断る** |
| **壊れている** | 断る (`manifest_invalid`) | 表示のみ・続行 |
| **無い** | 断る (`manifest_absent`) | 表示のみ・続行 |

**「確かめた結果おかしい」と「確かめられない」は別。** 前者は絞り込みでも
断る (この旗の本体)。後者は「名札はルートの世代を表すもので、絞った範囲の
正しさを保証しない」が壊れている場合と無い場合に等しく当たるので、扱いを
揃える。変異 `absent_refused_when_narrowed` (直す前の非対称) と
`mismatch_excused_when_narrowed` (両者を混ぜる) の 2 本で固定した。

語は `manifest_absent`。§2-4 の別票の集計名 `manifest_missing` と紛れない
ようにしてある。

### 5-3. `--no-manifest` と `--tag` でも古い名札は消す

票は「1 件でも失敗したら消す」としか書いていないが、**古い名札を残すほうが
危ない**。新しいファイル + 古い名札が残ると `--expect-build <古い ID>` が
**一致**してしまい、H4 が防ぐはずの事故がそのまま裏返って起きる。
`--tag` の部分配備も同じで、一部だけ入れ替えた配備元を 1 つの版として
名乗らせない。

### 5-4. 表のメモリ上限 — `HS_MAN_MAX 320`

実測 (2026-09-16): 配備定義が展開するファイルは**約 200 件**
(この worktree では `apps` / `game` の submodule が未取得で 79 件、
実際の HostDrv には 249 個のファイルがある)。320 は約 6 割の余裕。
表は `320 x (64 + 4 + 4 + 4) = 24,320 バイト` の BSS で、既に確保している
コピー用バッファ (64KB) より小さい。パスの上限は票 §2-3 の指定どおり
`NAME_CAP` (64) に揃えた — 実測の最長は 29 文字 (`usr/bin/sqlite_standalone.bin`)。

名札の**本文**はコピー用バッファ (`file_buf`) を借りて読む。名札を読むのは
同期を始める前の 1 回だけで、そのときコピー用バッファはまだ使っていない。

## 6. この試験が見ていないもの

- **ゲスト実機 (NP21/W) での確認**。票 §4-2 の 4 項目は動かしていない。
- ホストが実際に落ちた場合 (§2-3-1 の非原子性)。試験は「名札が古い」状態を
  直接作って `build_mismatch` を確かめているだけで、電源断そのものは再現
  していない。
- `make deploy` を実際に回した名札。書く側の試験は一時ディレクトリの
  合成配備定義で回している。
- `manifest_missing` (名札にあるのに配備元に無い)。票 §2-4 で**別票**。
