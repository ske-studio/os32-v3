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

**最低動作環境の数値はまだ確定していません。** 現時点で分かっているのは設計上の下限で、
**物理 9.6MB 構成以上**です — CPL=3 アプリのスタックが物理 0x7C0000〜0x7FFFFF に固定なので、
8MB ちょうどではアプリ帯がホットデプロイ窓と重なります
([tasks/gui/DESIGN.md §9.3](docs/tasks/gui/DESIGN.md) が正典、2026-09-04)。

2026-09-09 に 8MB 構成 (NP21/W `ExMemory=7`) で実測した結果: 起動し、CUI は動き、
kselftest 43/0、klibc 49/0、定型回帰 6/6、v86 自己試験も OK。ただし `heap_test` の
`mem_alloc` が 0x7A1098 まで伸び、ホットデプロイ窓と PEGC バックバッファの予約
[0x775000, 0x7C0000) に重なりました。**動くが下限を割っています。**

GUI の必要 RAM は別途実測で定義するもので、8MB や 16MB を一律の GUI 最低要件とはしません。
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
| `kernel.bin` | OS32カーネルバイナリ |
| `userland/**/*.bin`, `apps/*/*.bin` | 外部プログラム (OS32X形式) |
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

### ブートしない

- NP21/Wの設定で IDE #0 に正しいNHDファイルが設定されているか確認
- `make deploy` が正常に完了しているか確認

### プログラムが動かない

- `ver` コマンドの Build タイムスタンプが最新か確認
- `make clean` → `make all` → `make deploy` で再ビルド・再デプロイ

### 文字化け

- NP21/Wのフォント設定でPC-98用フォントが選択されているか確認
