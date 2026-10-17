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
| `bin/mkos32x.py` | OS32X 実行ファイルヘッダ (v4、60B) の付加。`bin/os32x_hdr.py` を同じ場所に置いて使う |
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

CRT の `_start` → `_start_c` が `main` を名前で呼び、戻り値を終了コードにします
(`sdk/crt/crt0.asm:38`、`sdk/crt/crt0_c.c:47`)。ソース内の関数の順序に制約はありません。

```c
int main(int argc, char **argv, KernelAPI *api);
```

第3引数で `KernelAPI` テーブルを受け取る。OS の機能はすべてここを
経由して呼ぶ (`api->kbd_trygetchar()` など)。

**OS32X ヘッダが要る。** リンクしただけの生バイナリは OS がロードしない。
`mkos32x.py` で最低 API バージョンと要求ヒープサイズを埋め込む。
最低 API バージョンは `KAPI_VERSION` 以下にします。生成定義
`include/os32/os32_generations.h` の `OS32X_MIN_API` が下限 (現在 75) で、これより小さい指定は下限に上がります。

**ヘッダは v4・60B。** `mkos32x.py` は `--elf <app.elf>` と ELF の刻印を照合します。
形式・KAPI 配置・世代の条件は [KAPI_SPEC の世代契約](../docs/KAPI_SPEC.md#t2c-os32x-v4-と生成する世代-kapi-v69) を参照してください。
SDK を差し替えたら旧オブジェクトを混ぜず、全て作り直してください。

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

`KAPI_VERSION` は機能版です。同じ世代で、アプリの最低 API 版を満たすカーネルなら利用できます。
古い SDK のアプリが新しいカーネルで無条件に動くわけではありません。形式・KAPI 配置・ABI 世代・
メモリ世代、および依存する共有ライブラリのプロトコルが一致する必要があります。
世代が違う場合は現行 SDK で再構築します ([世代契約](../docs/KAPI_SPEC.md#t2c-os32x-v4-と生成する世代-kapi-v69))。

## T2c の世代契約

OS32X v4 は60Bのヘッダで、アプリは0x80100000にリンクする。
旧SDKの .o / .a / Rust crate は新CRTと混ぜず、全て作り直す。
C の全コンパイル単位には `-include $(OS32_SDK)/include/os32/os32_unit_stamp.h`、
asm には `-p $(OS32_SDK)/crt/generations.inc` を付ける。
リンクは `python3 $(OS32_SDK)/bin/link_guard.py i386-elf-ld ...`、Rust は
`RUSTC_WRAPPER=$(OS32_SDK)/bin/rustc_stamp.py` を使う。
生成器が記録した `.inputs.json` / `.generations.json` もビルド成果物として保つ。
`example/hello/Makefile` が C の最小例。4世代の正典と形式は
[KAPI_SPEC の世代契約](../docs/KAPI_SPEC.md#t2c-os32x-v4-と生成する世代-kapi-v69)。

USER の newlib 利用プログラムは `lib/libos32nano.a` を `-lc` より前にリンクする
(付属 example を参照)。`libos32nano.json` は CRT と adapter の hash 照合用で、
ライブラリと一緒に配る。`bin/link_guard.py` が選択された全入力を調べ、未結線の
memalign・mallinfo・usable-size 等と重複 morecore を拒否する。
archive の由来とライセンスは `lib/nano.LICENSE`、提供元の hash は
`lib/libos32nano.json` に収録する。
