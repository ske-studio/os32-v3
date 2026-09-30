# C5: File Manager

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-07)**
> レーン: C / 前提: C4 + W4 + W3 session request  
> 親: [TASKS.md](../../tasks/gui/v12/TASKS.md) / 契約: [CONTRACTS.md](../../tasks/gui/v12/CONTRACTS.md) V12-F  
> 排他: `userland/rust/filer/**`、必要な build/deploy 登録は PM 経由

## ゴール

`/usr/bin/filer.bin` を Win3.1 風の 2 ペイン File Manager として実装し、GUI だけで基本ファイル操作とアプリ起動を完結させる。

KAPI v42 の既存 VFS API だけを使い、KAPI を追加しない。

利用する既存 API:

- `sys_ls`
- `sys_stat`
- `sys_mkdir`
- `sys_rename`
- `sys_unlink`
- `sys_rmdir`
- `sys_open / sys_read / sys_write / sys_close`

## 1. 画面

```text
┌──────────────────────────────────────────────┐
│ File Manager                                 │
├────────────────┬─────────────────────────────┤
│ [-] /          │ name        size   type     │
│   [-] usr      │ gui_demo    ...    BIN      │
│     bin        │ filer       ...    BIN      │
│   etc          │ ...                         │
│   sys          │                             │
├────────────────┴─────────────────────────────┤
│ /usr/bin                                     │
└──────────────────────────────────────────────┘
```

左 pane:

- directory tree の簡易表示。
- 専用 TreeView ABI は作らない。
- existing listbox + indent + `[+]` / `[-]` text で表現。
- 必要な枝だけ lazy load。全 FS を再帰走査しない。

右 pane:

- current directory の entries。
- name / size / type。
- directory を先、file を後にする程度の単純 sort でよい。大文字小文字規則は FS の既存規則に従う。

下部:

- current absolute path。

## 2. Path 規則

- filer 内部は絶対 path を正典とする。
- 最大 255B。
- 255B を超える操作は `ERR` として拒否し、黙って truncate しない。
- path separator は `/`。
- root より上へ `..` しない。

## 3. Navigate

- directory double-click 相当 / RETURN で移動。
- 左 tree selection でも移動。
- current path が変わったら右 pane を 1 回 reload。
- 毎 frame `sys_ls` しない。
- VFS 操作中に X4 相当の処理を要求しない。アプリ自身の event callback から短い KAPI を呼ぶのは可だが、大きい copy は §7 の chunking 作法を使う。

## 4. Launch

`.bin` file を Open/Run すると filer 自身で `exec_run()` しない。

1. C4 `session_launch(abs_path)`。
2. W3 が `LAUNCH(path)` pending。
3. filer は `Quit(REPLACE_APP)` を受ける。
4. 通常 event loop を抜ける。
5. filer 終了後、gshell top-level が次 app を起動。

`session_launch()` が `ERR_FULL / NOSYS / INVAL` なら filer は終了せず MessageBox で表示する。

## 5. File operations

### mkdir

- Input dialog で名前入力。
- `/` を含む名前は v1.2 では拒否。
- current directory + name で `sys_mkdir`。
- 成功後 reload。

### rename

- Input dialog で新しい basename。
- `/` を含む名前は拒否。
- `sys_rename(old,new)`。
- 成功後 reload。

### delete file

- MessageBox Yes/No を必須。
- Yes 後 `sys_unlink`。
- 失敗時は error dialog。

### rmdir

- MessageBox Yes/No を必須。
- `sys_rmdir`。
- recursive delete はしない。non-empty directory の失敗はそのまま表示。

### move

- v1.2 は同一 FS 内だけ。
- destination path を選び `sys_rename`。
- cross-FS fallback copy+delete は v1.2 対象外。

## 6. Copy

file のみ。directory recursive copy は対象外。

- source を read open。
- destination を create/write open。
- 4096B 固定 buffer で read -> write を繰り返す。
- write は short write を考慮し、read した全 byte が書けるまで進める。
- read/write error では両 fd を必ず close。
- 新規作成した destination が途中失敗で partial になった場合は error dialog で明示し、可能なら `sys_unlink` して cleanup。

既存 destination がある場合:

- Yes/No の overwrite confirmation を必須。
- No なら何もしない。
- Yes の open/truncate mode は SDK/VFS の既存定数を使い、数値を filer に直書きしない。

大きな file copy は 1 回の callback で全ファイルを copy して UI を長時間止めない。

v1.2 の作法:

1. copy state (`src_fd`, `dst_fd`, `bytes`, path) を保持。
2. timer または event loop 1 周につき最大 16KB (4 chunks) を処理。
3. 各周で通常 `OP_POLL/WAIT` へ戻る。
4. Cancel UI は任意。CTRL+STOP でも fd owner cleanup が働くこと。

## 7. Context menu

専用 popup Window ABI は使わない。C4 の規則どおり **client surface 内 overlay** とする。

file:

- Run (bin のみ)
- Copy
- Move
- Rename
- Delete

folder:

- Open
- Copy は disabled (recursive copy 対象外)
- Move
- Rename
- Delete

background:

- New Folder
- Refresh

menu rect は client rect 内へ clamp。右クリック位置が端でも画面外へ出さない。

ESC / 左クリック外で close。close 時は menu rect を invalidate し下地を再描画。

## 8. Icons

C4 `GuiIcon16` を使う。

最低 3 種:

- folder
- executable (`.bin`)
- generic file

icon resource は filer の rodata に置いてよい。

## 9. エラー表示

VFS error を握り潰さない。

最低限:

- source not found
- destination exists
- permission / read-only 相当
- directory not empty
- path too long
- read/write error
- session request failure

数値 error code と短い operation 名を MessageBox に出せばよい。

## 10. owner / cleanup

- filer 終了時に開いている fd を残さない。
- copy 中 CTRL+STOP でも exec owner cleanup によって fd が回収されることを試験。
- modal result は consume してから次 modal を開く。
- session launch accepted 後は新しい file operation を開始しない。

## 完了条件 (G3 / G4)

自動 mouse/key 操作で次を通す。

1. `/usr/bin` へ navigate。
2. folder 作成。
3. rename。
4. small file copy。
5. large file copy 中も mouse / clock / Quit が生存。
6. delete confirmation -> delete。
7. non-empty rmdir が失敗して error 表示。
8. `.bin` を Run -> filer 終了 -> 対象 app 起動。
9. context menu が client 端で画面外へ出ない。
10. 9801 / PEGC / Cirrus で同じ操作が成立。
