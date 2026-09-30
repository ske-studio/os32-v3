# 票 S6 — `tar` コマンド (ustar サブセット)

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

設計の正典は [DESIGN.md](../../tasks/settings/DESIGN.md) §6b。ここは S6 の実装記録だけを置く。
基点 `fac0d89` (`feat/gui`)、2026-09-14。

## 1. 範囲

設定のバックアップを束ねるための最小の `tar`。圧縮は持たず、`lz4` を外で掛ける。

```text
tar c ARCHIVE PATH...      PATH... を束ねる (ディレクトリは再帰)
tar x ARCHIVE [-C DIR]     DIR (既定は .) へ展開する
tar t ARCHIVE              中身を一覧する
```

```text
tar c /hd0/backup/etc.tar /etc/settings.json /etc/system.cfg /etc/profile
lz4 c /hd0/backup/etc.tar /hd0/backup/etc.tar.lz4
```

扱うのは**通常ファイルとディレクトリだけ**。リンク・デバイス・所有者・時刻は
持たない (ヘッダの mode / mtime は書くが、展開では使わない)。

## 2. 決めたこと

| # | 決めたこと | 理由 |
|---|---|---|
| D1 | ustar の読み書きは **vendor した `lib/microtar/`** (rxi、MIT、約 450 行)。自作しない | 票の指示と「車輪の再発明を避ける」。GNU tar は GPL + gnulib、busybox tar は GPLv2 |
| D2 | I/O は `mtar_t` の関数ポインタに KAPI (`sys_open` / `sys_read` / `sys_write` / `sys_lseek` / `sys_close`) を挿す。`mtar_open()` (stdio 版) は `MTAR_NO_STDIO` で外す | OS32 の `fopen` は VFS に繋がっていない |
| D3 | 名前は**先頭の `/` を落として**格納する | GNU tar と同じ。`-C` が意味を持ち、`/` へ戻すのは `tar x -C /` |
| D4 | ustar の name 欄は 100B。**99B を超える名前は拒否**して非ゼロ終了 | 100B ちょうどだと NUL 終端できない。作る側と読む側で対称にする |
| D5 | ディレクトリの判別は `sys_stat` の `st_mode` ではなく **`sys_open` が失敗するか** | 種別ビットは FS ごとに違う。`vfs_open` はディレクトリを断る ([06_filesystem.md](../../06_filesystem.md) §6-1) |
| D6 | 一覧は `sys_ls` のコールバックで**名前を集めるだけ**にし、戻ってから開く / 再帰する | POLICY_DEBUG §4-26 (コールバック中に FS を触ると `ext2_g_aux` が潰れる) |
| D7 | `..` を含む名前・絶対パスは展開時に断る | archive 1 本で展開先の外を書き換えられないようにする |
| D8 | ヒープを使わない (静的バッファ。text 19KB / bss 27KB) | `build/app.conf` の既定 (api 7 / heap 0) のままで済む |

### microtar への改変 (3 点。正典は `lib/microtar/README.OS32`)

`microtar.h` は無改変。`microtar.c` だけ:

1. stdio バックエンドと `mtar_open()` を `#ifndef MTAR_NO_STDIO` で囲った。
2. `sscanf(field, "%o", &v)` → 長さで止まる `mtar_octal()`
   (OS32 のユーザーランドは newlib の `sscanf` を使った実績が無い)。
3. `strcpy(h->name, rh->name)` と `strcpy(h.name, name)` → `mtar_copy_field()`
   (**上流のバッファオーバーフロー**。100B ちょうどの名前で 100B の宛先を溢れさせる)。

## 3. 変えたファイル

| ファイル | 中身 |
|---|---|
| `lib/microtar/{microtar.c,microtar.h,LICENSE,README.OS32}` | vendor (新規)。MIT |
| `userland/cmds/tar.c` | コマンド本体 (新規)。`-DHOST_TEST` でホスト試験にも使う |
| `build/programs.mk` | `lib/microtar/microtar_prog.o` と `userland/cmds/tar.elf` の明示規則 (`cmds/%.elf` パターンはライブラリを引けない)、`clean` に `lib/microtar/*.o` |
| `build/sdk.mk` | `check-tools-host` に `test_tar_cmd.py` |
| `tools/tests/test_tar_cmd.py` / `tools/tests/s6_tdd.md` | ホスト試験と TDD 記録 |
| `docs/07_shell.md` / `CLAUDE.md` / `docs/08_build.md` | 外部コマンド一覧、コマンド数 19→20、ソースツリーの `lib/` |

**`userland/deploy.yaml` は変更していない** — cmds の節は
`- host: "userland/cmds/*.bin"` の glob なので、`tar.bin` はビルドされた時点で
自動的に載る ([V2] は満たす)。個別行を足すと二重配備になるので足さなかった。
`build/image.mk` の `FDD_MIN_CMDS` にも入れていない (票の指示どおり)。
`build/app.conf` にも足していない (既定の api 7 / heap 0 / 宣言なしでよい)。

## 4. ゲスト受入項目 (PM へ)

ホストでは全部通っているが、**KAPI 経由の I/O はホスト試験では代替されている**
ので、以下はゲストで見る必要がある。

1. **往復** — `tar c /hd0/etc.tar /etc/settings.json /etc/system.cfg /etc/profile`
   → `tar t /hd0/etc.tar` が 3 本出る → `mkdir /hd0/r` → `tar x /hd0/etc.tar -C /hd0/r`
   → `diff /hd0/r/etc/system.cfg /etc/system.cfg` が無差分。
2. **`lz4` との組合せ** — `lz4 c /hd0/etc.tar /hd0/etc.tar.lz4` →
   `lz4 d /hd0/etc.tar.lz4 /hd0/etc2.tar` → `tar t /hd0/etc2.tar` が 1 と同じ。
3. **`sys_lseek` の後方シーク** — ここが一番の未知数。`mtar_read_header` は
   512B 読んでヘッダ先頭へ戻るので、1 エントリごとに後方シークが入る。
   既存の `sys_lseek` 利用者 (`cfg_backend.c` / `pkg.c` / `cdinst.c` /
   `tilemap_core.c`) は前方 / 一度きりで、この形の実績が無い。
   **ext2 (`/hd0`)・HostDrv・FDD (FatFs) の 3 つで**同じ往復を見る。
4. **端数ブロック** — 1KB 境界に乗らないサイズ (POLICY_DEBUG §4-32) で
   中身が一致するか。`/etc/system.cfg` のような数十バイトのファイルで足りる。
5. **ディレクトリの再帰** — `tar c /hd0/d.tar /etc` (`/etc` 配下は 100B 名に
   収まる) → `tar t` に `etc` とその下が出る。D5 の「`sys_open` が失敗したら
   ディレクトリ」が ext2 / FatFs / HostDrv で同じに効くか。
6. **エラー** — `tar c /hd0/x.tar /nope` が `tar: /nope: No such file or directory`
   で終了コード 1 (シェルから `echo $?`)。
7. **ホストで読めること** — ゲストが作った `etc.tar` を HostDrv 経由で吸い出し、
   ホストの `python3 -c "import tarfile; print(tarfile.open('etc.tar').getnames())"`
   で名前が出る (DESIGN.md §6b の要件そのもの)。

## 5. やっていないこと

- `make all` / `make check` / 配備 / エミュレータ操作 (PM の担当)。
- 圧縮 (`tar czf` 相当)。`lz4` を外で掛ける方針 (DESIGN.md §6b)。
- 100B を超える名前 (ustar の `prefix` 欄 / GNU 拡張 / PAX)。断るだけ。
- 所有者・パーミッション・時刻の復元。ヘッダには書くが展開では使わない。
- シンボリックリンク・ハードリンク・デバイスファイル (OS32 に無い)。
- 追記 (`tar r`) と個別取り出し (`tar x ARCHIVE MEMBER`)。

## PM 受入記録 (2026-09-14)

- 着地 `2ad4203`。`make check` (`test_tar_cmd.py` 8 件を含む) exit 0 (session `smalls-check`)。
- ゲスト (HDD ブート、HostDrv 経由で `hsync`): `tar c /host/e3.tar /etc` → 9 エントリ (ディレクトリ 1 + ファイル 8)、`tar x -C /tmp/y` で全ファイルがサイズどおり復元、ホストの Python `tarfile` で同じ 9 エントリを読めた。`tar t /nope.tar` はエラー。**機能は合格**。
- **未解決 (別票 S6-P)**: ext2 上 (`/tmp` = hd0) の `tar c` は 15B のファイル 1 本 (書庫 2KB) でも `/api/cmd` の 15 秒タイムアウトを超える。`/api/status` の EIP サンプリングは `ide_write_sector_chs` / `ide_read_sector_chs` に集中 (`kernel.map` で解決)。HostDrv では 0.3 秒。書庫自体は正しく作られる (`tar t /tmp/e6.tar` で確認)。tar の書き方 (512B 単位の書き込み + エントリごとの後方 `lseek`) が ext2 の書き込み経路 (`fs/ext2_file.c` の `ext2_write_stream` / `ext2_write`) で 1 回ごとに大量のセクタ I/O を起こしている疑い。**ext2 の小書き込みの性能は tar 固有ではない**ので、S6 の完了条件から外し、別票で ext2 側を見る (小物ではない)。
- `lz4` との組合せ、FatFs (FDD) 上の往復は未実施 (S6-P の後)。
