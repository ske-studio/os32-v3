# libos32asset — アセット・リソース管理ライブラリ設計書

*策定: 2026-04-28*

> OS32ゲームエンジンにおいて、データの「いつメモリに読み込み、いつ破棄するか」を
> 一元管理するライブラリ `libos32asset` の設計思想・API仕様・実装計画を定義する。

---

## 1. 設計背景

### 1.1 なぜ libos32asset が必要か

現在のOS32ゲームライブラリ群は、各ライブラリが独自にファイルI/Oを行っている:

| ライブラリ | 現在のロード方式 | 問題 |
|-----------|----------------|------|
| libos32map | `map_load()` で DB → RAM一括読込 | マップ切替時に画面が止まる |
| libos32chem | `chem_init()` で DB → RAM一括キャッシュ | 同じDBを複数箇所で開く無駄 |
| libtilemap | タイルセット画像を直接読込 | 同じ画像の二重ロード検出なし |
| libos32snd | MMLデータを直接読込 | BGM切替時にロード待ちが発生 |

**共通する課題**:

1. **同じファイルの二重ロード** — マップAとマップBが同じタイルセット画像を使う場合、2回読み込まれる
2. **ロード中の画面フリーズ** — `sys_read()` はブロッキングI/Oのため、大きなファイルで描画が止まる
3. **解放タイミングの不統一** — 各ライブラリが独自に `mem_free()` するため、ライフサイクル管理が散在
4. **パス解決の重複実装** — `"/data/tileset/forest.vdp"` のようなパス構築が各所に散在
5. **フォーマット変換の散在** — VDP画像デコード等のフォーマット変換ロジックが各プログラムに散在している

### 1.2 設計思想

- **「SQLベースのルール定義 + RAMキャッシュ実行」パターンの踏襲** — libos32chem/libos32map で確立済みのアーキテクチャを拡張
- **協調型非同期ロード** — OS32はシングルタスクのため、スレッドではなく「毎フレーム少しずつ読む」yield方式
- **参照カウントによるキャッシュ** — 同じアセットを複数の利用者が共有、最後の利用者が解放したときにメモリ回収
- **既存ライブラリへの非侵襲的統合** — 既存APIを壊さず、内部のファイルI/Oをlibos32assetに差し替え可能

### 1.3 参考にした既存パターン

| パターン | 参考元 |
|---------|--------|
| RAMキャッシュ + 静的配列 | libos32chem (`ChemReaction[64]`), libos32map (`TileProp[256]`) |
| コールバック方式 | VFS `sys_ls()`, `DirCallback` |
| DB接続管理 | libos32db (`db_handle_t`, 最大8接続) |

---

## 2. アーキテクチャ

### 2.1 全体構造

```
┌────────────────────────────────────────────────────┐
│  ゲーム / アプリケーション                           │
│  asset_request("tileset/forest.vdp") でアセット要求   │
├────────────────────────────────────────────────────┤
│  アセットマネージャ層 (Asset Manager)                │
│  参照カウント管理、キャッシュ検索、ライフサイクル制御  │
├────────────────────────────────────────────────────┤
│  ローダー層 (Loader)                                │
│  非同期ストリーミング読込、VDPデコード、フォーマット変換│
├────────────────────────────────────────────────────┤
│  I/O層 (既存KAPI)                                   │
│  sys_open / sys_read / sys_close / sys_stat          │
└────────────────────────────────────────────────────┘
```

### 2.2 ライブラリ依存関係

```
KernelAPI (sys_open, sys_read, sys_close, sys_stat, mem_alloc, mem_free)
     ^
libos32asset (KAPI のみ)
     ^
     ├── libos32map   (タイルデータ・マップファイル)
     ├── libtilemap   (タイルセット画像)
     ├── libos32snd   (BGM/SE データ)
     └── ゲーム本体   (スプライト画像・スクリプト等)
```

> **注意**: libos32chem と libos32db はSQLite経由のため、libos32asset の管理対象外。
> DB接続はカーネルスロット管理 (最大8接続) で既に統制されている。

### 2.3 ディレクトリ構成

```
programs/libos32asset/
    libos32asset.h         公開ヘッダ (全API宣言 + 型定義 + 定数)
    asset_core.c           初期化, 終了, フレーム更新 (非同期ポンプ)
    asset_cache.c          キャッシュ管理 (検索, 参照カウント, 解放)
    asset_loader.c         ファイルI/O + VDPデコード
```

---

## 3. コアデータ構造

### 3.1 アセットハンドル

```c
/* アセットハンドル (外部から操作するための不透明ID) */
typedef i16 asset_handle_t;
#define ASSET_INVALID  (-1)
```

### 3.2 アセット状態

```c
/* アセットのライフサイクル状態 */
#define ASSET_STATE_EMPTY     0  /* スロット未使用 */
#define ASSET_STATE_LOADING   1  /* 非同期ロード中 */
#define ASSET_STATE_READY     2  /* ロード完了、利用可能 */
#define ASSET_STATE_ERROR     3  /* ロード失敗 */
```

### 3.3 アセットタイプ

```c
/* アセット種別 (ローダーの処理分岐に使用) */
#define ASSET_TYPE_RAW        0  /* 生バイナリ (無加工) */
#define ASSET_TYPE_VDP        1  /* VDP画像 (自動デコード → GFX_Surface) */
#define ASSET_TYPE_MML        2  /* MML音楽データ */
```

### 3.4 アセットエントリ (内部構造体)

```c
#define ASSET_MAX_ENTRIES   32   /* 同時管理アセット上限 */
#define ASSET_MAX_PATH      64   /* アセットパス最大長 */

typedef struct {
    char    path[ASSET_MAX_PATH]; /* ファイルパス (キャッシュキー) */
    u8     *data;                 /* ロード済みデータへのポインタ */
    u32     size;                 /* データサイズ (バイト) */
    u16     ref_count;            /* 参照カウント */
    u8      state;                /* ASSET_STATE_* */
    u8      type;                 /* ASSET_TYPE_* */
    /* 非同期ロード用の中間状態 */
    int     _fd;                  /* ロード中のFD (-1=未使用) */
    u32     _loaded;              /* 読み込み済みバイト数 */
    u32     _total;               /* ファイル全体サイズ */
} AssetEntry;                     /* 約88バイト/エントリ */
```

### 3.5 ロード完了コールバック

```c
/* 非同期ロード完了時に呼ばれるコールバック */
typedef void (*asset_load_callback)(asset_handle_t handle,
                                     const void *data, u32 size,
                                     void *user_ctx);
```

### 3.6 ストリーミング設定

```c
/* 1フレームあたりの最大読み込みバイト数 */
/* IDE PIOの実測値: ~500KB/s → 60fps想定で ~8KB/frame */
/* 安全マージンを取って 4KB/frame をデフォルトとする */
#define ASSET_CHUNK_SIZE    4096
```

---

## 4. API設計

### 4.1 システム管理

```c
/* 初期化: KAPIポインタ保持、キャッシュ配列ゼロクリア */
int  asset_init(KernelAPI *api);

/* 終了: 全アセット強制解放、FDクローズ */
void asset_shutdown(void);
```

### 4.2 同期ロード (シンプルなケース)

```c
/* ファイルを即座に全読み込み (ブロッキング)
 * type: ASSET_TYPE_* (VDPなら自動デコード)
 * 戻り値: アセットハンドル、失敗時 ASSET_INVALID
 *
 * 同じpathが既にキャッシュにあれば、ref_count++ して既存ハンドルを返す
 */
asset_handle_t asset_load(const char *path, int type);

/* パスプレフィックスを使った便利関数
 * asset_set_base_path("/data/") 設定後、
 * asset_load("forest.vdp") → "/data/forest.vdp" として解決
 */
void asset_set_base_path(const char *prefix);
```

### 4.3 非同期ロード (画面を止めない)

```c
/* 非同期ロードを開始
 * 即座にハンドルを返す (state=ASSET_STATE_LOADING)
 * 完了時に cb が呼ばれる (NULLなら通知なし)
 *
 * 同じpathがキャッシュ済みなら、即座に READY で返す
 */
asset_handle_t asset_load_async(const char *path, int type,
                                 asset_load_callback cb, void *user_ctx);

/* 毎フレーム呼ぶ — ロード中アセットを少しずつ読み進める
 * ゲームループ内で input_update() の後に呼ぶ
 *
 * 処理内容:
 *   1. LOADING状態のエントリを走査
 *   2. 各エントリにつき最大 ASSET_CHUNK_SIZE バイト読込
 *   3. 読込完了したら VDPデコード等 → state=READY → コールバック呼出
 */
void asset_pump(void);

/* 非同期ロードの進捗取得 (0-100, READY時は100) */
int  asset_progress(asset_handle_t h);
```

### 4.4 アセットアクセス

```c
/* ロード済みデータへのポインタ取得 (READY時のみ有効)
 * state != READY なら NULL を返す
 */
const void *asset_data(asset_handle_t h);

/* データサイズ取得 */
u32  asset_size(asset_handle_t h);

/* 状態取得 */
int  asset_state(asset_handle_t h);
```

### 4.5 参照カウントとライフサイクル

```c
/* 参照カウント増加 (他のモジュールが同じアセットを共有する時)
 * asset_load() で既にキャッシュ済みの場合は内部で自動呼出
 */
void asset_retain(asset_handle_t h);

/* 参照カウント減少 — 0になったらメモリ解放
 * ゲームシーン切替時に各モジュールが呼ぶ
 */
void asset_release(asset_handle_t h);

/* 全アセットのref_countを強制的に0にして解放 (シーン完全リセット用) */
void asset_release_all(void);
```

### 4.6 キャッシュ制御

```c
/* 指定パスのアセットがキャッシュにあるか (ハンドル返却、なければ INVALID) */
asset_handle_t asset_find(const char *path);

/* キャッシュ統計 */
int  asset_cached_count(void);   /* キャッシュ中のエントリ数 */
u32  asset_mem_used(void);       /* 使用中メモリ総量 (バイト) */
```

### 4.7 デバッグ

```c
/* 全エントリのダンプ (kprintf経由) */
void asset_debug_dump(void);
```

---

## 5. 処理フロー

### 5.1 同期ロードフロー

```
asset_load("tileset/forest.vdp", ASSET_TYPE_RAW)
  │
  ├── asset_find("tileset/forest.vdp")
  │   ├── [HIT] → ref_count++ → return 既存ハンドル
  │   └── [MISS] ↓
  │
  ├── 空きスロット検索 (state == EMPTY)
  ├── base_path + path でフルパス構築
  ├── sys_stat() でファイルサイズ取得
  ├── mem_alloc(size) でバッファ確保
  ├── sys_open() → sys_read() 全量 → sys_close()
  ├── type == VDP なら VDPデコード → GFX_Surface 構築
  ├── state = READY, ref_count = 1
  └── return ハンドル
```

### 5.2 非同期ロードフロー

```
asset_load_async("bgm/battle.mml", ASSET_TYPE_RAW, on_loaded, ctx)
  │
  ├── [キャッシュHIT] → 即座にコールバック呼出 → return ハンドル
  │
  ├── [キャッシュMISS]
  │   ├── 空きスロット確保
  │   ├── sys_stat() でサイズ取得
  │   ├── mem_alloc(size) でバッファ事前確保
  │   ├── sys_open() → FDを _fd に保持
  │   ├── state = LOADING, _loaded = 0, _total = size
  │   └── return ハンドル (LOADINGの状態で返す)
  │
  └── 以降は asset_pump() で段階的に読み進める

asset_pump()  ← 毎フレーム呼出
  │
  └── for each entry (state == LOADING):
      ├── remain = _total - _loaded
      ├── chunk = min(remain, ASSET_CHUNK_SIZE)
      ├── sys_read(_fd, data + _loaded, chunk)
      ├── _loaded += 実読み込みバイト数
      ├── _loaded == _total ?
      │   ├── sys_close(_fd), _fd = -1
      │   ├── VDPデコード (type == VDP の場合)
      │   ├── state = READY
      │   └── コールバック呼出
      └── (未完了なら次フレームに続行)
```

### 5.3 ゲーム側の使用例

```c
/* --- シーン初期化 --- */
void scene_init(KernelAPI *api)
{
    asset_handle_t h_tiles, h_bgm;

    asset_init(api);
    asset_set_base_path("/data/");

    /* タイルセットは即座に必要 → 同期ロード */
    h_tiles = asset_load("tileset/forest.vdp", ASSET_TYPE_RAW);

    /* BGMはフェードインまでに間に合えばよい → 非同期 */
    h_bgm = asset_load_async("bgm/field.mml", ASSET_TYPE_RAW,
                              on_bgm_ready, NULL);
}

/* --- 毎フレーム --- */
void scene_update(void)
{
    input_update();
    asset_pump();      /* 非同期ロードを進める */
    chem_update();
    /* ... ゲームロジック ... */
}

/* --- シーン終了 --- */
void scene_cleanup(void)
{
    asset_release(h_tiles);
    asset_release(h_bgm);
    /* ref_count が 0 になったものだけ実際に解放される */
}

/* --- BGMロード完了コールバック --- */
void on_bgm_ready(asset_handle_t h, const void *data, u32 size, void *ctx)
{
    api->snd_bgm_play((const char *)data);
}
```

---

## 6. 非同期ロードの設計根拠

### 6.1 なぜスレッドではなくyield方式か

OS32はシングルタスク・シングルプロセスOS。プリエンプティブマルチタスクもスレッドも存在しない。
したがって非同期I/Oは **協調型 (cooperative)** — ゲームループが毎フレーム `asset_pump()` を呼び、
少しずつファイルを読み進める方式を採用する。

### 6.2 チャンクサイズの決定根拠

| パラメータ | 値 |
|-----------|-----|
| IDE PIO読込速度 | ~500 KB/s (実測) |
| 目標フレームレート | 30 fps |
| 1フレームの猶予 | ~33 ms |
| I/Oに使える時間 (10%目安) | ~3.3 ms |
| 3.3msで読めるバイト数 | ~1.6 KB |
| 安全マージン込みデフォルト | **4096 バイト** |

> `ASSET_CHUNK_SIZE` は `#define` で定義し、ゲーム側で調整可能。
> ローディング画面など描画が軽い場面では大きくして高速化できる。

```c
/* ゲーム側でローディング画面中にチャンクサイズを一時的に増加 */
/* (将来の拡張: asset_set_chunk_size(16384); のようなAPI追加を検討) */
```

---

## 7. リソース使用量の見積もり

| 項目 | サイズ |
|------|--------|
| コード (.text) | ~1.5 KB |
| AssetEntry[32] | 32 × 88 = 2,816 B |
| base_path バッファ | 64 B |
| 内部変数 (api ptr, count等) | ~20 B |
| **合計 RAM (管理構造)** | **~4.4 KB** |
| アセットデータ本体 | 利用者依存 (exec_heap から動的確保) |

PC-98のexec_heap (可変サイズ、Makefileの `--heap` で指定) から確保。
典型的なRPGゲームのアセットメモリ予算:

| アセット | サイズ目安 |
|---------|-----------|
| タイルセット画像 (1セット) | 8-16 KB |
| マップBGM (MML) | 1-2 KB |
| SE効果音 (複数) | 2-4 KB |
| スプライトシート | 4-8 KB |
| **典型的な1シーンの合計** | **~30 KB** |

---

## 8. 設計上の判断ポイント

### Q: キャッシュキーはパス文字列か、ハッシュか？

**パス文字列の直接比較を採用。** 理由:
- エントリ数が最大32個 — 線形走査でも十分高速 (32回の `strcmp`)
- ハッシュ衝突の考慮が不要
- デバッグ時にどのファイルがキャッシュされているか一目でわかる
- C89環境でハッシュ関数を追加するコスト > 利益

### Q: VDPデコードは同期/非同期どちらで行うか？

**同期デコードを採用。** 理由:
- VDPデコードはCPU処理のみ (I/Oなし) で高速
- デコードの途中状態を保持する複雑さに見合わない
- 非同期ロードの場合: 全バイト読込完了後に一括デコード → `GFX_Surface` 構築

### Q: libos32map/libtilemap の内部I/Oを置き換えるか？

**Phase 1では置き換えない (非侵襲)。** 理由:
- 既存ライブラリは安定稼働中であり、内部リファクタリングのリスクが高い
- Phase 1ではlibos32assetを独立ライブラリとして安定させる
- Phase 2でlibos32mapの`map_load()`内部をlibos32asset経由に移行する

### Q: SQLite DBファイルも管理対象か？

**対象外。** 理由:
- DB接続はカーネルスロット管理 (最大8接続) で既に統制されている
- libos32dbが接続のライフサイクルを管理しており、二重管理は危険
- アセットマネージャはバイナリデータ (画像・音楽・スクリプト) の管理に専念

---

## 9. 実装フェーズ

### Phase 1: コア実装

- [ ] `libos32asset.h` ヘッダ作成 (型定義, 定数, 全API宣言)
- [ ] `asset_core.c` 実装 (init, shutdown, set_base_path)
- [ ] `asset_cache.c` 実装 (find, retain, release, release_all, 統計)
- [ ] `asset_loader.c` 実装 (同期load, VDPデコード統合)
- [ ] Makefile 統合 (`LIBASSET_OBJ`, リンク順序)
- [ ] テストプログラム `asset_test.c` 作成

### Phase 2: 非同期ロード

- [ ] `asset_load_async()` 実装 (FD保持, LOADING状態管理)
- [ ] `asset_pump()` 実装 (チャンク読込, 完了検知, コールバック)
- [ ] `asset_progress()` 実装
- [ ] ローディング画面デモプログラム

### Phase 3: 既存ライブラリ統合

- [ ] libos32map の `map_load()` をlibos32asset経由に移行
- [ ] libtilemap のタイルセット読込をlibos32asset経由に移行
- [ ] libos32snd のBGM/SEデータ読込をlibos32asset経由に移行

### Phase 4: 高度な機能 (将来)

- [ ] アセットパック (複数ファイルを1つにまとめた独自アーカイブ)
- [ ] LRUキャッシュ (メモリ上限超過時に最も古いアセットを自動解放)
- [ ] アセット依存グラフ (「このマップにはこのタイルセットが必要」のメタデータ管理)
- [ ] ホットリロード (開発中にファイル変更を検知して再読込)

---

## 10. 検証計画

### ビルド確認

```bash
make all
```

### テストプログラム (`asset_test.c`)

1. **同期ロードテスト**: 既存ファイルを `asset_load()` → `asset_data()` でデータ検証
2. **キャッシュテスト**: 同じパスを2回ロード → ハンドルが同一、ref_count=2
3. **参照カウントテスト**: `asset_release()` 2回 → メモリ解放確認
4. **非同期ロードテスト**: `asset_load_async()` → ループ内で `asset_pump()` → 完了コールバック確認
5. **VDPデコードテスト**: VDP画像を `ASSET_TYPE_VDP` でロード → `GFX_Surface` データ検証
6. **エラーケース**: 存在しないパス → `ASSET_INVALID` 返却確認

---

## 11. Open Questions

> [!NOTE]
> ### Q1: ASSET_MAX_ENTRIES のサイズ → **解決: 32エントリで確定**

> [!NOTE]
> ### Q2: base_path の自動判定 → **解決: SQLで管理する方向性 (別途要計画)**
> 当面は `asset_set_base_path()` でゲスト側プログラムが手動設定。
> 将来的にはアセットパス情報をSQLiteデータベースで管理する方向。詳細設計は別途計画。

> [!NOTE]
> ### Q3: VDP画像の自動デコード → **解決: libos32asset 内で自動デコード**
> `ASSET_TYPE_VDP` でロードすると、ファイル読込後に自動的に VDP → `GFX_Surface`
> 変換を行い、呼び出し側には `GFX_Surface` へのポインタとして提供する。
> Phase 1 から組み込む。

---

## 12. 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| `os32-game:docs/libchem/LIBCHEM_DESIGN.md` (LIBCHEM_DESIGN.md) | libos32chem 設計書 (RAMキャッシュパターン参照) |
| [LIBINPUT_DESIGN.md](../libinput/LIBINPUT_DESIGN.md) | libos32input 設計書 (同一アーキテクチャパターン) |
| [06_filesystem.md](../../06_filesystem.md) | VFS仕様 (sys_open/read/close API) |
| [02_memory.md](../../02_memory.md) | メモリマップ (exec_heap制約) |
| [DEVELOPMENT.md](../../DEVELOPMENT.md) | 技術仕様ガイド (外部プログラム --heap 制約) |

---

*この設計書は libos32asset の実装に先立つ設計ドキュメントであり、*
*実装フェーズの進行に伴い更新される。*
