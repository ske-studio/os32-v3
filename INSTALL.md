# OS32 インストールガイド

## 1. 動作環境

### エミュレータ (推奨)

- **NP21/W** (NekoProject 21/W) — PC-9801エミュレータ
  - Windows版を推奨
  - HDD (NHD) イメージからのブートに対応

### 実機

- NEC PC-9801/9821 シリーズ (i386以上のCPU搭載機)
- IDE HDD または FDD

### メモリ要件

v3 の最低条件は **8MB 機で GUI + 私有 2MB のアプリ** (目標、[V3_PLAN §2-2](docs/tasks/v3/V3_PLAN.md))。固定スタックとホットデプロイ窓は撤去済みで、旧 9.6MB 下限の理由は解消しています。

2026-09-09 に 8MB 構成 (NP21/W `ExMemory=7`) で実測した結果: 起動し、CUI は動き、
kselftest 43/0、klibc 49/0、定型回帰 6/6、v86 自己試験も OK。ただし `heap_test` の
`mem_alloc` が 0x7A1098 まで伸び、ホットデプロイ窓と PEGC バックバッファの予約
[0x775000, 0x7C0000) に重なりました。**当時の旧配置では下限を割っていたという記録です。** 現行構成の判定は v3 の最終段で行います。

8MB GUI の最低条件は上記の目標として維持し、搭載メモリ量ごとの確認は v3 の最終段にまとめます。それまでのゲスト受入は 64MB の 1 構成です。
設計対象と現行実装の区別は [メモリ方針](docs/02_memory.md) を参照してください。

## 2. ビルド環境の準備

WSL (Ubuntu) または Linux 環境で以下のツールが必要です。

### 必須ツール

| ツール | バージョン | 用途 |
|-------|-----------|------|
| `i386-elf-gcc` | 13.x 推奨 | i386クロスコンパイラ |
| `nasm` | 2.15+ | アセンブラ |
| `make` | GNU Make | ビルドシステム |
| `python3` | 3.8+ | ビルドツール・スクリプト |

### クロスコンパイラのインストール

`i386-elf-gcc` (13.2.0) + newlib (**nano構成必須**) をソースからビルドします。
正確な手順・configureオプションは **[docs/08_build.md §8-5](docs/08_build.md)** を参照してください。

```bash
sudo apt install build-essential nasm make python3 python3-lz4 python3-yaml \
  genisoimage libgmp-dev libmpfr-dev libmpc-dev texinfo bison flex
```

Rust プログラムのビルドには rustup も必要です (`rust-toolchain.toml` が nightly を自動解決)。

インストール後、リポジトリ直下に `.env` を作成してパスを設定します:

```env
CROSS_DIR=/home/<user>/opt/cross
NP21W_DIR=/mnt/c/<np21wの場所>
HOSTDRV_DIR=/mnt/c/os32
```

## 3. ビルド

```bash
make clean
make all
```

成功すると以下が生成されます:

| ファイル | 説明 |
|---------|------|
| `build/out/kernel.bin` | OS32カーネルバイナリ |
| `userland/**/*.bin` | 外部プログラム (OS32X形式) |
| `images/os32_boot.d88` | FDDブート用D88ディスクイメージ |
| `images/os32_boot.img` | FDDブート用RAWイメージ |

## 4. NP21/Wでの起動 (NHDイメージ)

### 4-1. NHDイメージの準備

```bash
# 初回セットアップ: NHDイメージのコピー・フォーマット・マウント
make nhd-init

# カーネル・プログラム・データをNHDに書き込み (要NP21/W停止)
make deploy-nhd
make deploy-kernel
```

> ⚠️ NP21/W 実行中は NHD への書き戻しが反映されません。デプロイ前に NP21/W を
> 終了してください (詳細: [docs/POLICY_DEBUG.md §4-8](docs/POLICY_DEBUG.md))。

HostDrv (`C:\os32`) 経由の高速デプロイ (再起動不要) は `make deploy` を使用します。

### 4-2. NP21/Wの設定

1. NP21/Wを起動
2. メニューから **HardDisk** → **IDE #0** に生成されたNHDファイルを設定
3. **Emulate** → **Reset** でリセット
4. OS32が起動し、シェルプロンプトが表示される

### 4-3. 基本操作

```
A:> help              # コマンド一覧を表示
A:> ver               # バージョン情報を表示
A:> ls                # ファイル一覧
A:> edit memo.txt     # テキストエディタを起動
A:> cal               # カレンダーを表示
A:> man grep          # grepのマニュアルを表示
```

## 5. FDDブート (D88イメージ)

1. `make all` で生成された `images/os32_boot.d88` を使用
2. NP21/Wの **FDD1** に `images/os32_boot.d88` をセット
3. FDDからブート

※ FDDイメージは容量制約 (1,232KB) のため、
NHDに比べて搭載できるプログラムが限られます。

## 6. トラブルシューティング

### ブートしない・プログラムが動かない

起動媒体と実行ファイルを [バイナリ反映の確認](docs/POLICY_DEBUG.md#2-バイナリ反映の確認-最優先) に従って照合します。
[配備 3 経路](docs/08_build.md#配備3経路) から対象に合う経路を選び、ユーザーランドは HostDrv 同期後に `hsync`、
カーネルやライブラリ・テストの最終検証は NP21/W 停止 → NHD 配備 → 再起動で確認します ([V1])。
`make deploy` の成功や Build 日時だけで、実行中のバイナリが更新されたとは判断しません。

### 文字化け

- NP21/Wのフォント設定でPC-98用フォントが選択されているか確認


### C ソース静的検査の clang 依存

ビルドは i386-elf GCC のまま。`make check` の C 検査には libclang の
Python バインディングと、全 TypeLoc を辿る C++ 補助器用の LLVM 開発ヘッダ・
libLLVM も必要:

```bash
sudo apt-get install clang libclang-dev llvm-dev python3-clang
```

リポジトリのルートで、バインディングのロードと補助器の組み立てまで確認する:

```bash
python3 -c 'import clang.cindex; clang.cindex.Index.create()'
PYTHONPATH=tools python3 -c 'from clang_ast.type_occurrences import visitor_binary; print(visitor_binary())'
```

補助器は初回に `clang++` でコンパイルし、`libclang-cpp` と `libLLVM` にリンクする。
CI の setup-python では共通部が apt の `/usr/lib/python3/dist-packages` を参照する。
詳細は [docs/08_build.md](docs/08_build.md) §8-4。

クロスツールチェーンを使わない静的 CI (`check.yml`) では、`libnewlib-dev` の
`/usr/include/newlib` を解析用に使う。ローカルのクロス newlib があればそちらを優先する。
clang 本体・libclang・LLVM 開発ヘッダ・libLLVM・resource header は同じ版を使う。
