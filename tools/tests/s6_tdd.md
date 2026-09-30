# 票 S6 (`tar` コマンド) の TDD 記録

対象: `userland/cmds/tar.c` + vendor した `lib/microtar/`
試験: `tools/tests/test_tar_cmd.py` (`make check-tools-host` に登録)
日付: 2026-09-14 / 基点 `fac0d89` (worktree の記録。依頼票は `995bb19` と書いていたが
この worktree が実際に切られたのは `fac0d89`)

実物の `userland/cmds/tar.c` を `-DHOST_TEST -DMTAR_NO_STDIO` で gcc ビルドする。
切り替わるのは I/O の 6 関数 (`io_open_read` / `io_open_write` / `io_read` /
`io_write` / `io_seek` / `io_close` と `io_mkdir` / `io_stat` / 一覧) だけで、
ustar の読み書きは実機と同じ `lib/microtar/microtar.c` が行う。エミュレータ・
NHD・実機には触っていない。

---

## 段取り

この票は「既製の実装を vendor する」ものなので、先に試験を書いてから実装、
ではなく **vendor → 実装 → 試験 → 変異で RED を確認**の順に進めた。
試験が本当に効いているかは、通った後に実装へ**意図的な欠陥を入れて
RED になること**で確かめている (下の R1〜R4)。この順序であることを
そのまま記録しておく ([V4])。

---

## GREEN (最終形)

```
$ python3 -B tools/tests/test_tar_cmd.py
HOST GNU89 -Werror COMPILE PASS
CASE create_readable_by_python      ok x9
CASE extract_python_ustar           ok x6
CASE list_python_ustar              ok x2
CASE reject_long_name               ok x4
CASE missing_input                  ok x7
CASE directory_recursion            ok x8
CASE strip_leading_slash            ok x2
CASE name_exactly_100               ok x5
SUMMARY 8/8 cases PASS, 0 assertion failures
```

見ているもの:

| 区分 | 中身 |
|---|---|
| (a) | `tar c` が作った archive をホストの Python `tarfile` が開き、名前・サイズ (512 の倍数でない 1792B、0B、`TAR_CHUNK` 8192B を 2 回またぐ 20000B)・中身・種別が一致 |
| (b) | Python `tarfile` (USTAR_FORMAT) が書いた archive を `tar x` が展開 (20000B の本体で `mtar_read_data` の分割読みも)、`-C` 有り / 無し、既存ファイルの上書き |
| (b') | `tar t` の一覧が Python の `getnames()` と同じ順・同じ名前 |
| (c) | 150B の名前を拒否して非ゼロ終了。境界の 99B は通る |
| (d) | 存在しない入力 / archive / 引数不足で `tar: <path>: <理由>` と非ゼロ終了。1 つ壊れていても残りは束ね、終了コードは非ゼロ |
| (e) | ディレクトリの再帰 (3 階層 + 空ディレクトリ)。往復して中身が一致、ディレクトリは `isdir()` |
| (f) | 先頭の `/` を落として格納する (GNU tar と同じ) |
| (g) | name 欄ちょうど 100B (NUL 終端できない) の archive をきれいに断る |

---

## RED (変異で試験が効くことの確認)

### R1. `tar.c` の名前長検査を外す + `microtar` の書き込み側の守りも外す

`if ((int)strlen(name) > TAR_NAME_MAX)` → `if (0)`、
`mtar_write_file_header` の `mtar_copy_field` → 上流どおりの `strcpy`。

```
CASE reject_long_name
  FAIL longname.rc: rc=0
  FAIL longname.msg: out=''
SUMMARY 0/1 cases PASS, 2 assertion failures
```

150B の名前が黙って通り (上流の `strcpy` は 100B の `h.name` を溢れさせる)、
archive も壊れる。検査と守りの両方が要る。

### R2. `tar.c` の名前長検査だけを外す (microtar 側の守りは残す)

```
CASE reject_long_name
  ok   longname.rc: rc=1
  ok   longname.msg: out='tar: LLL...LLL: failure'
```

二重の守りの外側だけを外した形。非ゼロ終了は保たれるが、理由が
`failure` になって何が悪いのか分からない。`tar.c` 側の検査は
**メッセージのため**に要る (`name too long for ustar (max 99 bytes)`)。

### R3. 先頭の `/` を落とさない

`archive_name()` の `while (*path == '/') path++;` を削除。

```
CASE strip_leading_slash
  FAIL strip.name: names=['/tmp/os32-tar-mlr_n57q/g/abs.txt']
```

### R4. `microtar` の読み込み側を上流どおりの `strcpy` に戻す

`raw_to_header()` の `mtar_copy_field` → `strcpy(h->name, rh->name)`。

```
CASE name_exactly_100
  FAIL name100.list.rc: rc=0 out='NNNN...NNNN\n'
  FAIL name100.extract.rc: rc=0 out=''
  FAIL name100.extract.nofile: 壊れた名前で書き出さないこと
```

100B ちょうどの名前で `h->name` (100B) を溢れさせ、隣の `h->linkname` を
踏んだうえで**その壊れた名前でファイルを書き出す**。上流 microtar の
バグで、README.OS32 の改変点 3 はこれを断つもの。

---

## 別途確認したこと (試験本体には入れていない)

- **本物の GNU tar との往復** — `tar c` の出力を host の `tar tvf` / `tar xf`
  が読めることを手で確認した。

  ```
  $ ./tarbin c etc.tar s.json system.cfg
  $ tar tvf etc.tar
  -rw-rw-r-- 0/0               9 1970-01-01 09:00 s.json
  -rw-rw-r-- 0/0               9 1970-01-01 09:00 system.cfg
  $ tar xf etc.tar -C o && cat o/s.json o/system.cfg
  settings
  GFX=pegc
  ```

- **実機と同じフラグでのクロスコンパイル** (`build/programs.mk` の
  `PROGRAM_FLAGS` + `-Wall -Wextra -Werror`):

  ```
  i386-elf-gcc ... -DMTAR_NO_STDIO -Ilib/microtar -c lib/microtar/microtar.c  → warning 0
  i386-elf-gcc ... -DMTAR_NO_STDIO -Ilib/microtar -c userland/cmds/tar.c      → warning 0
  ```

  さらに `crt0` 一式と `-lc -lgcc` でリンクまで通し、未解決シンボルが無いことを見た
  (text 19,261 / data 2,208 / bss 27,116)。`make all` は PM の担当なので流していない。

## まだ見ていないこと (ゲスト受入へ)

`docs/archive/settings/TASK_S6.md` §4 に一覧。要点は 2 つ:

- **`sys_lseek` の後方シーク**を短い間隔で繰り返す (`mtar_read_header` は
  512B 読んでヘッダ先頭へ戻る) のは `tar` が初めて。既存の利用者
  (`cfg_backend.c` / `pkg.c` / `cdinst.c` / `tilemap_core.c`) はいずれも
  前方 / 一度きりで、ext2 / FatFs / HostDrv の 3 つを往復させた実績が無い。
- ディレクトリの判別を `sys_stat` の `st_mode` ではなく **`sys_open` が
  失敗するか**で見ている (`vfs_open` はディレクトリを断る、06_filesystem §6-1)。
  FS ごとに挙動が違わないかはゲストで見る必要がある。
