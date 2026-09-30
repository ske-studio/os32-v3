# S3-I2 — FDD からの新規インストールの修正 (lz4 カーネル + `/boot`) と使い捨て NHD の道具

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

実装 I `49ee29c` + `44e7899`、T `cd1e136` + `a80f7b0` + `9de8f0f` + `b2416a5` + `862d291` + `2c5aeb3`、K `b30b3f5`。Codex 実装レビュー 2 往復 (5 件 → 1 件 → 解消。往復 3 は実走の欠陥 2 件を T で修正したのみで Codex には出していない)。受入 F1〜F6 合格 (使い捨て NHD、4 回の trial、作業 NHD 無傷)。設計: 第 4 版。ユーザー決裁 2026-09-14「a から」。前提: S3 完了 (main `24cfcf7`)。
経緯: TASK_S0 §3 B10 → TASK_S3 §7 (残ゲート)。現行の `install` (無印) は `/kernel.bin` を必須とし LBA 6 へ生書きするが、FDD イメージは `/VMKRNL.LZ4` + ローダ v3 (`/sys/loader_h.bin` = `boot/loader_hdd.bin`、ext2 の `/boot/vmkernel.lz4` を読む) を収録するので、**FDD からの新規インストールは Phase 1 の `Missing /kernel.bin` で止まり `/etc` コピー (settings.db の seed) まで到達しない**。`cdinst` (CD) は lz4 / `/boot` 対応済み (`cdinst.c:240` の注、`:510` の `mkdir /hd0/boot`)。
正典: [TASK_S0.md](TASK_S0.md) §3 B10、[TASK_S3.md](TASK_S3.md) §7、`docs/08_build.md` §8-4 (配備 3 経路)、`build/image.mk` (FDD の中身)、スキル `os32-emu-config` (ini は PM だけ、実装と適用の承認を分ける)、memory `os32-np21w-launch` (FDD は引数、HDD は ini のみ)。
規約: [C1] C89、[D2] (NHD 上書き / ini 変更はユーザー承認、使い捨てだけを対象にする)、コーダーは worktree + ホスト TDD のみ。

## 0. 範囲と分担

| 票 | 範囲 | レーン | 触るファイル |
|---|---|---|---|
| **S3I2-I** | `userland/system/install.c` の通常インストール経路 (§1)。`--recover-settings` / `--revert-settings` (S3) は不変 | C (system) | `userland/system/install.c`、`tools/tests/install_fresh_host.c` / `test_install_fresh.py` / `s3i2_tdd.md` §I |
| **S3I2-T** | (a) `tools/np21w_ini.py` / `np21w_trial.py` に **`HDD1FILE`** を足す (§2)。(b) `tools/mk_blank_nhd.py` = 使い捨て NHD (NHD ヘッダ 512B + 全ゼロ、大きさ指定) を作る (§2) | ツール | `tools/np21w_ini.py`、`tools/np21w_trial.py`、`tools/np21w_ini_live.py` (触るなら)、`tools/mk_blank_nhd.py` (新規)、`tools/tests/test_np21w_ini*.py` / `test_mk_blank_nhd.py`、`tools/tests/np21w_trial_tdd.md` |
| **S3I2-K** | `fs/fatfs_vfs.c` の `fatfs_vfs_list()`: `f_readdir()` の途中エラーを `VFS_OK` で握りつぶさず `ff_to_vfs()` の負を返す (往復 2 の R2)。`vfs_ls` はその戻りをそのまま返す (現行どおり)。ホスト試験は `fatfs_stat_host.c` の作法で `f_readdir` の途中失敗を注入 | K (fs) | `fs/fatfs_vfs.c`、`tools/tests/fatfs_stat_host.c` (追記) |
| PM | 受入 (§3): 使い捨て NHD の作成 → trial ([D2]) → FDD ブート → `install` → trial で HDD ブート → 検証 → 通常 ini で起動し直す。`docs/INSTALL.md` / `08_build.md` の手順更新 | PM / テスター | — |

含めない: `cdinst` (CD) の変更、FDD イメージの中身の変更 (現行のまま使う)、パーティション / ジオメトリの見直し、`install` の対話 UI の作り直し。

## 1. `install` (無印) の修正 (S3I2-I)

現行 Phase 1〜3 のうち変えるのは **kernel.bin の段、`/boot` へのコピー、宛先名の小文字化、失敗の伝播、IDE 情報の型**:
- **IDE 情報の型** (往復 1 の B2): 現行の `IdeInfoTemp` (92 B) は `ide_identify` の実型 `IdeInfo` (`drivers/ide.h:79`、96 B、末尾 `phys_sector_size`) より小さく、正常な認識で 2 B の領域外書込みになる既存欠陥。**SDK の生成ヘッダの `IdeInfo` (または同じレイアウトの型) を使う**。ホスト試験で `sizeof` と末尾書込みを固定。
- Phase 0 (承認前の検査): **`/VMKRNL.LZ4` の存在と大きさ** (`sys_stat`、> 0。FAT の 8.3 名 `VMKRNL.LZ4` は大小文字を正規化して見つかる)、`/sys/boot_hdd.bin` / `/sys/loader_h.bin` の存在も同じくここで確認 (現行は書き込みの途中で `Missing …` になる)。無ければ中止 (**何も書かない**)。
- Phase 1 (ブートセクタ): IPL → LBA 0、PT → LBA 1、ローダ → LBA 2+ は**そのまま**。**`/kernel.bin` の読込と LBA 6 への生書きを削除**。
- Phase 2 (ext2 format): そのまま。format 失敗時は IPL / PT / ローダに加えて部分的な ext2 メタデータを含む不完全な NHD が残る (使い捨て対象なので許容。表示 `format failed - the drive is not bootable; rerun install`)。
- Phase 3 (コピー): `mkdir /hd0/boot` を追加し、**`/VMKRNL.LZ4` → `/hd0/boot/vmkernel.lz4`** をストリームコピー (現行 `copy_file` のバッファは **128KB**、470KB を反復で写す) → **長さ一致**を確認。続けて `/sys` `/bin` `/sbin` `/etc` を写す。
  - **宛先名の小文字化** (往復 1 の B1): FAT (`FF_USE_LFN 0`) の `sys_ls` は `SHELL.BIN` / `SETTINGS.DB` のように大文字を返し、ext2 は大小文字を区別するので、そのまま写すと `/hd0/etc/SETTINGS.DB` になり seed も `/sys/shell.bin` の起動も成立しない。**媒体 → HDD のコピーでは宛先の名前 (各成分) を ASCII 小文字に正規化する** (`copy_directory` の宛先パス組立と `mkdir`)。ソースは列挙で得た名前のまま開く。ホスト試験は大文字を返す `sys_ls` の贋物で最終パスを固定。
  - **`/etc/profile` は写さない** (FDD 用 `assets/profile_fdd` で PATH に `/usr/bin` が無い。HDD の profile は通常配備が置く。往復 1 non-blocker の矛盾を解消)。`/etc/settings.db` は写す (seed)。
  - **失敗の伝播** (往復 1 の B3、往復 2 の R2): `copy_file` は read の負を EOF と区別して負を返す、short write は負。`copy_directory` は **`sys_ls` の戻り値 (負) も失敗に数え**、mkdir / 再帰の失敗を含む失敗件数を返す。`main` はコピーの戻り値と **`vfs_sync()` の戻り値**を検査して 1 件でも失敗なら `[FAIL]` を出して終了 1。成功時だけ `Installation complete` と終了 0。列挙の途中エラーが `sys_ls` の負として届くよう、FAT 側は S3I2-K で直す (現行 `fatfs_vfs_list` は `f_readdir` の失敗後も `VFS_OK`)。
- 終了コード: 成功 0、失敗は 1 — **IPL / PT / ローダの書込み失敗、format 失敗、mount 失敗、コピー失敗、sync 失敗のすべて** (現行は共通 `end:` で 0 を返す → 非ゼロに統一、回復モードの規約と揃える。往復 3 non-blocker)。
- 表示: `[1/3]` `[2/3]` `[3/3]` は維持、`Written KERNEL (LBA 6)` の行を `vmkernel.lz4 -> /boot (<n> bytes)` に置換。バナー `v4.1`。
- 回復モード (`--recover-settings` / `--revert-settings`) の分岐は不変。
- ホスト TDD (`install_fresh_host.c`): KAPI 贋物 (`ide_identify` は実型の 96 B を書く、`ide_write_sectors` の記録、`ext2_format`、mount、mkdir、`sys_ls` は**大文字**を返す、open / read (負の注入) / write (short の注入) / stat、`vfs_sync` の失敗注入) で通常経路を通し、(1) `/kernel.bin` を読まず LBA 6 に書かない、(2) `/VMKRNL.LZ4` 欠損で LBA 0/1/2 に 1 セクタも書かない、(3) `/hd0/boot/vmkernel.lz4` が長さ一致、(4) 宛先が小文字 (`/hd0/etc/settings.db`、`/hd0/sys/shell.bin`)、(5) `profile` は写らない、(6) read 負 / short write / mkdir 失敗 / **`sys_ls` の負 (列挙途中の I/O 失敗)** / sync 失敗で終了 1 と `[FAIL]`、(6b) LBA 6 は「生カーネルの書込みが無い」で検査する (ローダの LBA 2〜17 の書込みには LBA 6 も含まれるので「LBA 6 に一切書かない」とはしない)、(7) `IdeInfo` の 96 B、(8) 回復モードの分岐が不変 (S3 の `install_recover_host` を再実行)。RED→GREEN を `s3i2_tdd.md` §I に。

## 2. 道具 (S3I2-T)

### 2a. ini ツール: `HDD1FILE` と FDD の扱い (往復 1 の B5 / B6)
- `tools/np21w_ini.py` の `ALLOWED` は「値の白リスト」。**パス値のキーを別表 `ALLOWED_PATHS`** に足す: `HDD1FILE` (値 = `NP21W_DIR` 直下の `[A-Za-z0-9_.-]+\.nhd`、`wslpath` でホスト側に存在すること、Windows 絶対パス `<NP21W_DIR の Windows 表記>\<name>` に展開)、`FDD1FILE` / `FDD2FILE` (**変更後の値**は空だけ = 装着解除。変更前の値は非空の CP932 パスでもよく、そのまま検査せず空へ差し替える。`SVFDFILE=true` の構成では FDD の装着が ini に保存され次回も再装着されるので、HDD ブートの trial では空にする)。既存の固定値キー (`USEGD5430` 等) の検査は不変で、**`transform()` の「変更キーが 1 つでもあれば全キーの存在を要求」は `ALLOWED` (固定値) と `ALLOWED_PATHS` を別々に適用** (live 操作が新しい必須キーに依存しないよう、`ALLOWED_PATHS` の存在要求は `ALLOWED_PATHS` のキーを変更するときだけ)。fail closed / CP932 保持 / 重複拒否 / バイト単位の差し替えは現行の作法。
- **手順の前提** (往復 2 の R1): 現行 `np21w_trial.py` の `_run()` は「承認された PID / 生成時刻の稼働中プロセスが 1 件存在する」ことを最初に確認し、**trial 自身が通常終了 → 終了確認 → 新 ini 作成 → 起動**を行う。したがって受入では **PM が先に NP21/W を終了しない** (稼働中のプロセスを選んで計画を承認し、trial に終了させる)。停止済みからの開始経路は作らない。
- `tools/np21w_trial.py`: 計画 (`make_plan()`) に **`hdd` (HDD1FILE の名前)、`fdd_eject` (FDD1/2FILE を空にする)、`fdd_arg` (起動引数に付ける `.d88` の絶対パス、`NP21W_DIR` 直下、任意)** を足し、`bind_trial()` の承認対象に含める。起動コマンドは `exe + /i<trial ini> [+ <d88>]` に拡張し、PowerShell 側と Python 側の照合も同じ形に。Cirrus 系のキーは**この票では触らない** (計画に含めない = 変更集合は明示したキーだけ)。現行 trial が持つ `e_resume=false` の強制と、欠落 / 重複 / 未知値の拒否は**維持**し、承認計画に残す。
- `np21w_ini_live.py` (ライブ変更) には足さない (作業 NHD を差し替える事故を作らない)。
- 「復元」は無い: trial は原本 ini を書き換えず、通常の ini で起動し直すだけ (往復 1 non-blocker の文言)。
- ホスト TDD: `transform()` の `ALLOWED_PATHS` (名前規則 / 存在 / 重複 / 別 section / CP932 保持 / 変更前の非空 FDD 値を空にできる / 新しい絶対パスの符号化 (ASCII) と長さ / 固定値キーの検査が変わらない)、`make_plan()` / `bind_trial()` の新フィールドと起動コマンドの照合 (d88 あり / なし)、`np21w_trial_tdd.md` に追記。実 ini・実プロセスには触れない。
- **適用の承認は別** (スキル §0): PM が受入のたびに [D2] で承認を取る。

### 2b. `tools/mk_blank_nhd.py` (往復 1 の B4)
- `--out <path> --size-mb <n>` (既定 200。**受付範囲 1 ≤ C ≤ 65535** = NP21/W が `C=0` / `C>=65536` を拒否する (`sxsihdd.c`) ので、それに収まる容量だけ通す: 約 0.07MB〜4351MB)。**ジオメトリは H=8、S=17、セクタ長 512 に固定**し、`C = floor(size_mb × 1024 × 1024 / (8 × 17 × 512))`、**ファイル本体は C × 8 × 17 × 512 B ちょうど** (指定容量をシリンダ境界へ切り下げ、実容量とヘッダの C×H×S を一致させる。NP21/W はヘッダの C×H×S を総セクタ数として扱う `sxsihdd.c:195`)。ヘッダ 512B (`T98HDDIMAGE.R0`、`dwHeadSize` 512、C / H / S / 512) は `tools/nhd_deploy.py` の定数と同じ値を書く。`--force` 無しでは既存を上書きしない。表示に `C=<n> capacity=<bytes>`。
- ホスト TDD: ヘッダのバイト列、`C` の計算 (100 / 200 / 端数)、本体長 = C×H×S×512、`--force` 無しの拒否、`nhd_deploy.py` の定数との一致。

## 3. 受入 (PM / テスター。**使い捨て NHD だけを触り、作業 NHD は最後まで触らない**)

| ID | 手順 | 合格 |
|---|---|---|
| F1 | ホスト: `mk_blank_nhd.py --out <NP21W_DIR>/os32_fresh.nhd --size-mb 200`。NP21/W は**稼働中のまま** (作業 NHD で通常運転) → `np21w_trial.py` に稼働中プロセス (PID / 生成時刻) と計画 `hdd=os32_fresh.nhd, fdd_eject, fdd_arg=os32_boot.d88` を渡して [D2] で承認 → **trial が通常終了 → 終了確認 → 新 ini + d88 引数で起動** (PM は taskkill も先の終了もしない) | FDD ブートが上がる (`ls -l /` が `LOADER.BIN` / `VMKRNL.LZ4`)、`ls /hd0` は未マウント (ext2 無し) |
| F2 | `install` → `WARNING … ERASED` に `Y` | `[1/3]` IPL / PT / LOADER、`[2/3] Format OK`、`[3/3]` に `vmkernel.lz4 -> /boot (<当該ビルドの vmkernel.lz4 の大きさ> bytes)` と `/sys` `/bin` `/sbin` `/etc` の `[OK]` (小文字)、`Installation complete`、終了 0 |
| F3 | 稼働中の trial プロセスを選び、2 つ目の計画 `hdd=os32_fresh.nhd, fdd_eject` (d88 引数なし) を [D2] で承認 → trial が通常終了 → 新 ini で起動 | **root が hd0** (`ls -l /` が ext2 の `boot` / `sys` / `bin` … 小文字、`FDD` の名前が無い)、`ver` が応答、kselftest = 当該ビルドの `kselftest_pass` / 0、`ls -l /boot/vmkernel.lz4` = 当該ビルドの大きさ、`cfg status` = `OK schema_version 1` (**seed 済み** = S0-T の T1 完了)、`cfg list` に tsv の 3 行、`ls /etc` に `profile` が無い |
| F4 | 通常配備は行わない (`nhd_deploy.py` は trial の NHD を知らない。配備保護は S0-D 済み) | 記録のみ |
| F6 (F5 より前) | **同じ使い捨て NHD** で S3 の回帰: 新規 HDD には `settings.tsv` が無い (FDD の `/etc` は profile と settings.db だけ、往復 2 の R3) ので **`cp /sys/boot_hdd.bin /etc/settings.db`** で壊し、**`cfg status` = CORRUPT と `sync` の成功を確認してから** → 稼働中の trial プロセスで計画 (`hdd=os32_fresh.nhd, fdd_arg=os32_boot.d88`) → FDD ブート → `install --recover-settings hd0` → `Y` → 稼働中プロセスで計画 (`hdd=os32_fresh.nhd, fdd_eject`) → HDD ブート → `cfg status` OK | 合格 (S3 の I2 と同じ) |
| F5 | 後始末 (最後): 稼働中の trial プロセスを通常終了 (trial の終了経路、または NP21/W のメニュー) → **通常の ini** (原本。trial ツールは終了後の baseline を上書きしない — NP21/W 自身の保存は別) で起動 → 作業 NHD で `cfg status` OK、`ls -l /boot/vmkernel.lz4`、`cfg list` が受入前と同じ (受入前に控えた `cfg export` と一致)。`os32_fresh.nhd` は残す | 作業 NHD が受入前と同じ |

## 4. レビューで見てほしい点
1. Phase 1 で `/kernel.bin` を外しても IPL / ローダ v3 が `/boot/vmkernel.lz4` を見つける前提 (`boot/loader_hdd.bin` = v3、`HDD_PARTITION_LBA`、ext2 の位置) が FDD の同梱物と一致するか。
2. `/VMKRNL.LZ4` の事前検査が承認前に置かれ、失敗で 1 バイトも書かないか。lz4 (470KB) が 128KB バッファの反復で写り、長さ一致で確認されるか。
3. 宛先名の小文字化の範囲 (媒体 → HDD だけ) と `profile` を写さない判断。失敗の伝播 (copy_directory の件数、vfs_sync) が main の終了コードまで届くか。
4. ini ツールの `ALLOWED_PATHS` (`HDD1FILE` / `FDD1FILE` / `FDD2FILE`) と trial の計画拡張 (`hdd` / `fdd_eject` / `fdd_arg`、起動コマンドの照合) がスキル §0 / §1 と矛盾しないか。CP932 パス (`NP21W_DIR` は ASCII のみでよいか)。固定値キーの live 操作が新しい必須キーに依存しないか。
5. `mk_blank_nhd.py` のヘッダ (H=8、S=17、C は容量から、本体長 = C×H×S×512) と NP21/W の容量算出 (`sxsihdd.c:195`) の一致、`ide_identify` の total_sectors。
6. 受入 F1〜F6 の順序 (F6 を F5 の前に) で作業 NHD を触らないこと (使い捨てイメージの同一性と IDE 第 1 スロットの対応を実受入で確認)、FDD の装着解除で HDD ブートが保証されること、trial 自身が通常終了する手順 (PM は先に終了しない、taskkill 不可)。
7. S3I2-K (`fatfs_vfs_list` のエラー伝播) の範囲と、FAT を root にした FDD ブートの他の呼び手 (`ls`、`copy_directory`) への影響。シェルの `ls` (`cmd_dir.c:43`) は戻り値を見ないので途中失敗を表示できない — 本票の対象外 (台帳 INHERITED_BUGS.md へ)。

## 5. ユーザー判断が要る点
- なし (使い捨て NHD の作成と trial ini の適用は受入時に [D2] で個別に承認を取る)。

## 6. レビュー記録

| 版 | 判定 | 要旨 |
|---|---|---|
| 第 3 版 | **Approve** | R1〜R3 解消、到達可能な反例なし。non-blocker 4 件 (中間状態の表現、終了コードの範囲、trial の e_resume 維持、`ls` の診断は台帳へ) を第 4 版に反映 |
| 第 2 版 | Request changes | 3 件: R1 現行 trial は稼働中プロセスの選択が前提で、先に通常終了すると `selected identity mismatch` → trial 自身に終了させる手順に統一、R2 FAT の列挙途中の I/O エラーが `VFS_OK` で握りつぶされ欠けたファイルのまま終了 0 → S3I2-K で `fatfs_vfs_list` を直し `sys_ls` の負を失敗に数える、R3 F6 の破損元 `settings.tsv` が新規 HDD に無い → `/sys/boot_hdd.bin` で壊し CORRUPT を確認してから回復。non-blocker: C の受付範囲、FDD 値の変更前後、LBA 6 の検査表現、保護の表現 |
| 第 1 版 | Request changes | 7 件: B1 FAT の列挙名が大文字で HDD に `SETTINGS.DB` ができる → 宛先を小文字に正規化、B2 `IdeInfoTemp` (92 B) が実型 96 B より小さく領域外書込み → 実型を使う、B3 コピー / sync の失敗が伝播しない → 件数と戻り値を main まで、B4 `--size-mb` とシリンダ数の丸写しが矛盾 → H/S 固定で C を算出し本体長を一致、B5 現行 trial は自動起動で d88 引数を渡せない → 計画に `fdd_arg` を足し照合も拡張、B6 `SVFDFILE=true` で FDD が再装着され HDD ブートにならない → `fdd_eject` (FDD1/2FILE を空に) と root の確認、B7 F6 が作業 NHD に戻った後 → F5 の前に。non-blocker: 中間状態の明記、バッファは 128KB、profile の矛盾、ini 境界、文言 (restore 無し、期待値は当該ビルド) |

## 7. 実装と受入の記録 (PM、2026-09-14)

### 7a. 着地
- `b30b3f5` S3I2-K (fatfs_vfs_list、host 11/11、突然変異 4 種)、`49ee29c` S3I2-I (install v4.1、host 9/9 + `--target` の IdeInfo 96B 表明、回復 14/14 不変、install.bin 19,632 B)、`cd1e136` S3I2-T (ALLOWED_PATHS、trial の hdd / fdd_eject / fdd_arg、mk_blank_nhd.py、host 114 + 13)。`make all` / `check` exit 0 (vmkernel 470,687 B)。

### 7b. Codex 実装レビュー
| 対象 | 判定 | 要旨 |
|---|---|---|
| 3 コミット (往復 1) | Request changes | 5 件: B1 install の初期ディレクトリ mkdir の失敗を無視して終了 0、B2 trial の計画に HDD の解決済み絶対パスが残らず `NP21W_DIR` の変更で別ディスクになる、B3 `wslpath` 出力の `.strip()` で末尾空白のディレクトリの存在確認と書込み先がずれる、B4 新しいパス値の `;` / `#` を許し再処理できない ini を生成、B5 `.nhd` / `.d88` 名のディレクトリを受理。non-blocker: install 試験の不足 (列挙途中失敗の接続、64/65、深さ 4/5、EOF 長さ不一致、綴り保持)、既存の IPL / ローダ検査の緩さ、スキル文書の旧記述 |
| `44e7899` (I) + `a80f7b0` (T) (往復 2) | Request changes | B1〜B4 解消、B5 は名前入力と trial で解消。残 1 件: ini CLI の絶対パス入力 (`--set HDD1FILE=C:\…`) が `resolve_image()` を迂回し存在 / 通常ファイル / 配置先の検査なしで prepare が通る → CLI は名前だけを受ける。non-blocker: `_bound_image()` の説明、PowerShell 試験の位置づけ |

### 7c. 受入の準備 (2026-09-14)
- `mk_blank_nhd.py --out <NP21W_DIR>/os32_fresh.nhd --size-mb 200` → `C=3011 capacity=209661952` (ファイル 209,662,464 B = 作業 NHD と同じ大きさ)。
- trial の dry-run: `--baseline` は **Windows 表記** (`<dir>\np21x64w.ini`) で渡す (WSL パスだと `path_key` が拒否し、CLI は理由を出さず `invalid setup` になる — 票 §2a の実装細部。スキル文書に追記)。稼働中 NP21/W の PID / 生成時刻を `Get-CimInstance Win32_Process` で取り、`hdd=os32_fresh.nhd, fdd_eject, fdd_arg=os32_boot.d88` の計画が束縛できることを確認。`--execute` はユーザーの [D2] 承認待ち。

### 7d. 受入 (使い捨て NHD `os32_fresh.nhd`、2026-09-14、ユーザー承認「実行自体は承認」)
| ID | 結果 |
|---|---|
| F1 | **合格 (機能)**: trial (`hdd=os32_fresh.nhd, fdd_eject, fdd_arg=os32_boot.d88`) が作業 NHD の NP21/W を通常終了 → 原本 ini は不変 (trial ini は `HDD1FILE` の 1 行だけ差) → `"exe" "/i<trial ini>" "<d88>"` で起動。ゲストは FDD ブート (`LOADER.BIN` / `VMKRNL.LZ4`)、`/hd0` は未マウント (空 HDD)。**ツールは start 段の identity 検査で失敗を返した** (exe の大小文字比較と created の精度) → `b2416a5` で修正、F3 で実証 |
| F2 | **合格**: `install` → `[y/N]` に `y` → `[1/3]` IPL (geom 8/17) / PT (end cyl 3010) / LOADER 2707 B、`[2/3] Format OK`、`[3/3]` `vmkernel.lz4 -> /boot (470687 bytes)`、`/sys` `/bin` `/sbin` `/etc` が**小文字**で写る (`/bin/CFG.BIN -> /hd0/bin/cfg.bin` 等)、`/etc` は `settings.db` だけ (profile 無し)、`Installation complete`。`/hd0` に boot / sys / bin / sbin / etc / usr / data / home / tmp |
| F3 | **合格**: 2 つ目の trial (`hdd=os32_fresh.nhd, fdd_eject`、d88 なし) → 起動 (`"exe" "/i<trial ini>"`)。ゲストは **HDD ブート** (`ls -l /` が ext2 の boot / sys / bin / sbin / etc / usr / data / home / tmp、FDD の名前無し)、`ver` 応答、kselftest **87 / 0**、`/boot/vmkernel.lz4` = 470,687 B (当該ビルド)、`cfg status` = `OK schema_version 1` (**seed 済み**)、`cfg list` に tsv の 3 行、`/etc` は `settings.db` だけ (profile 無し)。`klibc_test` は FDD の最小コマンドに無いので入っていない (期待どおり)。ツールは start 段を通過 (`process` に起動行) したが **dispose 段で失敗を返した** → T へ |
| F6 | **合格** (使い捨て NHD 上、F5 の前): `cp /sys/boot_hdd.bin /etc/settings.db` → `cfg status` = `CORRUPT sqlite=26`、`sync` → 3 つ目の trial (d88 付き) で FDD ブート → `/hd0/etc/settings.db` 512 B → `install --recover-settings hd0` → `Y` → `recovered: schema_version 1, 3 keys, sync=0, reopen=ok, close=ok` (`.bak` 512 B、印) → 4 つ目の trial (d88 なし) で HDD ブート → `cfg status` OK、`cfg list` に 3 行 |
| trial ツール | 4 回とも起動は成功。ただし結果 JSON は `stage: dispose` で `ok: False` — 原因は起動した NP21/W が PowerShell の stdout パイプを継承し、reader が EOF に届かず cleanup timeout になること (T の解析)。ゲストの動作には影響しない。**PM 判断: ③ `UseShellExecute=$true` で継承を断つ** (実走で `ok: True` を確認してから F5) |
| trial ③ | **合格**: `UseShellExecute=$true` (`2c5aeb3`) の後の trial (`hdd=os32_fresh.nhd, fdd_eject`) は `ok: True, stage: verified, started_pid` で完走、ゲストは HDD ブートで `cfg status` OK |
| F5 | **合格**: trial セッションを止め (trial ini のセッションなので taskkill 可)、通常の ini で起動 (引数なし、`HDD1FILE` は作業 NHD)。作業 NHD は受入前と同じ: `cfg status` OK、`cfg list` の 3 行、`/etc` (settings.db 3072 / .bak 1406 / .failed 3072 / 印 / fixture / tsv)、`/boot/vmkernel.lz4` 470,761 B (受入前の配備のまま)、原本 ini の mtime は F1 の通常終了時 (07:46) から不変 |
| 教訓 | install の確認は `[y/N]` (小文字 y)。受入の道具 (`fdd_run2.py`) は `[Y/N]` しか見ておらず 1 回目は `N` で中止した (DB / HDD への影響なし) |
