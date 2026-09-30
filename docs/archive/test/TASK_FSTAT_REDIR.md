# TASK_FSTAT_REDIR — `fstat` がリダイレクトを見ておらず `isatty` と食い違う

> 発行: PM (Claude Code `claude-opus-5`、2026-09-17) / 状態: **受入完了 (2026-09-17)** — 実装とゲスト受入は §6

基点: `feat/gui` = `9c3e442`。
発見: 段 3 のランナー ([`TASK_TEST_RUNNER.md`](TASK_TEST_RUNNER.md)) を実機で回したとき。
**カーネル層 (VFS) なので [`POLICY_DEV.md`](../../POLICY_DEV.md) §1 により新機能より先。**

## 0. 症状

`stat_t` は単独では **5/5 合格**するが、**出力をリダイレクトすると 4/5 で落ちる**。

    $ stat_t                    → stat_t: PASS 5/5
    $ stat_t > /host/st.txt     → stat_t: FAIL 4/5

画面で落ちた 1 件を見ると:

    [OK]   fd=1 is a character device     ← fstat はキャラクタデバイスと答える
    [FAIL] isatty(1) == 1                 ← isatty は端末でないと答える

**同じ fd について 2 つの API が食い違っている。**

## 1. 原因 (確認済み)

`fs/vfs_fd.c`:

```c
int vfs_isatty(int fd)
{
    if (fd == 0 || fd == 1 || fd == 2) {
        /* リダイレクト中はTTYではない */
        if (fd_is_redirected(fd)) return 0;      /* ← 見ている */
        return 1;
    }
    ...
}

int vfs_fstat(int fd, OS32_Stat *buf)
{
    if (fd == 0 || fd == 1 || fd == 2) {
        ...
        buf->st_mode = OS_S_IFCHR | ...;          /* ← 無条件 */
        return VFS_OK;                            /* リダイレクトを見ていない */
    }
    ...
}
```

`vfs_isatty` は `fd_is_redirected()` を見るが、**`vfs_fstat` は fd 0/1/2 なら
無条件でキャラクタデバイスと答える**。リダイレクト先がファイルでも変わらない。

**正しいのは `isatty` のほう。** リダイレクト中の fd 1 はファイルであって端末ではない。

## 2. なぜ見つからなかったか

- **人が対話で叩く限り fd 1 は本当に端末**なので、両方とも正しく見える。
- 自動で回すと出力をリダイレクトするので**そこで初めて食い違う**。
- `stat_t` は 2026-09-17 まで終了コードを常に 0 にしていたので、落ちても緑に見えた
  (票 [`TASK_TEST_RESULT.md`](TASK_TEST_RESULT.md) で直した)。

**3 段構えの穴**だった。API が食い違い、試験がそれを検出できず、終了コードも出ない。
ランナーを作って初めて 3 つとも外れた。

## 3. 直し方 (設計、実装は別)

### 3-1. `vfs_fstat` をリダイレクトに従わせる

fd 0/1/2 がリダイレクトされているなら、**リダイレクト先の実体を返す** —
`st_mode` はファイルなら `OS_S_IFREG`、大きさ・時刻もそのファイルのもの。
`vfs_isatty` が既に `fd_is_redirected()` で見ているので、**同じ 1 か所から引く**こと
([C4])。2 か所で別々に判定すると、また食い違う。

**`fd_is_redirected()` が「リダイレクトされている」しか返さないなら、
`fstat` にはそれだけでは足りない** (先の実体が要る)。何が引けるかを先に調べ、
足りなければ**引ける形にするところから**。

### 3-2. `stat_t` の前提を直す

`isatty(1) == 1` は**対話で叩いたときしか真でない**。ランナーでは必ず偽になる。
主張を「特定の値」から**「2 つの API が一致すること」**に変える。

    fstat(1) が S_IFCHR  ⇔  isatty(1) == 1

これなら対話でもリダイレクトでも成立し、**食い違いそのものを捕まえる**。
今回の不具合はこの形の主張があれば最初から落ちていた。

## 4. 受入

| ID | 反例・操作 | 期待 |
|---|---|---|
| F1 | ゲストで `stat_t` (対話) | `PASS 5/5`。`fstat` が S_IFCHR、`isatty` が 1、**一致** |
| F2 | ゲストで `stat_t > file` | `PASS 5/5`。`fstat` が S_IFREG、`isatty` が 0、**一致** |
| F3 | `make check-guest` | **不合格 0 件** |
| F4 | ホスト試験 | リダイレクトの有無で `fstat` の `st_mode` が変わることを見る。無条件 S_IFCHR に戻す変異で RED |
| F5 | F4 に「2 つの API が一致する」検査 | 片方だけ直す変異で RED |
| F6 | 回帰 | `stat` / `fstat` の既存の検査 (`test_vfs_kind` ほか) が通る |

F5 が肝。**片方だけ直せない形にする** — 今回の根は 2 か所で別々に判定していたこと。

## 5. この票でしないこと

- fd 0/1/2 以外の `fstat`。既に FS へ委ねていて正しい。
- `isatty` の側の変更。こちらが正しい。
- パイプの扱い。リダイレクトと同じ道に乗るなら自然に直るが、**確かめてから**別途。

---

## 6. 実装 (2026-09-17) とゲスト受入 (PM、2026-09-17)

`56b49fd`。判定を `fs/fd_redirect.c` の **`fd_redirect_ifmt()` 1 本**にまとめ、
`fd_is_redirected` / `vfs_isatty` / `vfs_fstat` の 3 つを全部そこから導いた。
`FD_TARGET_FILE` は既存の経路へ委譲するので、**値を作らず `stat <path>` と
`fstat 1` が必ず一致する**。

形でも固定した。`test_fstat_redir.py` の `check_single_source()` が
「3 つとも `fd_redirect_ifmt()` から引いている」「`isatty`/`fstat` が
`fd_is_redirected()` を直に見ていない」を静的に突き合わせる。
**2 か所で別々に判定できない。**

`OS_S_IFIFO` (0x1000) を新設した (票 §5 からのはみ出し、PM 承認)。パイプを
`S_IFCHR` のままにすると「一致」の主張が崩れ、`S_IFREG` は嘘になる。
調べたところ既存の型は 4 本だけで FIFO は無かったので、`S_IFMT` の空きで
POSIX と同じ値を足した。**推測で値を作っていない。**
**KAPI 版数は上げていない** — 共有ヘッダの `#define` 1 本で、スロットも
`KernelAPI` 構造体も `OS32_Stat` の配置も変えていないため。`make external` は回した。

### 受入

| ID | 操作 | 結果 |
|---|---|---|
| F1 | ゲストで `stat_t` (対話) | **合格** `PASS 5/5`。`fstat(1) S_IFCHR <-> isatty(1)==1 (agree)` |
| F2 | ゲストで `stat_t > /host/st2.txt` | **合格** `PASS 5/5`。リダイレクトでも一致する |
| F3 | `make check-guest` | **合格**。16 件中 PASS=14 / SKIP=2、**不合格 0 件、EXIT=0** |
| F4 / F5 | ホスト試験 | **合格**。変異 4 本すべて RED |
| F6 | 回帰 | **合格**。27 件すべて通る。`make check` も EXIT=0 |

起動後: `kselftest` 合格 90 / 失敗 0、`paging_memmap_bad_count` 0、`fault_generation` 0。

### 変異 `isatty_always_tty` が示したこと

**片方だけ直した形**は、F4 の 24 件を **1 件も落とさず**、F5 とパイプの節だけが落ちる。
「`fstat` が S_IFCHR ⇔ `isatty` が 1」という**一致の主張だけがこれを捕まえる**。
「特定の値」を主張する形のままなら、片方を直して満足して終わっていた。

### 5 件目は fd 1 ではなく fd 0 を見る

`cmd | stat_t` では **fd 0 がパイプ**になる。fd 1 だけ見ていると同じ穴をもう一度踏む。
