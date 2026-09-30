# TASK_KAPI_DATA_FIELDS — KAPI のデータ欄 (sbrk_heap_limit / shm_base) が関数追加のたびにずれ、旧バイナリが黙って壊れる

> 状態: **受入完了 (2026-09-24)** — NP21/W で v63 へ移行 (CD 新規インストール、kselftest 210/210、05388d1f)。v2.1 に同梱。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-09-23) / それまでの状態: **実装済み・実装レビュー Approve (2026-09-24、Codex / Opus のラリー 2 で両者 Approve)** — NP21/W / 実機の受入は v63 への移行 (CD インストール) で — ユーザー指示「別票を着手」(2026-09-24)。カーネル層 (KAPI / exec) の既知の欠陥なので POLICY_DEV §1 に沿って新機能より先に扱う。
> 出所: キーボード修正 (bda95fa / f924275、KAPI v62) の実装レビュー。ラリー 1 で Codex が blocker、Opus が非 blocker と判定が分かれ、ラリー 2 で**両者とも「この commit 固有ではない構造問題、別票 (b)」で一致**。

## 事実

- 生成器はデータ欄を関数表の直後に置く (`exec/exec.c:125`、`tools/check_kapi_version.py:71`)。関数を 1 つ足すたびに `sbrk_heap_limit` / `shm_base` のオフセットが 4 バイト動く (KAPI_SPEC の版表: v50 0x348 → … → v61 0x39C/0x3A0 → v62 0x3A0/0x3A4)。
- exec の検査は `min_api_ver > KAPI_VERSION` だけ (`exec/exec.c:1446`)。OS32X ヘッダにビルド時のデータ欄配置を示す欄は無い。
- **反例 (到達可能)**: v61 でビルドした `_sbrk` を使う C アプリを v62 で起動 → 0x39C (= v62 の `kbd_diag` のトランポリン番地、< 0x500000) をヒープ上限と読む → `sdk/crt/syscalls.c:140` で malloc が全部 ENOMEM。`shm_base` はヒープ上限に化け、libos32db の結果読みと GUI スロットが狂う。**逆方向も到達可能**: `make deploy` + `hsync` だけで `deploy-kernel` をしないと、v62 ビルドの userland が v61 カーネルに載る。`min_api_ver` が低いまま配置に依存するアプリの例: `gshell` (50、Rust の shm_base)、`db_v50_test` (shm_base)、`heap_test` (sbrk_heap_limit)。

## 当面の運用 (現行の [ABI3])

KAPI を上げたら `make clean` → `make all` → `make external` → **カーネルとユーザーランドを同時に全配備** (HostDrv だけ・旧バイナリの持ち込みは守備範囲外)。

## 最小案 (Opus 案、レビュー前)

1. mkos32x が OS32X ヘッダに「データ欄のオフセット (= 8 + 4 × 関数数)」を書く。
2. exec はそれがカーネルの値と違えば `rebuild required` で断る (load_addr の照合と同じ作法)。
3. 欄の無い旧ヘッダは一度だけ全部断る (全再ビルド)。
4. 恒久策 (データ欄を固定オフセットへ: 関数表の容量を予約してその後ろ、またはアクセサ KAPI) はその後。

## 方針 (レビュー対象) — まず検出、恒久策は同じ票の段 2

**段 1 (検出、KAPI v63)**
1. `sdk/gen_kapi.py` が `KAPI_DATA_FIELDS_OFF` (= 8 + 4 × 関数数、今のデータ欄の先頭オフセット) を生成物に出す。`tools/check_kapi_version.py` が一致を検査する。
2. OS32X ヘッダを **v3** にし、末尾に `kapi_data_off` (u32) を追記 (`OS32X_HDR_V3_SIZE`)。`tools/mkos32x.py` は SDK の生成物からこの値を焼く (C / Rust / 外部 repo のビルドはすべて mkos32x を通る — 確認する)。
3. exec (`exec/exec.c:1446` 付近) は、**ヘッダ v3 未満、または `kapi_data_off` が現在のカーネルの値と違えば**「`rebuild required (KAPI data layout)`」で断る。**常駐シェルと共有ライブラリ (`libos32gui.shlib`) にも同じ検査** (shlib のローダ側)。旧バイナリは一度だけ全部断られる = `make clean` → `make all` → `make external` → 全配備が要る (ROADMAP / 08_build に明記)。
4. hsync は現行の「KAPI v53 未満のカーネルを断る」に加え、`/host` の名札の KAPI 版とカーネルの版が違えば警告する (配置違いの持ち込み防止、逆方向の反例)。

**段 2 (恒久策、段 1 の後で別の着地)**
5. データ欄を**固定オフセット**へ移す: 関数表の容量を予約 (例: 512 スロット = 0x808 まで) し、`sbrk_heap_limit` / `shm_base` をその後ろに固定する。以後の関数追加でデータ欄は動かない。ヘッダ v3 の `kapi_data_off` はその固定値になり、検査は残す。
6. 段 2 は KAPI 構造体の大きさが変わる ABI 変更なので、段 1 の検出が入った後に行う (旧バイナリは段 1 の検査で確実に断られる)。

受入: ホスト試験 (mkos32x がヘッダ v3 を焼く、exec の判定関数: v2 → 断る、v3 で値違い → 断る、一致 → 通す)、NP21/W で旧バイナリ (v62 でビルドしたもの) が `rebuild required` で断られ、作り直したものは動く、shlib の検査、kselftest。

## 方針 v2 (ラリー 1 を反映、段 1 / 段 2 を置き換える) — **検出と固定を同じ v63 で 1 回だけ** (両者の代案、全再ビルドを 2 回にしない)

1. **固定配置**: 関数表の容量を **R = 300 スロット**予約し、データ欄 (`sbrk_heap_limit` / `shm_base`) を `8 + 4×300 = 0x4B8` に固定する。R は「トランポリンの 1 ページ (`sizeof(KernelAPI)` + スタブ 8B×R + 写し場 256B ≤ 4096 → 12R + 272 ≤ 4096 → R ≤ 318)」から決めた (Codex/Opus B-3)。スタブも R 本ぶんを前提にした STATIC_ASSERT、関数数が R を超えたら `sdk/gen_kapi.py` が生成を拒否する。**CPL=3 の表 `tbl[...]` の初期化・アプリ切替 (`exec/exec.c:945`)・起動時更新 (`:1870`) もすべて生成した固定オフセットを使う** (Codex B-4)。
2. **照合のヘッダ v3**: OS32X ヘッダの末尾に `kapi_data_off` (u32)。値は包装時ではなく**コードが実際に使った配置**から取る (両者): crt0 / 生成ヘッダが ELF の `.os32_kapi_layout` セクションに `KAPI_DATA_FIELDS_OFF` を置き (Rust は os32api の static)、生成器はそれを読んでヘッダへ写す。無ければ生成器が失敗する。
3. **生成器を 1 つに** (両者 B-1/B-2): 実際の生成器は `sdk/mkos32x.py` (SDK 配布 `$(OS32_SDK)/bin` のコピーを含む) と `tools/mkshlib.py`。ヘッダ生成を共通モジュールにし、shlib も v3。生成器は v3 のとき **`min_api_ver` を 63 以上**にする (旧カーネルが v3 バイナリを受け入れない、Codex B-2)。
4. **照合する側**: exec (アプリ)、`kernel/shlib.c` (shlib、不一致なら shlib 無効 → gshell は CUI に落ちる旨を表示)、常駐シェル (不一致なら「`/sys` を作り直して配備せよ」と明示して停止 — 走らせても malloc が壊れる。FD 起動で直す)。v3 検査は version・`header_size`・実読込長が v3 全体を含むことも見る。
5. **hsync**: 名札 (`.deploy/manifest.txt`) に `kapi=` を足し (format を上げる)、カーネルと違えば**既定で断る** (`--force` で越える)。最初の移行 (旧 hsync) は運用で補う。
6. **移行手順** (08_build / ROADMAP に明記): `make clean && make clean-external` → `make all external fd144` → **NHD はエミュレータ停止中に一式** (カーネル・/sys・shlib・userland) → HostDrv だけ・`/sys` を外した hsync は移行完了ではない。配備順は「ユーザーランドを先、カーネルを後」。実機は FD / CD の入れ直し。CI の成果物は全媒体が v63。
7. 受入: gen_kapi の容量拒否、ヘッダ v3 (C / Rust / shlib / 外部 repo) の値がセクションと一致、exec / shlib / 常駐シェルの判定関数 (v2 → 断る、v3 値違い → 断る、一致 → 通す)、NP21/W で v62 のバイナリが `rebuild required` で断られ作り直したものは動く、kselftest。

## 方針 v3 (ラリー 2 を反映、v2 に追記)

- **刻印は翻訳単位ごと** (Opus B2-1): 生成ヘッダ (C) が各翻訳単位で `static const u32 … __attribute__((section(".os32_kapi_layout"), used)) = KAPI_DATA_FIELDS_OFF;` を出す。静的ライブラリ (libos32db / cfg_backend / 外部 repo の .a) も同じヘッダを通るので刻印を持つ。生成器は**全部の値が一致し、1 個以上ある**ことを検査し、1 つでも違えばビルドを失敗させる。Rust は os32api の `#[used] #[link_section]`。`app.ld` / shlib のリンカ台本は KEEP し、**非ロード** (平らなバイナリに入れない)。CRT を使わない asm の試験バイナリは刻印を明示するか対象外を明記。旧 `.raw` と新 ELF の取り違えは生成工程で防ぐ (ELF からしかヘッダを作らない)。
- **旧カーネル + 新 shlib** (Codex B-R2-1): 旧 shlib ローダは `min_api_ver` を見ないので、**shlib 自身の入口 (`shlib_init`) で `api->version < 63` なら初期化を断る** (以後の呼び出しは失敗を返し、GUI アプリはエラーで終わる)。新カーネル側の `kernel/shlib.c` の v3 照合は v2 のとおり。
- **hsync の名札** (Opus 実装 7): 比べるのは版ではなく**配置 (`kapi_data_off`) の不一致**と「host の KAPI 版 > カーネルの版」。v63 以降は配置が固定なので、関数を 1 つ足すたびに `--force` が要ることにはならない。`kapi=` の欠落・不正・未知の format は一致と扱わない。
- 予約スロット 230〜299 は `tbl[2+i]` にトランポリンを置かず (int 0x80 は `slot >= KAPI_FUNC_COUNT` で kill 済み)、カーネル側の構造体は NULL。`kapi_rust_gen.py` の構造体にも R 個分の詰め物。R の残りが少なくなったら次の R を決める票を起こす目安を ROADMAP に。
- 移行手順の書き分け: NHD は停止中に一式 (順序は問わない)。HostDrv + hsync だけで移る場合は「ユーザーランド (`hsync sys` を含む) を先、カーネルを後」だが、旧 hsync は名札を見ないので**初回の移行は NHD 一式か FD / CD の入れ直しで行う**。

## ユーザー決裁 (2026-09-24)

「既存アプリケーションはすべて自作なので再ビルドで解決する問題は確認は入りますが、基本的に再ビルド方向」。→ ラリー 3 の残件 (刻印の無い v62 以前のオブジェクトの混入、Codex B-R3-1 / Opus B-1) は**全再ビルド (`make clean && make clean-external` → 全ビルド → 全配備) を前提に閉じる** (選択肢 a)。確認として、**v63 で crt の大域変数 `kapi` を `os32_kapi_v63` に改名** (生成ヘッダに `#define kapi os32_kapi_v63`) し、作り直し忘れのオブジェクトをリンクで落とす (選択肢 b)。翻訳単位ごとの刻印と全一致検査 (v3) は**取りやめ**、刻印は crt0 の 1 か所 + ヘッダ v3 の照合だけにする。**v64 以降の配備順 (Opus B-2) → 決裁 (a)**: v64 以降の HostDrv 移行は「**カーネルを先、ユーザーランドを後**」と書き直し、hsync の「host の版 > カーネルの版」の拒否は残す (v63 以降は配置が固定なので新カーネルで旧ユーザーランドは動く)。**方針確定、実装中**。

実装で守ること (Opus ラリー 3 の実装メモ): 刻印のセクションは非 alloc (app.ld で KEEP、平らなバイナリに入れない)、カーネル側には刻印を入れない (`__OS32_USERLAND__`)、`os32gui_shlib_init` は断るとき `os32_init(api)` の前に返し旧 KAPI を保存しない + 全エクスポートが初期化成功フラグで失敗を返す (旧 `bind()` は init の戻り値を捨てる)、予約スロット 230〜299 は「未実装」スタブ (CPL=3 は kill、CPL=0 は NOSYS) で埋める (NULL にしない)、asm の試験バイナリ (`--api 39`) の扱いを決める。

## NP21/W 受入 (2026-09-24、c7f1ba2 のビルド)

v63 への移行を CD の新規インストールで実施 (NHD を空に → FD 起動 (API v63) → `cdinst` Normal → `e2fsck -fn` clean、入ったカーネルはビルドとバイト一致 → HDD 起動)。**`ver` API v63、kselftest 210/210 (fail 0)**。`cal` (malloc) と HostDrv からの `cfg list` (SQLite + 固定配置の shm_base) が動く。**v62 の `cal.bin` は `old header (no KAPI layout) (bin=0 kernel=4b8)` → `rebuild required (KAPI data layout)` で断られた**。
記録: CD の Normal パッケージに `cfg` が入っていない (FD には入る) — 既存のパッケージ定義の漏れか意図か未確認。
