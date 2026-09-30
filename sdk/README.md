# OS32 SDK

PC-9801 向け 32bit OS **OS32** のアプリケーション開発キット。
OS のソースツリーを持たなくても、この SDK だけでアプリを作れる。

## 収録物

| | |
|---|---|
| `include/` | プラットフォームライブラリの公開ヘッダ (`libos32gfx.h` など) |
| `include/os32/` | KernelAPI の契約 (`os32api.h`, `os32_kapi_shared.h`) |
| `include/rt/` | ランタイム小物 (`dbgserial.h`, `pkg.h`) |
| `lib/` | 静的アーカイブ (`libos32gfx.a` など 25 本) |
| `crt/` | スタートアップ (`crt0.o`, `crt0_c.o`, `syscalls.o`, `help.o`) |
| `link/` | リンカスクリプト (`app.ld`) |
| `bin/mkos32x.py` | OS32X 実行ファイルヘッダ (v3) の付加。`bin/os32x_hdr.py` を同じ場所に置いて使う |
| `rust/` | Rust ターゲット定義と `os32api` クレート |
| `example/hello/` | 動く最小例 |
| `KAPI_VERSION` | この SDK が対応する KernelAPI バージョン |
| `SDK_MANIFEST` | 収録ファイル一覧と SHA-256 |

## はじめかた

```
tar xzf os32-sdk-39.tar.gz
cd os32-sdk-39/example/hello
make OS32_SDK=../..
```

`hello.bin` ができる。ゲストの `/bin/` に置いて実行する。

## 必要なもの

- クロスコンパイラ `i386-elf-gcc` / `i386-elf-ld` / `i386-elf-objcopy`
  (binutils 2.41 + GCC 13.2.0 + newlib nano 構成)
- `nasm` (アセンブリを書く場合)
- Python 3 (`mkos32x.py` 用)

## アプリを書くときの約束

**`main()` はソース中の最初の関数**にすること。crt0 がファイル先頭へ
ジャンプするため、補助関数は `main()` の後ろに前方宣言付きで置く。

```c
void main(int argc, char **argv, KernelAPI *api);
```

第3引数で `KernelAPI` テーブルを受け取る。OS の機能はすべてここを
経由して呼ぶ (`api->kbd_trygetchar()` など)。

**OS32X ヘッダが要る。** リンクしただけの生バイナリは OS がロードしない。
`mkos32x.py` で最低 API バージョンと要求ヒープサイズを埋め込む。
最低 API バージョンは `KAPI_VERSION` 以下にすること (63 未満を渡しても 63 に上がる)。

**ヘッダは v3 (KAPI v63〜)。** `mkos32x.py` は `--elf <app.elf>` が必須で、ELF の
`.os32_kapi_layout` (crt0 が置く KernelAPI のデータ欄の配置の刻印) をヘッダの
`kapi_data_off` に写す。刻印が無い ELF (crt0 を付けずにリンクした等) は断る。
OS はこの値が自分の配置と違うバイナリを `rebuild required (KAPI data layout)` で
起動しない。v62 以前の SDK で作ったオブジェクトは crt の `kapi` の実名
(`os32_kapi_v63`) が違うのでリンクで落ちる — SDK を差し替えたら全部作り直すこと。

### グラフィックスの注意

`gfx_present()` は**ダーティ矩形を登録するだけ**で、VRAM への転送は
行わない。実際に画面へ出すには `api->gfx_present_dirty()` を呼ぶ。

```c
libos32gfx_init(api);
gfx_clear(0);
gfx_fill_rect(40, 160, 30, 80, 7);
gfx_present();              /* 全画面をダーティに */
api->gfx_present_dirty();   /* ここで初めて転送される */
```

描画プリミティブは自動でダーティ矩形を登録するので、変化した部分だけ
描けば転送量が減る。16MHz 級の実機では全画面転送のコストが支配的なので、
これが効く。

### 文字列を返す KAPI の寿命

`sys_getcwd` / `vfs_devname` / `path_get_drive` / `path_get_cwd` の返り値は、
アプリ (CPL=3) では**トランポリンページ内の写し**で、**次の KAPI 呼び出しで
上書きされる**。4 本は同じ 1 本の写しを共用するので、どれかを呼べば他の 3 本の
返り値も変わる。使う前に自分のバッファへ写しを取ること。

```c
char cwd[256];
const char *p = api->sys_getcwd();
strncpy(cwd, p, sizeof(cwd) - 1);   /* 次の api-> 呼び出しより前に写す */
cwd[sizeof(cwd) - 1] = '\0';
```

(`db_last_error` / `db_column_text` は共有メモリを返すので対象外。詳細は
OS 側の `docs/KAPI_SPEC.md` の「API関数」の注記。)

## 互換性

`KAPI_VERSION` は KernelAPI のテーブルレイアウトの版数。カーネルが
新しい API を追加するたびに上がる。古い SDK でビルドしたアプリは
新しいカーネルでも動くが、逆は動かない (exec がヘッダの最低 API
バージョンを見て弾く)。
