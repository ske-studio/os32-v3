# sh_truncation TDD — シェルが入力を黙って切り詰める経路

票: [`docs/archive/shell/TASK_SH_TRUNCATION.md`](../../docs/archive/shell/TASK_SH_TRUNCATION.md)
基点: `feat/gui` = `a4f5429` (段 1) / 段 4 は `3fd4da5`
試験: `make check-sh-truncation-host` (= `python3 -B tools/tests/test_sh_truncation.py`)
    否定側もまとめて: `python3 -B tools/tests/test_sh_truncation.py --mutate`

## 0. この段 (§5 の段 1「足場」) で何をしたか

**挙動は 1 つも変えていない。** やったのは 3 つだけ:

1. `userland/shell/main.c` の `try_exec` / `try_exec_from_path` / `run_cmd_internal`
   (と `has_ext` / `has_slash` / `sh_is_cui_only`) を `userland/shell/sh_exec.inc` へ移し、
   `main.c` が `#include` する。トークン列は切り出す前と同一で、
   `userland/shell/` の `.o` は `cmd_base.o` の `__TIME__` 以外すべて md5 が一致する (§3)。
2. ホスト試験 `tools/tests/sh_truncation_host.c` + `test_sh_truncation.py` の骨を置いた。
   **登録表も `execute_command` も実物**を通す (`main.c` を丸ごと `#include` する)。
3. 票 §2-3 が要求する洗い出し表 (§2) を作った。

### RED → GREEN ではない — 「現状の記録」

段 1 の試験は**今の挙動を固定するだけ**で、直すべき欠陥を赤で落としていない。
反転させる予定の検査には `[EXPECTED_TO_CHANGE]` と書いてある。

| 検査 | いま記録している挙動 | 段 2 でどうなるか |
|---|---|---|
| `1c` / `1d` | `if <A> == <B>` の両辺が 256 文字以上で先頭 255 文字が同じとき、**条件が真になって右辺のコマンドが走る** | 断る。右辺を実行しない。上限超過を報告する (票 U1) |
| `1e` | 同じ反例の `!=` が**偽になって右辺が走らない** | 断る (走らない点は同じでも、理由がメッセージで分かる) |
| `2a`〜`2g` | 登録表・`execute_command` が実物であること、255 文字以下の比較は正しいこと | 変えない (足場の前提なので段 2 以降も緑のまま) |

段 2 で `1c` / `1d` / `1e` を票 §4 の U1 (「実行しない」「上限超過を報告する」
「スクリプト中なら後続行も実行しない」) の形へ書き換える。

### 足場の作り
`tools/tests/sh_shell_host.c` は `execute_command` を**スタブ**にしていて、
`exit` / `source` / `goto` を文字列で直接見ている。切り詰めの経路 (`execute_command`
→ `execute_single` → `parse_args_and_glob` → `run_cmd_internal` → 登録表 → `cmd_if`)
はそこでは通らないので、この票では別に `sh_truncation_host.c` を置いた:

- `#include` する実物: `main.c` (→ `sh_exec.inc` / `sh_args.inc` / `sh_launch.inc` /
  `sh_pipe.inc` / `sh_ls.inc`)、`cmd_base.c` / `cmd_dir.c` / `cmd_env.c` /
  `cmd_file.c` / `cmd_fs_shared.c` / `cmd_mnt.c` / `cmd_script.c` / `cmd_sys.c`、
  `sh_redraw.inc`
- 置き換えるのは KernelAPI 表 (贋 FS・贋ヒープ・`launch_req` の記録) と、
  `ui.c` の 3 本 (`shell_run` / `hist_save` / `hist_load`)、`sdk/crt/help.c` の 2 本だけ
- `cmd_filer.c` は `filer_draw.c` (GFX) を丸ごと引くので取り込んでいない。
  `SHELL_AS_APP` では `sh_is_cui_only` が `filer` を先に断つ。T16 / T23 は段 4 の担当。
- 子が起きたかどうかは `launch_req` の呼び出し回数で見る (GUI 外を模して
  `OS32_ERR_INVAL` を返すので、実際にプログラムは動かない)。

所要時間: 約 1.1 秒 (ホスト gcc ILP32 と i386-elf-gcc の 2 回のコンパイル込み)。

## 0-2. 段 2 (§5 の段 2「T1 単独 + §2-1 の中断規則」) — RED → GREEN

基点: `feat/gui` = `b3954f1`。

### 何を変えたか

| ファイル:行 | 変更 |
|---|---|
| `userland/shell/cmd_script.c` `strip_quotes` | 溢れたら**黙って切らずに `-1` を返す**。長さは**クォート除去後**で数える (`sh_args.inc` が既にクォートを落としているので、`strip_quotes` は取りこぼしの掃除役) |
| `userland/shell/cmd_script.c` `cmd_if` | `v1` / `v2` の幅を `IF_VALUE_MAX` (shell.h、[C4]) に。どちらかが収まらなければ**比較せずに断る** — `sh_refuse("if: left value" / "if: right value", IF_VALUE_MAX - 1)` の赤字 1 行を出し、コマンドは実行しない |
| `userland/shell/cmd_script.c` `script_exec` | `execute_command` の後に `sh_refused_take()` を見て、立っていたら `script_abort_flag` を立てて打ち切る (§2-1)。戻り値で「断って打ち切った」を親へ返す |
| `userland/shell/cmd_script.c` `script_source_file` | 断って打ち切ったら `SCRIPT_ERR_REFUSED` (-2) を返す |
| `userland/shell/cmd_script.c` `cmd_source` / `script_source_profile` (新規) | `source` は `-2` を受けたら印を立て直して親のスクリプトも打ち切る。起動スクリプトは**印を立て直さず**メッセージを出して続行する (R2) |
| `userland/shell/main.c` | 印の実体 `sh_refused_flag` と `sh_refuse()` / `sh_refuse_mark()` / `sh_refused_take()`。`execute_command` は `execute_command_line` を包む形にして、**いちばん外側の呼び出しだけ**が入口で印を消す |
| `userland/shell/main.c` `execute_command_line` | 既にあった `env_expand` 溢れの断りにも印を立てる (同じ §2-1 の規則) |
| `userland/shell/sh_exec.inc` | `foo.sh` を直に打った経路 (`run_cmd_internal`) も `-2` を印に変換して親へ伝える |
| `userland/shell/ui.c` | 起動スクリプト 2 本を `script_source_profile()` 経由に |
| `userland/shell/shell.h` | `IF_VALUE_MAX` / `SCRIPT_ERR_REFUSED` / 印の宣言 (寿命の規則もここに書いた) |

**印を消すのは 1 か所だけ** — いちばん外側の `execute_command` の入口。入れ子
(`if` / `time` が組み立てた行) で消すと内側の断りが `script_exec` に届かず、
段の後ろが `time …` のパイプで取りこぼす (変異 `nested_clear` が実際に落ちる)。
`script_exec` は 1 行ごとに読んで消す。対話 / `rshell` は誰も読まないので、
断った行の**次の**行は今までどおり動く。

### 段 1 の `[EXPECTED_TO_CHANGE]` をどう反転させたか

| 段 1 の検査 | 段 1 が記録した挙動 | 段 2 の検査 |
|---|---|---|
| `1c` | 条件が真になって右辺が走る | `1c` 右辺のコマンドを実行しない (`==`) |
| `1d` | 走った証拠 (`command not found`) | `1d` 何が上限を超えたか + 上限を 1 行で報告する |
| `1e` | `!=` が偽になって走らない | `1e` / `1f` 走らないのは同じだが、理由が赤字 1 行で分かる |
| `2a`〜`2g` | 足場が実物であること | そのまま (`2f` に「断りも出さない」を足しただけ) |

段 1 の記録は §0 に残してある (履歴)。

### RED → GREEN

段 2 の試験は 64 件。**GREEN 64 / FAIL 0** (`EXIT sh_truncation_host=0`)。

RED の記録は「段 2 の前の姿」に戻した版で取った (`--mutate` の否定側がそのまま
pre-fix の再現になっている。全ソースを段 1 の状態へ戻すと `SCRIPT_ERR_REFUSED`
などが無くてコンパイルが通らないので、規則ごとに 1 つずつ壊す形にした):

| 変異 (= 壊した規則) | 落ちた検査 |
|---|---|
| `compare_truncated` (段 2 の前の `strip_quotes`。黙って 255 で切る) | **24 件** 1c 1d 1f 1h 1i 1j 1k 3d 3g 4b 4c 4e 4f 5a 5b 5c 5d 6b 6c 6g 7b 8a 8b 8c |
| `no_mark` (断っても印を立てない = §2-1 が無い) | **11 件** 1j 1k 4c 4f 5b 5c 5d 6c 6g 8b 8c |
| `no_clear` (印を消し忘れる) | **8 件** 4b 4e 4h 4i 6d 6e 7d 7e |
| `nested_clear` (入れ子でも消す) | **1 件** 6g |
| `abort_interactive` (対話でも打ち切る) | **18 件** 1f 1h 1k 3a 3d 3e 3g 4b 4e 4g 4h 4i 6d 6e 7a 7c 7d 7e |
| `source_not_propagated` (入れ子 source の断りを親へ伝えない) | **1 件** 5c |
| `profile_aborts_boot` (起動スクリプトの断りで印を立て直す) | **1 件** 8d |

7 変異すべて RED。GREEN のまま通ったものは無い。

### 票 §2-1 が挙げた 6 経路の受け持ち

| 経路 | 検査 | 取りこぼし / 誤発火の両方を見たか |
|---|---|---|
| `if` / `time` 経由の入れ子 `execute_command` | 4a〜4f (取りこぼし) / 4g〜4i (誤発火) | はい |
| 入れ子の `source` (戻り値で親へ) | 5a〜5d / 5e・5f | はい |
| パイプの段 | 6a〜6c・6f・6g / 6d・6e | はい |
| `rshell` から呼ばれた `execute_command` | 7a〜7d | **rshell.c は取り込んでいない** (下の §4 を見よ) |
| 対話 (スクリプト外) | 7a〜7e | はい |
| 起動時の `/etc/profile` | 8a〜8e / 8f・8g | `ui.c` は取り込んでいない (下の §4) |

## 0-3. 段 2 の追補 — パイプの段で断ったら**後続の段も実行しない** (PM 決裁)

基点: `feat/gui` = `2ef6f6a` (段 2)。

### なぜ直すか (決裁の理由)

段 2 では「パイプの段で断っても**段の途中では止めない**」(bash の `false | cat` に寄せる)
という判断をして PM の裁定を仰いだ。**独立レビューが同じ箇所を指摘した**ので、
票 [`TASK_SH_TRUNCATION.md`](../../docs/archive/shell/TASK_SH_TRUNCATION.md) §2 の
**「その場で赤字のエラーを 1 行出して、行全体を実行しない」**に合わせる。

反例:

```
<断られる段> | tee 重要ファイル
<断られる段> | echo x > 重要ファイル
```

段を続けると**断ったのに後段の書き込みが起きる**。`> file` は `apply_redirects` が
`FD_REDIR_WRITE` (= O_TRUNC) で開くので、**リダイレクト先が空で上書きされ得る**。
「切り詰めたら実行しない」という票の規則に正面から反する。

bash 寄せを採らないのはここが**シェルの通常の意味論の話ではない**ため —
`false` は「失敗した段」だが、断りは「**そもそも実行されなかった行**」で、
票 §2 の扱いは行単位と決まっている。

### 何を変えたか

| ファイル:行 | 変更 |
|---|---|
| `userland/shell/main.c:448` | `sh_refused_peek()` を足した。**読むだけで消さない** — `sh_refused_take()` を使うと断りが `script_exec` まで届かず、後続の**行**が走る |
| `userland/shell/main.c:611` | パイプの段ループの末尾で `if (sh_refused_peek()) break;`。段の後始末 (`sys_redirect_get_buf_len` → `reset_all_redirects` → `prev_buf` 記録) を**全部通してから**抜けるので、パイプバッファとリダイレクトの回収は今までどおり (`sh_pipeline_leave` / `sh_pipe_free` / `mem_free` はループの外) |
| `userland/shell/shell.h:77,84` | 印の寿命の規則に「段ループは peek で読むだけ」を足し、`sh_refused_peek()` を宣言 |
| `tools/tests/sh_shell_host.c:439` | 同じ実体のスタブ (この試験は `execute_command` をスタブにしているので使わないが、宣言に揃える) |

**印は消していない** (段 2 の規則のまま)。消すのはいちばん外側の `execute_command` の入口だけで、
スクリプト中ならこの行の後で `script_exec` が打ち切る — そこは 1 行も変えていない。

### 試験の足場に足したもの

段 2 の時点では**段が走ったかどうかを見る窓が無かった**。2 つ足した:

- `h_sys_write` が fd 1 / 2 への書き込みを出力に残すようにした。`echo` は `kprintf` ではなく
  `sys_write(1, ...)` を使うので、捨てていると「後続の段が走った」ことを検出できず
  **偽の GREEN** になる (実際、足す前は `6j` が pre-fix でも通ってしまった)。
- `h_sys_redirect_fd` がリダイレクト先を記録するようにした (`redir_opened()` / `g_redir_count`)。
  本物の `>` は O_TRUNC で開くので、**この呼び出しが起きたこと自体**が
  「リダイレクト先が空で上書きされた」ことを意味する。

### RED → GREEN

| | 件数 |
|---|---|
| 段 2 (`2ef6f6a`) の姿 + 新しい検査 = **RED** | GREEN 75 / **FAIL 4** (`6j` `6k` `6l` `6n`) |
| 直した後 = **GREEN** | **GREEN 79 / FAIL 0** (`EXIT sh_truncation_host=0`) |

RED で落ちた 4 件:

| 検査 | 見ているもの |
|---|---|
| `6j` | 断った段の**後続の段を実行しない** (痕跡 = 後段の `echo` の出力) |
| `6k` | 後続の段の**リダイレクト先を開かない** (= 空で上書きしない) |
| `6l` | 断った行はリダイレクトを 1 つも張らない |
| `6n` | 3 段の**真ん中**で断ったときも 3 段目は走らない |

誤発火の裏 (pre-fix でも GREEN のまま = 挙動を変えていないこと):

| 検査 | 見ているもの |
|---|---|
| `6m` | 断る**前**の段は今までどおり走る |
| `6o` / `6p` | 断りの無い 3 段パイプは全段走り、印を残さない |
| `6q` | 通るパイプの最終段の `> file` は今までどおり張る |
| `6r` / `6s` | `if` が**偽**で右辺を走らせないのは断りではない → 後続の段は走る |

### 変異 (否定側) — 8 本すべて RED

| 変異 | 落ちた検査 |
|---|---|
| `pipe_no_peek` (**新規**。段ループで印を見ない = この決裁の前の姿) | **4 件** 6j 6k 6l 6n |
| `compare_truncated` | 24 件 (段 2 と同じ) |
| `no_mark` | 11 件 (段 2 と同じ) |
| `no_clear` | **13 件** 4b 4e 4h 4i **4l** 6d 6e **6o 6p 6q 6s** 7d 7e |
| `nested_clear` | **1 件 4k** (下記) |
| `abort_interactive` | **25 件** 1f 1h 1k 3a 3d 3e 3g 4b 4e 4g 4h 4i 4l 6d 6e 6h 6m 6o 6p 6q 6s 7a 7c 7d 7e |
| `source_not_propagated` | 1 件 5c |
| `profile_aborts_boot` | 1 件 8d |

### `nested_clear` の証人が入れ替わったこと (足した検査 `4j`〜`4l`)

段 2 では「入れ子の入口で印を消さない」規則の**唯一の証人が `6g`** だった —
「断った段の**後ろ**の段が `time …` だと、そこで入れ子の `execute_command` が入口で印を消す」。
この決裁で**断った後の段を走らせなくなった**ので、その経路からは見えなくなった
(`6g` は今も GREEN だが、`time` の段に到達しないので規則を試していない)。

規則自体は `shell.h` の契約として残る (将来「断りの後に入れ子を呼ぶ」構文が増えたら効く) ので、
入れ子の深さを直に作って押さえる検査を足した:

- `4j` 入れ子の `execute_command` は今までどおり走る
- `4k` **入れ子の入口では印を消さない** (`g_exec_depth` を 1 つ上げて `sh_refuse_mark()` の後に呼ぶ)
- `4l` いちばん外側 (深さ 0) の入口では消す

`4k` が `nested_clear` の新しい証人。`g_exec_depth` は `main.c` の file-scope static だが、
この試験は `main.c` を丸ごと `#include` する同じ翻訳単位なので直に触れる。

### 隣の試験

| 試験 | 結果 |
|---|---|
| `python3 -B tools/tests/test_sh_shell.py` | ALL PASS (`EXIT sh_shell_host=0`) |
| `python3 -B tools/tests/test_sh_launch.py` | ALL PASS (`EXIT sh_launch_host=0`、`-Werror` 版のコンパイルも通る) |

### コンパイル

`i386-elf-gcc` で `userland/shell/main.c` を**常駐版**と `-DSHELL_AS_APP` 版の両方、
Makefile と同じフラグ (`-O2 -Wall -Wdeclaration-after-statement`) で **警告 0**。
段ループの `break` は `#ifdef` の外なので両方に同じように効く。

### この追補で確かめていないこと

- **実機 (NP21/W) では動かしていない** ([V4])。ゲスト受入 (票 §4 末尾) は未実施。
- **リンクしていない**: 段 2 と同じく `/usr/local/cross/i386-elf/lib` がこの環境に無く
  `-lc` / `-lgcc` / `-los32save` が解決できないので、`shell.bin` / `sh.bin` は作れていない。
  `make all` / `make check` / `make external` はこの段の担当外 (PM 指示で禁止)。
- **外部段を含むパイプでは見ていない**。`SHELL_AS_APP` では外部段を含むパイプが
  段ループへ入る前に丸ごと断られるので、試験の段は全部内蔵コマンド。
  常駐 `shell.bin` では外部段が段ループを通るが、その経路は実機でしか踏めない。
- `tee` は OS32 に無い。決裁の理由で挙げた `| tee 重要ファイル` は型の説明で、
  試験で使った反例は `| echo tail > /keep.txt` (同じ O_TRUNC の経路)。
- 段 3 / 段 4 の範囲 (T2〜T26) は手つかず。

## 0-4. 段 3 (§5 の段 3「ルーター」) — RED → GREEN

基点: `feat/gui` = `11705db` (段 2b)。範囲は票 §1 の **T3 / T4 / T5 / T6 / T7 / T11 / T13 / T17**
(`main.c` / `sh_exec.inc` / `sh_args.inc` / `sh_launch.inc` と、T3 の内蔵 2 本
`cmd_base.c` の `time` / `cmd_mnt.c` の `exec`)。段 4 の経路には触れていない。

### 何を変えたか

| 経路 | ファイル:行 | 断る条件 (数え方) |
|---|---|---|
| T3 (`try_exec`) | `sh_exec.inc:62-87` `try_exec_len` (新規)、`:105-117` | 組み立てる**前**に測る。`bin_path` の長さ + 各引数について「区切りの空白 1 + 本体 + (クォートが要るなら 2)」。**本体の `"` と `\` は 2 倍** (エスケープの `\` が付くため)。合計が `TRY_EXEC_BUF_SIZE - 2` (510) を超えたら断る。パス単独が `TRY_EXEC_BUF_SIZE - TRY_EXEC_MARGIN` (500) を超えても断る |
| T3 (`exec` 内蔵) | `cmd_mnt.c:41,51-65` | `argv[1..]` の本体 + 区切りの空白。`EXEC_CMDLINE_MAX - 1` (255) を超えたら断る。`exec` はクォートを付け直さないので倍にはならない |
| T3 (`time` 内蔵) | `cmd_base.c:117,126-141` | 同上。`TIME_CMD_MAX - 2` (510) を超えたら断る |
| T4 | `sh_exec.inc:302-309` | `strlen(argv[0])` が `PATH_MAX_LEN - 5` (251) を超えたら断る。251 ちょうどは `.bin` を足しても 255 で収まるので通す |
| T5 | `main.c:406-442` `split_pipeline` | 段数が `MAX_PIPE_STAGES` (8) を超える / **空の段がある** / 1 段が `seg_size - 1` に収まらない、のいずれかで `-1` を返す。呼び手 (`main.c:537-541`) は段を 1 つも実行しない |
| T6 | `sh_args.inc:61,88-91`、`:268,285-291` | `mem_alloc` の失敗を `ctx.alloc_failed` で持ち帰り (コールバックの中では KAPI を呼ばない)、`sys_ls` が戻ってから赤字 1 行 + 印 + `-1`。確保済みは呼び手 `execute_single` が `allocated_strings` からまとめて解放する |
| T7 | `sh_args.inc:149-169` `glob_copy_pattern` (新規)、`:240-258` | パターンが `GLOB_PATTERN_MAX - 1` (255) に収まらない / ディレクトリ部が `PATH_MAX_LEN - 1` (255) を超える → **`sys_ls` を呼ばずに**断る |
| T11 | `sh_launch.inc:72-86` | `cmdline` の長さが `LAUNCH_CMDLINE_MAX` (256) 以上なら **`launch_req` を呼ばずに** 断る。カーネル (`exec/launch.c:144-146`) は 1 行も変えていない |
| T13 | `main.c:316-322` (`execute_single`)、`:493-499` (`execute_command_line`) | 空行は今までどおり黙って戻り、`CMD_BUF_SIZE` (4096) 以上は断る。**空行と区別する**のがこの経路の全部 |
| T17 | `sh_exec.inc:194-200`、`:205-215` | PATH 項目の取り込みが `PATH_MAX_LEN - 2` (254) で止まったのに次の文字が `:` でも `\0` でもない = **区切りを見失っている** → 断る。`dir + '/' + name_buf` が `PATH_MAX_LEN - 1` (255) に収まらないときも、連結する**前**に断る |

上限の定数は [C4] にしたがい `shell.h` に置いた (`TIME_CMD_MAX` / `EXEC_CMDLINE_MAX` /
`GLOB_PATTERN_MAX`)。`cmd_base.c` にあった生の `510` / `512` も定数に置き換えた。

### 断ったら PATH 走査ごと止める (印の**差分**で見る)

`try_exec` は PATH 候補ごとに呼ばれるので、断りを戻り値だけで伝えると
**同じ赤字が候補の数だけ出て、最後に "command not found" で閉じる**。
`run_cmd_internal` と `try_exec_from_path` は入口で `sh_refused_peek()` を控えておき、
呼び出しの前後で**印が 0 → 1 に変わったときだけ**走査を止める。

peek の値そのものを見てはいけない: 入れ子の `execute_command` は印を消さないので、
入ってきた時点で既に印が立っていることがある。値で止めると `4j`
(「入れ子の `execute_command` は今までどおり走る」) が落ちる — 実際に一度落として気づいた。

### `sh.bin` では実効上限が 255 になる (層が 2 つある)

`SHELL_AS_APP` では `try_exec` (510) を通り抜けても `sh_launch` が要求表の上限
(`LAUNCH_CMDLINE_MAX - 1` = 255、T11) で断る。**`try_exec` の 510 が効くのは常駐
`shell.bin` (`sh_launch` = `exec_run`) だけ**。試験は層を文言で見分けている
(`sh: argument list` = T3 の層 / `sh: launch command line` = T11 の層)。

### クォートを見ないパイプ分割 (票 §6 の範囲外) — `echo "a||b"` も断る

`split_pipeline` は**クォートを見ない**。この分割規則は 1 文字も変えていないので、
`echo "a||b"` の中の `|` も今までどおり区切りとして扱われ、真ん中が空の段になる。
したがって `a || b` と同じ規則で**断られる**。クォートを見る分割は票 §6 で範囲外と
決めてあるので、ここでは「断る側」に倒した (黙って捨てる方が票 §2 に反する)。
検査は `13l`。

### RED → GREEN

| | 件数 |
|---|---|
| 段 2b (`11705db`) の姿 + 新しい検査 = **RED** | GREEN 111 / **FAIL 50** |
| 直した後 = **GREEN** | **GREEN 161 / FAIL 0** (`EXIT sh_truncation_host=0`)。下の I1 の追補を入れて最終 **178 検査 / 0 FAIL** |

RED で落ちた 50 件の内訳:

| 経路 | 落ちた検査 |
|---|---|
| T3 (`try_exec`) | 10a 10b 10d 10e 10f 10i 10j 10l 10m 10n |
| T3 (`exec` / `time`) | 11a 11b 11e 11f 11g 11j |
| T4 | 12a 12b 12c |
| T5 | 13a 13b 13e 13f 13g 13h 13i 13j 13k 13l 13m 13n |
| T6 | 14a 14b 14f 14g |
| T7 | 15a 15b 15e 15f |
| T11 | 16a 16b 16f 16g 16h |
| T13 | 17a 17f |
| T17 | 18a 18b 18c 18d |

誤発火の裏 (RED でも GREEN でも通る = 挙動を変えていないこと): `10c` `10g` `10k`
`11c` `11d` `11i` `12d` `12e` `13c` `13d` `13o` `13p` `14c` `14d` `14e` `15c` `15d`
`15g` `15h` `16d` `16e` `17b` `17c` `17d` `17e` `17g` `17h` `18e` `18f` `18g`。
どれも「上限ちょうどは通る」「断っていない行は今までどおり」を見る。

### 「その経路が本当に走ったか」の窓 (偽の緑を避ける)

段 2b の教訓どおり、断りの赤字が出たことだけでは足りない。足した窓は 4 つ:

| 窓 | 何を見るか |
|---|---|
| `g_launch_count` / `g_launch_last` | **子を起こしにいったか** (`launch_req` の呼び出し回数と中身)。T3 / T4 / T11 / T17 の証人 |
| `g_ls_calls` | **照合を試みたか** (`sys_ls` の呼び出し回数)。T7 は 0 でなければならない |
| `g_glob_alloc_budget` / `g_glob_allocs` / `g_frees` | glob が 1 件ごとに確保する**小さな**文字列だけを N 回目で失敗させる。大きな確保 (スクリプト読み込み) と区別しないと、行が glob に届く前に予算を使い切って**別の理由**で失敗し偽の緑になる (実際 1 度そうなった) |
| `out_count()` | 断りの赤字が**何行出たか**。PATH 走査を止めていないと候補の数だけ出る (変異 `scan_not_stopped` の唯一の証人) |

`echo` の出力 (`h_sys_write`) とリダイレクトの記録 (`redir_opened`) は段 2b のものをそのまま使う。

### 変異 (否定側) — 26 本すべて RED

段 2b までの 8 本に、経路ごとの「検査を外した版」「印を立てない版」を 18 本足した:

`t3_no_check` `t3_no_quote_pad` `t3_no_escape_count` `t3_exec_no_check` `t3_time_no_check`
`t4_no_check` `t5_drop_stages` `t5_empty_stage` `t5_empty_no_mark` `t6_no_mark`
`t7_pattern_truncates` `t7_dir_truncates` `t11_no_check` `t13_line_silent`
`t13_single_silent` `t17_entry_split` `t17_join_truncates` `scan_not_stopped`

`t3_no_quote_pad` (前後の `"` を数えない) と `t3_no_escape_count` (`"` `\` を 2 倍にしない)
が**クォート再付与ぶんの数え方**の証人。どちらも素直に数えれば上限に収まる長さなので、
数え落とすと切り詰めたまま子が起きる。

### 隣の試験

| 試験 | 結果 |
|---|---|
| `python3 -B tools/tests/test_sh_shell.py` | ALL PASS (`EXIT sh_shell_host=0`) |
| `python3 -B tools/tests/test_sh_launch.py` | ALL PASS。**T11 で契約が変わったので直した** (下記) |
| `python3 -B tools/tests/test_fs_kind_callers.py` | 40 checks, 0 failures |

`sh_launch_host.c` は `sh_launch.inc` だけを取り込むので、`sh_launch` が呼ぶようになった
`sh_refuse` の最小実装 (赤字 1 行 + 印) をこの試験にも置いた。`test_sh_launch.py` の
`TARGET_STUB` にも宣言を 1 行足してある。既存の `case_req_nogui` (7a〜7f) は
**そのまま GREEN** — 使っている行が 255 バイト未満なので T11 の検査に当たらない。
新しい `case_cmdline_too_long` (8a〜8h) で 256 は断る / 255 は通ることを見る。

### コンパイル

`i386-elf-gcc` で `main.c` / `cmd_base.c` / `cmd_mnt.c` を**常駐版**と `-DSHELL_AS_APP` 版の
両方、Makefile と同じフラグ (`-O2 -Wall`) で **警告 0**。ホスト側は
`-Wall -Wdeclaration-after-statement` で警告 0 ([C1] GNU89)。

### 追補: I1 (`sh: too many arguments`) にも印を立てた (PM 決裁 2026-09-16)

同じ型 (**入力を捨てたのに次の行が走る**) で、しかも T6 を直した
`parse_args_and_glob` の中にある。段 4 へ持ち越すと規則が半端になるので段 3 に含めた。

文言は据え置き (`sh: too many arguments`)。上限ではなく「入り切らない」なので
`sh_refuse` の書式には乗らず、**`sh_refuse_mark()` で印だけ立てる** —
空の段 (T5) / glob の確保失敗 (T6) と同じ扱い。3 か所すべてに入れた
(`sh_args.inc:299` `ctx.overflow` / `:306` 一致しない glob を足せない / `:315` 素の語)。

`MAX_ARGS` は 256 で `argv[argc]` の NUL ぶん**格納は 255 個まで**なので、
語が 255 個の行は通り、256 個目で断る。

| | 件数 |
|---|---|
| 印を立てない姿 (段 3 の他は入った状態) = **RED** | 178 検査中 **6 FAIL** (19b 19f 19i 19k 19l 19q) |
| 直した後 = **GREEN** | **178 検査 / 0 FAIL** |

変異 `i1_no_mark` (`ctx.overflow` の印だけ外す) を足して **RED** を確認。変異は計 **27 本**。

検査 (case 19) は断る 3 か所を全部踏む: (a) glob の展開中に溢れる (`ctx.overflow`、
素の語 250 個 + 8 件一致の glob)、(b) 一致しない glob を足せない、(c) 素の語で溢れる。
併せて「スクリプト中なら後続の行を実行しない」「対話では打ち切らない」
「255 語ちょうどは通る」「段の中で捨てたら後続の段も実行しない」。

**隣の試験で 1 件直した**: `tools/tests/sh_shell_host.c` は `execute_command` を
スタブにしていて**入口の掃除が無い**ので、`case_argv_bound` (12) が立てた印が
そのまま残り、後の `case_exit_stops_rest` (14) で本物の `script_exec` が
1 行目で打ち切っていた。印を明示的に下ろすようにし、ついでに
「捨てるときは印を立てる」ことをその場で見る検査を 2 本足した (`12b2` `12h`)。

### この段で確かめていないこと

- **実機 (NP21/W) では動かしていない** ([V4])。ゲスト受入 (票 §4 末尾) は未実施。
- **リンクしていない**: `make all` / `make check` / `make external` は PM 指示で禁止。
  `shell.bin` / `sh.bin` は作れていないので、常駐シェルの SHA-256 比較もしていない
  (段 3 は挙動を変える段なので、そもそも一致しない)。
- **常駐 `shell.bin` の経路は試験していない**。ホスト試験は `-DSHELL_AS_APP` の 1 本だけで、
  常駐側 (`sh_launch` = `exec_run`、T11 の検査が無い) はコンパイルしか見ていない。
  `try_exec` の 510 の上限が実際に効くのは常駐側だけなので、ここは実機でしか踏めない。
- **T3 のパス単独の上限 (500)** は `PATH_MAX_LEN` が 256 なので到達しない。検査は入れたが
  試験で踏んだ反例は無い。
- 段 4 の範囲 (T2 / T8 / T9 / T10 / T12 / T14〜T16 / T18〜T26) は手つかず。

## 0-5. 段 4 (§5 の段 4「内蔵と入口」) — RED → GREEN

基点: `feat/gui` = `3fd4da5` (段 3)。範囲は票 §1 の **T2 / T8 / T9 / T10 / T12 / T14 / T15 /
T16 / T18 / T19 / T20 / T21 / T22 / T23 / T24**。T25 / T26 は「将来の地雷」のまま手を付けていない。

### 何を変えたか

| 経路 | ファイル:行 | 断る条件 (数え方) |
|---|---|---|
| T2 | `cmd_script.c:70-73` (`more` / `refused`)、`:97-121` (読み切り確認)、`:141-146` (行)、`:160-170` (行数)、`:198-206` (実行しない) | 読み切れない (`sys_read` がバッファを埋め、**閉じる前の 1 バイト追い読みで続きがある**) / 行が `SCRIPT_MAX_LINE - 1` (255) に収まらない / 行数が `SCRIPT_MAX_LINES` (128) を超える、のいずれかで `script_load` が `-1`。**1 行も実行しない** |
| T2 (起動) | `cmd_script.c:322-333` `script_source_profile` | 断りの印を**先に**下ろしてからメッセージを出す。`script_load` で断ると `script_exec` を通らないため、下ろさないと起動後の 1 行目が巻き添えになる (R2) |
| T8 | `cmd_env.c:70-81` `env_set`、`:200-210` `cmd_set` | 登録口は `env_set` 1 本。名前が `ENV_NAME_MAX - 1` (31) / 値が `ENV_VALUE_MAX - 1` (255) を超えたら**登録しない**。`cmd_set` は `=` の手前が 31 に収まらなければ登録も表示もしない。値は写さず `val_start` をそのまま渡す (途中で切らない) |
| T9 | `cmd_env.c:130-150` `env_expand` | 名前の**終端 (`}` / 区切り / 行末) を幅の検査より先に**見る。`vi` が 31 に達してもまだ名前が続くなら `ENV_EXPAND_ERR_NAME` (-2)。`main.c:502-516` が `sh_refuse("sh: variable name", 31)` にして行を断る。`${` + 31 文字 + `}` の `}` 食べ残しも同時に直る |
| T10 | `rshell.c:78-89` `rshell_end_reply` (新規)、`:129-135`、`:143-150` | 上限を超えても**読み取りを止めず行末まで読み捨て**、`rpos >= RSHELL_LINE_MAX - 2` (126) なら `sh_refuse` + **EOT (0x04)** + 次の行へ。成功時の EOT も同じ `rshell_end_reply()` を通る (§2-2) |
| T12 | `cmd_script.c:434-455` `join_args`、`:520-524` `cmd_if` | `join_args` は `CMD_BUF_SIZE - 1` (4095) に収まらなければ `-1`。呼び手は `sh_refuse("if: command line", 4095)` で実行しない。到達するのは**glob で argv が伸びたとき**だけ |
| T14 | `ui.c:63-67` `hist_add`、`:475-483` / `:497-506` `hist_load` | `HIST_LINE_MAX - 1` (511) を超える行は**履歴に入れない / 読み込まない**。`.history` を読み切れなかったとき (追い読みで続きがある) は改行で終わっていない末尾行も入れない。**実行は断らない** — 行そのものは `CMD_BUF_SIZE` まで正当 (票 §2 の 4) |
| T15 | `ui.c:501` `cmd_dropped`、`:648-652`、`:668-670`、`:700-707` | 打鍵を捨てたら印を立て、ENTER で `sh_refuse("sh: line", CMD_BUF_SIZE - 4)` (4092)。**実行も履歴登録もしない**。ESC で行を捨てたら印も下ろす |
| T16 | `cmd_filer.c:74-85` `ft_load`、`:150-170` `fl_path_join`、`:225-232` `fl_ls_callback`、`:352-402` `fl_action_enter` | 名前が `FL_MAX_NAME_LEN - 1` (63) に収まらなければ**表に載せず `fl_state.dropped` に数える** (`sh_ls.inc` の作法。ヘッダに `(hidden N)` を出す)。`fl_path_join` は溢れたら `-1` で、呼び手は起動も移動もせず popup。`/etc/filetypes` を読み切れなければ関連付け表を作らない |
| T18 | `cmd_script.c:335-360` (prompt)、`:389-398` / `:406-409` (input) | プロンプトが `ASK_PROMPT_MAX - 2` (254) に収まらなければ**訊かずに断る**。入力は 255 文字目の打鍵で印を立て、ENTER で断って**登録しない** |
| T19 | `rshell.c:258-283` `resolve_host_path`、`:296-305` (recv)、`:396-406` (push) | 収まらなければ `-1`。呼び手は `sh_refuse("recv/push: host path", 255)` で**開きにいかない** (`O_TRUNC` で別のパスを作らない) |
| T20 | `ui.c:417-429` `hist_build_path`、`:462-469` `hist_load`、`:540-556` (profile) | `HOME` が `PATH_MAX_LEN - HIST_FILE_ROOM` (常駐 244 / `sh.bin` 240) に収まらなければ履歴を使わない。`.profile` は `PATH_MAX_LEN - PROFILE_PATH_ROOM` (244)。**起動は止めない** |
| T21 | `ui.c:143-147` `path_comp_cb`、`:213-222` `file_comp_cb` | 収まらない候補は**補完しない** (切ると別の名前になり、ENTER で別のファイルに作用する) |
| T22 | `cmd_sys.c:165-186`、`:196-203`、`:208-221` `cfg_set_key` | 読み切れなければ (追い読みで続きがある) **書き戻さずに `-1`**。行が `out` に収まらない / 新しい `KEY=VALUE` が入らない場合も同じ |
| T23 | `filer_core.c:82-107` `dir_callback`、`:124-145` `build_full_path`、`:338-341`、`:349-357` | T16 と同型。収まらない名前は載せず `dropped_count` に数え、`build_full_path` は `-1` で移動も確定もしない |
| T24 | `rshell.c:432-452` `cmd_push` | `xfer_buf` 単位で**読み切るまで回す** (`cmd_recv` と同じ形)。以前は `sys_read` が 1 回だけで、4KB を超えるファイルは先頭 4KB だけ送って `Sent` と報告していた |

上限の定数は [C4] にしたがい `shell.h` に置いた (`ENV_NAME_MAX` / `ENV_VALUE_MAX` は
`cmd_env.c` から移設、`ASK_PROMPT_MAX` / `ASK_INPUT_MAX` / `RSHELL_LINE_MAX` /
`RSHELL_HOST_PATH_MAX` / `ENV_EXPAND_ERR_NAME` は新規)。`cmd_sys.c` の生の
`1024` / `1152` は `CFG_READ_MAX` / `CFG_OUT_MAX` に、`ui.c` の生の `12` は
`PROFILE_PATH_ROOM` にした。

### `ui.c` / `rshell.c` は印ではなく**その場で断る**

どちらも `execute_command` の外側なので、印を立てて誰かに読ませる相手がいない。
`sh_refuse()` で赤字 1 行 (上限の書式は [C4] のまま) を出した直後に
`sh_refused_take()` で下ろす。下ろさないと次に走る行 (対話の次の入力、
rshell の次のコマンド) が巻き添えで捨てられる。

`rshell` の断りは**必ず `rshell_end_reply()` を通す** — `buz_off` → 1 tick 待ち →
`serial_putchar(0x04)`。返さないと `/api/cmd` が timeout まで待ち、以後のコマンドが
全滅する (票 §2-2、`POLICY_DEBUG.md` §4-7 と同じ壊れ方)。変異 `t10_no_eot` が証人。

### 試験の足場: `ui.c` / `rshell.c` / `cmd_filer.c` を取り込んだ

段 3 までは「この 3 本はホスト試験に取り込んでいない」と記録していた。段 4 の
T10 / T14 / T15 / T16 / T19 / T20 / T21 はこの 3 本の中にしか無いので、
**実物をそのまま `#include`** するようにした。**ソースは 1 行も写していない**。
差し替えたのは端末と GFX を握る部分だけ:

| 差し替え | 何にしたか |
|---|---|
| キー入力 (`kbd_getkey` / `kbd_getchar` / `kbd_trygetchar` / `ime_getkey`) | 台本 (`key_push*`) を 1 つずつ返す。使い切ったら既定で ESC (`ask` だけ ENTER)。**読みすぎたら試験を落とす** (ハングさせない) |
| シリアル (`serial_*`) | 送信バイトを記録する。`ser_count(0x04)` が EOT の窓 |
| `fldraw_*` (14 本) | 空実装。`filer_draw.c` は TVRAM を直に叩くので取り込まない。`fldraw_popup_message` だけは呼ばれた回数と文言を記録する |
| `save_crc32` / `strtoul` | `rshell.c` の `hotdeploy` が引くだけ。最小実装 |

`shell_run()` は `exit` の印で抜けるので、台本の末尾に `exit` + ENTER を積む。
`cmd_rshell` は ESC、`filer` は `fl_init` / `fl_action_enter` を直に呼ぶ
(`cmd_filer` の全体ループは `kbd_getchar` 台本で抜けられるが、この段で見たいのは
一覧と Enter の判断なので直に呼んでいる)。

**贋 FS を 3 か所直した** (直さないと偽の結果になる):

| 直したもの | 直さないとどうなるか |
|---|---|
| 読み位置を **fd ごと**にした | `push` / `recv` は読み口と書き口を同時に開く。1 本だと書き口を開いた拍子に読み位置が 0 へ戻り、T24 の「読み切るまで回す」が無限ループになる |
| 贋 fd を **10 番から**配るようにした | 1 から配ると `fd_out` が 2 になり、**ファイルへの書き込みが stderr 扱い**になって T24 の窓 (`g_data_written`) が動かない |
| `get_tick` を 1 呼び出しごとに進めるようにした | 止まったままだと rshell / filer の「次の tick まで待つ」ループが抜けない |

### 「その経路が本当に走ったか」の窓 (偽の緑を 2 回捕まえた)

| 窓 | 何を見るか |
|---|---|
| `out_count()` を**エコーと実行で数え分ける** | 行編集の試験は打鍵がそのまま画面へ返るので、`out_has("MK20OUT")` は「打った」だけで真になる。**2 回目**が「`echo` が実際に走った」証拠。`OUT_CAP` も 2KB → 96KB に広げた (4092 バイト注入で肝心の行が押し出されて偽の緑になっていた) |
| `ser_count(0x04)` | rshell が EOT を返したか。断りの経路でも返すこと |
| `g_open_calls` / `opened_path()` | 切れた別の綴りで `sys_open` しにいっていないか (T19 / T20) |
| `g_data_written` / `g_data_writes` | 何バイト書いたか / 1 度でも書き戻したか (T24 / T22) |
| `hist_count` / `hist_buf` | 履歴に**入ったか** (T14 / T15)。`ui.c` の file-scope static を同じ翻訳単位から直に見る |
| `fl_state.count` / `fl_state.dropped` / `g_launch_count` | 一覧に載ったか / 数えたか / 子を起こしにいったか (T16) |
| `g_popup_count` | filer が断りを画面に出したか |

**捕まえた偽の緑 2 件**:

1. `27e` で置いた「スクリプトがそもそも走ったか」の窓 (`echo MK32PRE`) が、
   **case 24 の `shell_run` が立てた `sh_exit_flag` が残っていて、以降の
   `script_exec` が 1 行目で `break` していた**ことを暴いた。窓が無ければ
   `!out_has("MK32OUT")` も `!ran("mknext")` も真で通っていた。`fresh()` で
   `sh_exit_flag` / `sh_refused_flag` を下ろすようにして直した。
2. `24a` / `24h` / `24k` / `24m` は当初「外部コマンド名 + 長い引数」で
   「実行されたか」を見ようとしていたが、**T3 (`try_exec` の 510 バイト上限、段 3)
   が先に断つ**ので、直っていなくても `ran()` が偽になる = 常に緑だった。
   内蔵の `echo` に変えて「エコー 1 回 / 実行で 2 回」を数える形に直した。

### RED → GREEN

RED は**段 4 で触った `.c` 8 本だけを `3fd4da5` の姿に戻して**取った
(`shell.h` の定数と `FL_State` / `FilerState` の `dropped` は残す — 戻すと
新しい試験がコンパイルできない)。

| | 件数 |
|---|---|
| 段 3 (`3fd4da5`) の `.c` + 新しい検査 = **RED** | GREEN 237 / **FAIL 65** |
| 直した後 = **GREEN** | **302 検査 / 0 FAIL** (`EXIT sh_truncation_host=0`) |

RED で落ちた 65 件:

| 経路 | 落ちた検査 |
|---|---|
| T2 (`script_load` / profile) | 20a 20b 20c 20f 20g 20k 20l 20p 20q 20s |
| T8 (`set` / `export` / `ask` の登録) | 21a 21e 21f 21i 21j 21k 21l 21m 21n 21q 21r 21s |
| T9 (`env_expand` の名前) | 22a 22b 22c 22d 22e 22g3 22h 22i |
| T10 / T19 / T24 (`rshell`) | 23a 23b 23g 23j 23k 23l 23m 23p |
| T14 / T15 / T20 (`ui.c`) | 24b 24e 24f 24g 24h 24i 24j 24p 24q 24t 24u |
| T16 (`filer`) | 25a 25b 25c 25g 25h 25k 25k2 |
| T22 (`cfg_set_key`) | 26a 26b 26c |
| T12 (`if` の組み立て) | 27a 27b 27f 27g |
| T21 (タブ補完) | 28a 28d |

誤発火の裏 (RED でも GREEN でも通る = 挙動を変えていないこと): `20d` `20e` `20i`
`20j` `20n` `20o` `20r` `20t` `21b` `21c` `21d` `21g` `21h` `21o` `21p` `21t` `22f`
`22g` `22g2` `22j` `23c`〜`23f` `23h` `23i` `23n` `23o` `23q` `24a` `24c` `24d` `24k`
`24l` `24m` `24n` `24r` `24s` `25d` `25e` `25f` `25i` `25j` `25l` `25m` `26d` `26e`
`26f` `27c` `27d` `27e` `28b` `28c` `28e` `28f` `28g`。
どれも「上限ちょうどは通る」「断っていない行は今までどおり」を見る。

### 変異 (否定側) — 54 本すべて RED

段 3 までの 27 本に、段 4 の経路ごとに 27 本足した:

`t2_line_truncates` `t2_lines_continue` `t2_read_no_probe` `t2_profile_keeps_mark`
`t8_env_set_no_check` `t8_set_name_truncates` `t9_name_no_check`
`t10_stop_reading` `t10_no_eot` `t12_join_truncates`
`t14_hist_add_truncates` `t14_hist_load_truncates` `t14_hist_no_probe`
`t15_drop_silent` `t15_esc_keeps_mark`
`t16_name_truncates` `t16_join_truncates` `t16_ft_no_probe`
`t18_ask_input_silent` `t18_ask_prompt_truncates` `t19_host_path_truncates`
`t20_hist_path_truncates` `t20_profile_truncates`
`t21_cmd_comp_truncates` `t21_file_comp_truncates` `t22_cfg_no_probe`
`t24_push_single_read`

**GREEN のまま通った変異は 1 本も無い。SKIP (目印が 1 か所でない) も無い。**
`t10_no_eot` が §2-2 (断っても EOT を返す) の唯一の証人。

### コンパイル

`i386-elf-gcc` で `main.c` / `cmd_env.c` / `cmd_script.c` / `cmd_sys.c` / `cmd_filer.c` /
`rshell.c` / `ui.c` / `cmd_base.c` / `cmd_mnt.c` / `cmd_dir.c` / `cmd_file.c` /
`cmd_fs_shared.c` を**常駐版と `-DSHELL_AS_APP` 版の両方**、`userland/lib/filer/` の
`filer_core.c` / `filer_draw.c` を、Makefile と同じフラグ (`-O2 -Wall
-Wdeclaration-after-statement`) で **警告 0**。ホスト側も **警告 0** ([C1] GNU89)。

### 隣の試験

| 試験 | 結果 |
|---|---|
| `python3 -B tools/tests/test_sh_shell.py` | ALL PASS (`EXIT sh_shell_host=0`) |
| `python3 -B tools/tests/test_sh_launch.py` | ALL PASS (`EXIT sh_launch_host=0`) |
| `python3 -B tools/tests/test_fs_kind_callers.py` | 40 checks, 0 failures |

### 判断に迷ったところ (PM の確認が要る)

1. **`FilerState` に `dropped_count` を、`FL_State` に `dropped` を足した** (構造体が
   1 int 大きくなる)。どちらも `userland/lib/filer/` と `userland/shell/cmd_filer.c`
   の中でしか使われていない (`apps/` `game/` はこの worktree で未チェックアウトのため
   未確認)。静的リンクなので `make external` で一緒に建て直せば問題ないが、
   **建て直しは PM の担当**。
2. **`T17` の `ui.c` 側は今は到達不能**。`ui.c` の取り込みは `PATH_MAX_LEN - 1` (255)
   なので区切りを見失うには 1 項目が 256 バイト以上要るが、`PATH` は環境変数で
   値の上限が `ENV_VALUE_MAX - 1` = 255。守り (走査ごと止める) は入れたが**反例は
   作れない**ので、T25 / T26 と同じ「将来の地雷」に分類した。検査 `28f` `28g` は
   「普通の PATH は今までどおり」だけを見ている。
3. **`hist_save` は断りを出さない**。`hist_build_path` が `-1` を返したら黙って戻る
   (毎行通るので、出すとプロンプトが赤字で埋まる)。理由は起動時に 1 度だけ通る
   `hist_load` が 1 行出す。
4. **`ask` の入力の印はバックスペースで下りない**。捨てた後に消しても ENTER で断る。
   `ask` には ESC の枝が無く、下ろす契機を新設すると挙動の追加になるため
   「捨てたら断る」に倒した (T15 の ESC は既存の枝に足しただけ)。
5. **`set <32 文字以上の名前>` は argc に関係なく断る**ようにした。直す前は引数 2 個
   なら「値の表示」に落ちて `not set` を出していた。どちらでも登録はされないが、
   31 文字に切った名前で表示していたので断りに寄せた。

### この段で確かめていないこと

- **実機 (NP21/W) では 1 度も動かしていない** ([V4])。票 §4 末尾のゲスト受入
  (U1 / U5 を `/api/key text=` 注入か `set` + 展開で再現) は**未実施**。
  とくに T10 (rshell の EOT) は `/api/cmd` の実物では踏んでいない。
- **リンクしていない**: `make all` / `make check` / `make external` は PM 指示で禁止。
  `shell.bin` / `sh.bin` / `libos32filer.a` は作れていないので、常駐シェルの
  SHA-256 比較もしていない (挙動を変える段なので、そもそも一致しない)。
- **常駐 `shell.bin` の経路は試験していない**。ホスト試験は `-DSHELL_AS_APP` の 1 本だけ。
  `rshell` は常駐側でしか登録されない (`main.c:48` が `#ifndef SHELL_AS_APP`) ので、
  この試験は `cmd_rshell` / `cmd_push` / `cmd_recv` を**直に呼んで**いる
  (`execute_command("rshell")` 経由ではない)。
- **`cmd_filer` の全体ループ (`fl_loop`) は踏んでいない**。`fl_init` / `fl_action_enter`
  を直に呼んでいる。キーリピートと描画の経路は実機でしか踏めない。
- **`filer_core.c` (T23) は 1 件も試験していない**。`libos32gfx` を丸ごと引くので
  ホスト試験に入れていない。票 §4 にも T23 の受入 (U) が無い。直しは
  `cmd_filer.c` (T16) と同型で、そちらは試験で押さえてある。
- **`cmd_recv` の本体 (ダウンロード) は踏んでいない**。試験したのは `resolve_host_path`
  の断り (T19) までで、読み書きのループは段 3 以前から変えていない。
- `docs/POLICY_DEBUG.md` §4 への記録と `docs/manpages/` の更新は**まだ**。
- `hotdeploy` / `tvdump` / `terminal` / `send` (rshell.c の残り) は触っていない。

## 1. 洗い出しの範囲と方法

対象: `userland/shell/` の全ファイルと `userland/lib/filer/`。

見たもの: `char [N]` の宣言、`strncpy` / `strncat` / `strcat` / `str_copy` /
`kstrncpy`、および `PATH_MAX_LEN` (256) / `CMD_BUF_SIZE` (4096) /
`SCRIPT_MAX_LINE` (256) / `SCRIPT_MAX_LINES` (128) / `TRY_EXEC_BUF_SIZE` (512) /
`ENV_NAME_MAX` (32) / `ENV_VALUE_MAX` (256) / `HIST_LINE_MAX` (512) /
`LAUNCH_CMDLINE_MAX` (256、カーネル側) / `SH_LS_NAME_MAX` (256) /
`FL_MAX_NAME_LEN` (64) / `FL_MAX_PATH_LEN` (256) / `FILER_NAME_LEN` (64) /
`FILER_MAX_PATH` (256) / `MAX_PIPE_STAGES` (8) / `MAX_ARGS` (256) の各所。

行番号は **切り出し後** (`sh_exec.inc` を作った後) のもの。票 §1 の行番号は
切り出し前の `main.c` を指しているので、`T3` / `T4` / `T17` はここの番号が正。

## 2. 洗い出し表

### 2-1. 票 §1 に挙がっている 22 件 (すべて再確認した)

| # | 場所 (ファイル:行) | 上限 (定数名と値) | 今の挙動 | 段 2〜4 で変えるか | 備考 |
|---|---|---|---|---|---|
| T1 | `cmd_script.c:349` `strip_quotes` / `:407` `cmd_if` の `v1[256]` `v2[256]` | 255 (`max - 1`) | 255 文字で切って比較。**条件が逆になる** | **段 2 — 済み** (`b3954f1` の次) | 票どおり。長さはクォート除去後 (`sh_args.inc:168,183` が既に落としている) |
| T2 | `cmd_script.c:150` (`li < SCRIPT_MAX_LINE - 1`)、`:121`、`:134-138` | `SCRIPT_MAX_LINE` 256 (実効 255) / `SCRIPT_MAX_LINES` 128 | 256 文字目以降を切る。129 行目以降は赤字を出して `break` し、`return 0` で先頭 128 行を実行 | **段 4 — 済み** (`3fd4da5` の次) | 票どおり |
| T2' | `cmd_script.c:97` `sys_read(fd, raw_buf, raw_buf_size - 1)` | `SCRIPT_MAX_LINES * SCRIPT_MAX_LINE - 1` = 32767 | 32KB 超のスクリプトを**黙って**途中で切り、途中の行から先が無かったことになる | **段 4 — 済み** (`3fd4da5` の次) | 票 U3 の「読み込み上限超のファイル」に対応。票の表には行が無いので分けて書いた |
| T3a | `sh_exec.inc:54,70,85,91` `try_exec` | `TRY_EXEC_BUF_SIZE` 512、実効 510 (`limit_args`) | 溢れた引数を落として**起動する**。クォート再付与で `"` `\` は 2 倍 | **段 3 — 済み** (`11705db` の次) | 票どおり (票の `main.c:140-170` は切り出し前の番号) |
| T3b | `cmd_mnt.c:41,51-65` `exec` | `EXEC_CMDLINE_MAX` 256、実効 255 | 同上 | **段 3 — 済み** (`11705db` の次) | 票どおり。PM 指示で T3 の 3 か所をまとめて段 3 で直した (票の表は段 4 に置いていた) |
| T3c | `cmd_base.c:117,126-141` `time` | `TIME_CMD_MAX` 512、実効 510 | 同上 (組み立てた行を `execute_command` へ) | **段 3 — 済み** (`11705db` の次) | 同上 |
| T4 | `sh_exec.inc:209-212` `run_cmd_internal` | `PATH_MAX_LEN - 5` = 251 | コマンド名を 251 で切って `.bin` を付ける → **別のファイルが起動する** | **段 3 — 済み** (`11705db` の次) | 票どおり |
| T5 | `main.c:386` `MAX_PIPE_STAGES`、`:388-411` `split_pipeline` | 8 段 / 段ごと `CMD_BUF_SIZE` 4096 | 9 段目以降と空の段を捨てる | **段 3 — 済み** (`11705db` の次) | 票どおり。段バッファ側 (`pos < seg_size - 1`) は 4096 で実質届かない |
| T6 | `sh_args.inc:84-85` `glob_cb` | — (`mem_alloc` 失敗) | 印を立てずに `return`。一致の**一部だけ**を渡す | **段 3 — 済み** (`11705db` の次) | 票どおり |
| T7 | `sh_args.inc:222,230` (`patlen < 255`)、`:216` (`i < PATH_MAX_LEN - 1`) | 255 / 255 | パターンとディレクトリ部を切る → **別のファイルに一致する** | **段 3 — 済み** (`11705db` の次) | 票どおり |
| T8 | `cmd_env.c:37-41` `str_copy` (`env_set` 経由)、`:193` (`ni < ENV_NAME_MAX - 1`)、`:227` (`vi < ENV_VALUE_MAX - 1`) | `ENV_NAME_MAX` 32 (実効 31) / `ENV_VALUE_MAX` 256 (実効 255) | 切った名前・値で**登録する** | **段 4 — 済み** (`3fd4da5` の次) | 票の訂正どおり。**さらに 1 つ**: `set <32 文字以上の名前>=VALUE` は名前の途中で `=` を見失うが、**引数が 3 個以上あると** `argc >= 3` の fallback (`cmd_env.c:208-210`) に落ちて `argv[2]` を値として登録する (引数 2 個なら「値の表示」分岐) |
| T9 | `cmd_env.c:126-136` `env_expand` | `ENV_NAME_MAX - 1` = 31 | `${NAME}` も裸の `$NAME` も 31 文字で打ち切り、残り (と `}`) が**リテラルとして素通りする** | **段 4 — 済み** (`3fd4da5` の次) | 票どおり |
| T10 | `rshell.c:123-124` (`rpos < 126`) | `rbuf[128]`、実効 126 | 126 文字で読み取りを止め、**その接頭辞を実行する**。残りは次の入力になる | **段 4 — 済み** (`3fd4da5` の次) | 票どおり。断るときも EOT (0x04) を返すこと (票 §2-2) |
| T11 | `sh_launch.inc:63` `sh_launch` + `exec/launch.c` (`LAUNCH_CMDLINE_MAX` 256) | 256 | `launch_req` が `INVAL` を返し、`sh.bin` は「GUI 外」と読む (`sh_launch.inc:93-102`) | **段 3 — 済み** (`11705db` の次) | 票どおり。カーネルは変えない |
| T12 | `cmd_script.c:427-429` `cmd_if` の `join_args` | `CMD_BUF_SIZE` 4096 | 切れたコマンド行を実行する。到達するのは glob 展開で argv が伸びたとき | **段 4 — 済み** (`3fd4da5` の次) | 票どおり |
| T13 | `main.c:423` `execute_command` / `:316` `execute_single` | `CMD_BUF_SIZE` 4096 | 空行と長大行を**同じ扱い**で黙って `return` | **段 3 — 済み** (`11705db` の次) | 票どおり (両方とも `>= CMD_BUF_SIZE`) |
| T14 | `ui.c:65` `hist_add` (`j < HIST_LINE_MAX - 1`)、`:480` `hist_load` (`li >= HIST_LINE_MAX` → 511) | `HIST_LINE_MAX` 512 (実効 511) | 512 バイト目以降を切って履歴に残す / 読み込む | **段 4 — 済み** (`3fd4da5` の次) | 票どおり。`hist_load` は `:467` で `HIST_SIZE * HIST_LINE_MAX - 1` = 8191 バイトしか読まない (末尾の行も切れる) |
| T15 | `ui.c:645` (`cmd_len < CMD_BUF_SIZE - 4`)、`:666` (`cmd_len + utf8_len < CMD_BUF_SIZE - 1`)、`:705-706` | 4092 / 4095 | 4092 バイトで打鍵を黙って捨て、ENTER でその接頭辞を実行し履歴にも入れる | **段 4 — 済み** (`3fd4da5` の次) | 票どおり。`/api/key text=` の機械注入では気付けない |
| T16 | `cmd_filer.c:215-217` (`i < FL_MAX_NAME_LEN - 1`)、`:144-153` `fl_path_join`、`:330-339`、`:73` `ft_load` | `FL_MAX_NAME_LEN` 64 (実効 63) / `FL_MAX_PATH_LEN` 256 / `FL_FILETYPES_MAXSZ` 8192 (実効 8191) | 63 バイトで切った名前で**別のファイルを起動する / 別のディレクトリへ入る**。`/etc/filetypes` の末尾行が切れて別コマンドに関連付く | **段 4 — 済み** (`3fd4da5` の次) | 票どおり。`fl_path_join` は戻り値が無く、溢れても呼び手に伝わらない |
| T17 | `sh_exec.inc:125` (`di < PATH_MAX_LEN - 2`)、`ui.c:264` (`di < PATH_MAX_LEN - 1`) | 254 / 255 | 長い PATH 項目で `:` の区切りを見失い、**残りが別のディレクトリとして扱われる** | **段 3 — 済み** (`sh_exec.inc`) / **段 4 — 済み** (`ui.c`、ただし PATH は `ENV_VALUE_MAX` 上限のため**今は到達不能**) | 票どおり |
| T18 | `cmd_script.c:289` (`pi < 254`)、`:323` (`len < 254`) | `prompt[256]` / `input[256]`、実効 254 | 255 文字目以降の打鍵を捨てて変数に入れる | **段 4 — 済み** (`3fd4da5` の次) | 票どおり。入れた値は `env_set` でさらに 255 で切られる (T8) |
| T19 | `rshell.c:242-257` `resolve_host_path` | `host_path[256]`、実効 255 | `push` / `recv` で切れた別パスを `O_TRUNC` で作る | **段 4 — 済み** (`3fd4da5` の次) | 票どおり。`host:` の 5 バイト (+ `/` 補完 1) を差し引くので、`host:` の後ろが 249〜250 文字で切れ始める |
| T20 | `ui.c:404-418` `hist_build_path`、`:519-525` profile パス | `PATH_MAX_LEN - HIST_FILE_ROOM` = 244 (常駐) / 240 (`sh.bin`、`HIST_FILE_ROOM` 16)、profile は `PATH_MAX_LEN - 12` = 244 | 長い `HOME` を切って**別ディレクトリの `.history` / `.profile`** を読み書きする | **段 4 — 済み** (`3fd4da5` の次) | 票の「244 文字以上」は常駐側の値。`sh.bin` は `.sh_history` を使うので 240 |
| T21 | `ui.c:143,153-155` `name_store[40][64]`、`:210-215` `name_store[40][128]` | 63 / 126 (+ `/` 1) | 補完結果が切れて別の名前になり、`ui.c:327-335` / `:374-375` でバッファへ書き込まれる | **段 4 — 済み** (`3fd4da5` の次) | 票どおり |
| T22 | `cmd_sys.c:161-169` (`sizeof(buf) - 1`)、`:186` (`sizeof(out) - CFG_LINE_RESERVE`) | `buf[1024]` (実効 1023) / `out[1152] - 64` = 1088 | `/etc/system.cfg` を 1023 バイトで読み、**切れたまま書き戻す** (1KB 超の設定が消える) | **段 4 — 済み** (`3fd4da5` の次) | 票どおり |

### 2-2. 票が「変更不要と確認された」と書いているもの — 再確認の結果

| 場所 | 票の記述 | 再確認 | 備考 |
|---|---|---|---|
| `cmd_file.c` の `fs_join_path` 呼び手 | 「`-1` を全呼び手が見る」 | **そのとおり** | 呼び手は `cmd_file.c:178,179,256,267,351` の 5 か所。すべて `< 0` を見て `cp: path too long` で飛ばす |
| `cmd_dir.c` | 「変更不要」 | **そのとおり** | `old_dir[PATH_MAX_LEN]` は `strncpy` + 明示 NUL で、用途は `OLDPWD` の保存だけ。`size_buf[16]` は数値の整形 |
| `cmd_base.c` の `time` 以外 | 「変更不要」 | **そのとおり** | `np2_get_version` は `sizeof(buf)` を渡す。`pad[12]` は RTC の作業域 |
| `sh_ls.inc` | 「変更不要」 | **そのとおり** | `SH_LS_NAME_MAX` = `OS32_MAX_PATH` = `DirEntry_Ext.name` と同じ幅なので名前は切れない。溢れた**件数**は `sh_ls_dropped()` で呼び手へ伝わる |
| `split_pipeline` の段バッファ | 「変更不要」 | **そのとおり** | 段ごとの幅は `CMD_BUF_SIZE` 4096 で、`execute_command` の入口 (T13) が同じ上限で先に断るので届かない。段**数** 8 は T5 |
| `sh_stage_is_builtin` | 「変更不要」 | **そのとおり** | `main.c:272` の `name[PATH_MAX_LEN]` は先頭語を写すだけ。255 で切れても「内蔵か」の判定が保守側 (外部扱い) に倒れるだけで、切れた名前は実行に使われない |
| `sh_path_normalize` | 「変更不要」 | **ほぼそのとおり。1 点だけ違う** | 出力側は `seg_start + sn >= max` で `-1` を返すので票の言うとおり。ただし**入力側** (`cmd_fs_shared.c:114,120`、`tmp[PATH_MAX_LEN * 2]`) は cwd + 引数が 511 バイトを超えると**黙って切る**。呼び手は `fs_same_file` だけで、切れた綴りが偶然一致すると「同じファイル」と誤判定して `cp` が中止される (安全側に倒れる) — 段 2〜4 では変えないが、表には残す |
| カーネルのトークナイザ (`exec/exec.c`) | 「変更不要」 | **範囲外 (票 §3 でカーネルは変えない)** | 読むだけ |
| `cmd_fs_shared.c:215` `fs_append_basename` / `:261` `fs_parse_two_args` | 「呼び手が無い」 | **そのとおり** | `userland/` `tools/` 全体を検索して、宣言 (`cmd_fs_shared.h:42,49`) 以外の参照は 0。どちらも溢れを見ない (`fs_parse_two_args` は 255 で黙って切る)。**削除するか段 4 で決める** |

### 2-3. 票に無かったもの (この洗い出しで足した)

| # | 場所 (ファイル:行) | 上限 (定数名と値) | 今の挙動 | 段 2〜4 で変えるか | 備考 |
|---|---|---|---|---|---|
| T23 | `userland/lib/filer/filer_core.c:96-99` `dir_callback`、`:124-133` `build_full_path`、`:243`、`:320-325`、`:330` | `FILER_NAME_LEN` 64 (実効 63) / `FILER_MAX_PATH` 256 | T16 とまったく同じ穴が **`libos32filer` 側にもある**。63 バイトで切った名前で `filer_get_selected_path()` が**別のファイルのパス**を返し、ディレクトリ移動も別の場所へ行く。`build_full_path` は溢れても戻り値が無い | **段 4 — 済み** (`3fd4da5` の次) | 票 §3 は `userland/lib/filer/` を「名前の幅」として範囲に入れている。`cmd_filer.c` (TVRAM ファイラ) とは別のソースなので両方直す |
| T24 | `rshell.c:366-378` `cmd_push` | `xfer_buf[4096]` | `sys_read` が 1 回だけなので、**4096 バイトを超えるファイルは先頭 4KB だけがホストへ書かれ**、`Uploaded` と報告される。`cmd_recv` は `:303-319` で読み切るまで回すので直っている (同じ欠陥が `recv` 側だけ直された) | **段 4 — 済み** (`3fd4da5` の次) | 入力そのものではなくファイル内容の切り詰めだが、「切り詰めたまま成功を返す」という同じ型 |
| T25 | `cmd_dir.c:167,169` `env_set("OLDPWD", ...)` / `env_set("PWD", ...)` | `ENV_VALUE_MAX - 1` = 255 | cwd が 255 バイトちょうどまでは収まるので**今は届かない**。`PATH_MAX_LEN` (256) を上げたら T8 の切り詰めが先に当たる | **変えない** | T8 を直せば自動的に閉じる。上限を上げるときの注意として残す |
| T26 | `ui.c:387-388` タブ補完の共通接頭辞 | `CMD_BUF_SIZE - 1` | 書き込みループは境界を見るが、直後の `buf[common_len] = 0;` は見ない。`common_len` は候補名の長さ (≤ 126) で抑えられているので**今は届かない** | **変えない** | T21 を直すときに一緒に見る |

**合計 26 件** (票の 22 件 + 新規 4 件)。票の記述と食い違ったのは 3 か所 —
T8 の `set NAME=VALUE` に第 3 引数があるときの経路、`sh_path_normalize` の入力側、
T20 の `sh.bin` 側の値 (240 であって 244 ではない)。いずれも上の表に書いた。

## 3. 等価の証明 (段 1 は挙動を変えていない)

`make -n programs` が出す実際のコンパイル行 (常駐 `userland/shell/*.o` と
`-DSHELL_AS_APP` の `userland/shell/sh_obj/*.o` の 2 組、計 24 個) を、切り出しの
前後で回して md5 を比べた。

| 結果 | 件数 |
|---|---|
| md5 が完全に一致 | **22 / 24** |
| 3 バイトだけ違う | 2 (`cmd_base.o` の常駐版と `sh.bin` 版) |

`main.o` は念のためもう一度、`git show a4f5429:userland/shell/main.c` を取り出して
**`-MMD -MP` まで含む make の実コンパイル行そのまま**でも比べた — 常駐版
`f3790e1f82566a8f3555b8c93d0472b6` / `sh.bin` 版 `37d0baa1bb2ec95339725f95069931f6` で
どちらも一致する。

`cmd_base.o` の差は `cmd_base.c:68` の `__DATE__` / `__TIME__` — `strings` の差分が
`11:20:47` → `11:22:02` の 1 行だけで、`.text` / `.data` / `.rodata` の
`objdump -s` は 3 セクションとも一致する。**切り出しとは無関係の、ビルド時刻の
埋め込みによる差**。

一致する理由はソースの作りからも言える: `sh_exec.inc` は `main.c` の該当区間
(旧 `main.c:120-346`) を **1 行も変えずに** 移し、`#include` を元の位置に置いた
だけなので、プリプロセスした後のトークン列が同一になる。`sh_launch.inc` /
`sh_pipe.inc` の `#include` を `try_exec` の**後ろ**に置く並びも崩していない
(先に置くと `sh_launch` が `try_exec` へ展開され得て、コード生成が変わる)。

## 4. 確かめていないこと

### 段 2 の時点

- **実機 (NP21/W) では 1 度も動かしていない** ([V4])。票 §4 末尾のゲスト受入
  (U1 を `/api/key text=` 注入か `set` + 展開で再現) は未実施。
- **リンクは通していない**: この環境に `/usr/local/cross/i386-elf/lib` が無く、
  `-lc` / `-lgcc` / `-los32save` が解決できないので `userland/shell.bin` /
  `userland/sh.bin` は作れていない (この票の変更とは無関係の環境側の欠け)。
  **コンパイルは常駐版・`-DSHELL_AS_APP` 版の両方で警告 0** を確認した。
- `rshell.c` は serial を握るのでホスト試験に取り込んでいない。7a〜7d は
  `rshell.c:149` と同じ「1 行ずつ `execute_command` を呼ぶ」形を並べて見ている
  だけで、rshell そのものは通していない (EOT = 票 §2-2 は段 4 の担当)。
- `ui.c` の `shell_run()` も取り込んでいない。8a〜8g は `shell_run` が呼ぶ
  `script_source_profile()` を実物で通しているが、`shell_run` の中の呼び出し
  そのもの (2 行) は試験で踏んでいない。
- `docs/POLICY_DEBUG.md` §4 への T1 の記録は**まだ書いていない** (票 §3)。

### 段 1 の時点

- 実機 (NP21/W) では 1 度も動かしていない。段 1 は挙動を変えないのでゲスト受入は
  段 2 以降 (票 §4 末尾の経路: `/api/key text=` 注入か、スクリプトの `set` + 展開)。
- `shell.bin` / `sh.bin` のリンクとバイナリ一致は見ていない (`.o` の md5 まで)。
  `make programs` を通したリンク後の SHA-256 比較は段 2 のコミットでまとめて見る。
- 表の「今の挙動」はコードの読みと `sh_truncation_host.c` の 2 例で確かめたもので、
  T2〜T26 の各行を試験で 1 件ずつ踏んではいない。踏むのは段 2〜4 の担当 (票 §4 の
  U1〜U20)。

---

# 5. 継承バグ「`source` が ESC 以外も食う」 (2026-09-16)

基点: `feat/gui` = `3e95ab1`
試験: `python3 -B tools/tests/test_sh_truncation.py` (case 29) / `--mutate` で否定側

## 5-1. 直したもの

`script_exec` (`userland/shell/cmd_script.c`) は **1 行ごと**に ESC を見て打ち切る。
その監視が `kbd_trygetkey()` — つまり**キューからキーを取り出す**口 — だったので、
ESC 以外の打鍵は条件に合わず**そのまま捨てられて**いた。スクリプト実行中に打った
文字が消え、終わった後の入力の先頭が欠ける。

直し方は **(B) カーネルに覗く口を足す**:

| 場所 | 足したもの |
|---|---|
| `drivers/kbd.c` | `kbd_peekkey()` — 取り出さずに次のキーを返す (無ければ -1)。戻り値の形は `kbd_trygetkey` と同じ。源の見る順番も同じ (GUI の注入リング → rshell のシリアル → cooked リング) |
| `drivers/serial.c` | `serial_peekchar()` — `ser_buf[ser_head]` を読むだけ |
| `kernel/kbd_inject.c` | `kbd_inject_peek()` — 注入リングの先頭を読むだけ。自己診断にビット 5 を追加 |
| `sdk/kapi.json` | `kbd_peekkey` を**末尾に追加** ([ABI2])、版数 v53 → v54 ([ABI3])。slot 214 = 0x360 |
| `userland/shell/cmd_script.c` | 監視を `kbd_peekkey()` に差し替え、**ESC と分かってから** `kbd_trygetkey()` を 1 回呼んで取り除く |
| `build/app.conf` | 要求 KAPI を `userland/shell` 46 → 54、`userland/sh` (同じソースの CPL=3 版) 49 → 54 |

### なぜ (A)「シェル側で押し戻す」を採らなかったか

`kbd_inject` は使えない。理由は 4 つで、どれも単独で決定的:

1. **CUI では誰も読まない。** 注入リングを見るのは `kbd_gui_mode` のときだけ
   (`drivers/kbd.c` の `kbd_trygetchar` / `kbd_trygetkey` の GUI 分岐)。
   `source` の普通の経路 (CUI / rshell) は cooked リングを読むので、
   押し戻したバイトは**二度と読まれない** — バグは直らない。
2. **所有権で断られる。** `kbd_inject` は `con_sink` の読み手からしか受けない
   (`kernel/kbd_inject.c`、読み手が違えば `OS32_ERR_EXIST`)。GUI ではその読み手は
   端末アプリで、シェルではない。
3. **順序が壊れる。** `inj_push` は末尾に積むので、取り出したキーを戻すと
   **既に並んでいるものの後ろ**に回る。
4. **情報が落ちる。** `kbd_trygetkey` は u16 (上位 = スキャンコード)、`kbd_inject` は
   UTF-8 バイト列。上位バイトが消えるので矢印・ファンクションキーが化ける。

## 5-2. RED → GREEN

RED は `cmd_script.c` の監視を元の `kbd_trygetkey()` の姿に戻して取った。

| | 検査 | FAIL |
|---|---|---|
| 直す前 (= `kbd_trygetkey` で取り出す) | 337 | **9** (29c / 29d / 29f / 29g / 29l / 29m / 29n / 29p / 29q) |
| 直した後 | **337** | **0** (`EXIT sh_truncation_host=0`) |

変異 (否定側) は **59 本すべて RED**。今回足した 3 本:

| 変異 | 壊すもの |
|---|---|
| `esc_watch_eats_key` | 直す前の姿 (`kbd_trygetkey` で取り出して捨てる) |
| `esc_not_removed` | 覗くだけで **ESC も取り除かない** (後の行編集が ESC を食う) |
| `esc_no_abort` | ESC の打ち切りそのものを外す (今の挙動を弱めていないかの裏) |

隣の試験: `test_sh_shell.py` / `test_cat_linenum.py` / `test_fs_kind_callers.py` /
`test_kbd_inject.py` すべて GREEN。`sh_shell_host.c` は贋 KAPI に `kbd_peekkey` が
無いまま NULL を呼んで **SIGSEGV (-11)** を出したので、空実装を足した
(= 新しいシェルを古いカーネルで動かすと同じことが起きる。`app.conf` の要求版数が
それを止める)。

## 5-3. 挙動が 1 つだけ変わる (PM 判断が要る)

**ESC が**他の打鍵の**後ろに積まれている**とき、この監視は打ち切らなくなった。
覗くのは**先頭だけ**なので、`['a', ESC]` の状態では `'a'` しか見えない。

- 直す前: 行ごとに 1 つ取り出していたので、`'a'` を捨ててから次の行で ESC に届き、
  打ち切っていた (打鍵が消える側のバグそのもの)。
- 直した後: `'a'` も ESC も消えずに順序どおり残るが、その行では打ち切らない。

これを両立させるには「キューの途中から ESC だけ抜く」口が要る (覗きに添字を付けるか、
ESC 専用の検査を足すか)。今回は入れていない。逃げ道として **CTRL+STOP**
(`ring3_abort_request`、キューを経由しない) は従来どおり効く。

## 5-4. 確かめていないこと ([V4])

- **実機 (NP21/W) では 1 度も動かしていない。** `make clean` → `make all` → `make check`
  → `make external` も**未実施** (コーダーの禁止範囲)。[ABI3] の clean ビルドは PM 待ち。
- 通したのはホスト試験と、`drivers/kbd.c` / `drivers/serial.c` / `kernel/kbd_inject.c` の
  `i386-elf-gcc -Wall -Wextra -Werror` 単体コンパイルだけ。`kapi/kapi_generated.c` は
  この場の `-I` では無関係な既存の implicit declaration が出るので `-Werror` 無しで通し、
  `kbd_peekkey` に関する警告が 0 であることだけ見た。
- **GUI (`SHELL_AS_APP`) で実際に動かしていない。** `kbd_peekkey` は `exec_park_poll` を
  呼ばないので、GUI 中のスクリプト実行で**行ごとの WM への譲りが無くなる**。park は
  成立すると戻らず、起こされるときに `exec_resume` が注入リングの 1 バイトを取り出して
  EAX に入れてしまうため、覗きと両立しない。長いスクリプトで GUI の反応が鈍らないかは
  未確認。
- `apps/` `game/` の submodule は再ビルドしていない (この変更は KAPI の**末尾追加**だけで
  既存スロットを動かさないが、[ABI3] の手順としては `make external` が要る)。
