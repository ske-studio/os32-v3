> 出所: ~/os32-tmp/run/kfill-7a/DESIGN.final.md (設計ラリー 3 回 + 失敗の表の往復 3 回、2026-11-20 確定)
> 状態: **現行 (2026-11-20)** — 段 7a の確定設計。末尾の「確定」の節は本文に優先する

# 段 7a 詳細設計 r3 — FEP を PowerPC の像で動かし、IME の 17 本を小分けに公開する

基点 main 9d99c1b64。読んだだけで、ビルド・QEMU・NP21/W・変異は回していない。大きさの PowerPC 側は見積もり (実測は段 S2)。
確かめていない所は「未確認」と書いた。浮動小数点は使わない。KAPI の版・slot の並びは変えない (既存 slot の提供だけ)。この 1 枚で読める (r1・r2 は要らない)。

## r2→r3 の変更 (REVIEW.r2 の指摘ごと)

| 指摘 | 直し |
|---|---|
| P1 rd2 の名前表と `/db` の ramfs | `platform/powermac/root_boot.c` の `pm_root_boot` の名前表を `rd0`〜`rd3` (数 4) に広げ、`rd2` を `/db`、`rd3` を `/dbu` に mount する (§2-1、P4b)。ramfs は装置の番号を使わないのでブロック装置の登録は無し |
| P1 `user_path_valid` と `user_rc` | 静的の像だけ許可ディレクトリを `/dbu/` にする (`kernel/ime/ime_store.h` の `FEP_USER_DIR` を `#ifdef OS32_FEP_STATIC` で分ける。x86 は `/etc/` のまま、fep.mod は同じ)。prepare が経路と `user_rc` を両方入れる (§2-5)。学習 DB の作成と再開の試験を足した |
| P1 64 KiB のピーク | 受入用の辞書は `tools/fep_to_sqlite.py` で作る SQLite の fixture。辞書 2 版 (`/db`) と学習 DB + ジャーナル (`/dbu`) の確保容量のピークを 1 KiB 単位で測る host 試験を P7e の前提にした (§4-3) |
| P1 `ime_dict_boot` の出口 | 今の本体を静的の像だけ `ime_dict_open` の名前にし、新しい `ime_dict_boot` が gate → prepare → open → commit の 1 本道 (§2-2)。写しの途中の HOSTDOWN・STALE は途中のファイルを消して AGAIN。§1-2 と §2-3 を揃えた |
| P1 隔離の判定の位置・アプリ DB の使用中 | 隔離は `dict_loaded` より先に見る。予約の前の `boot_check` が BUSY なら何も変えずに AGAIN (巻き戻しを呼ばない)。READY の後にアプリの DB が開ける試験を足した (§2-3) |
| P1 link-sh と sh.bin の入力口 | 入力元と修飾の覚えを `input_usb.c` の取り出しに置き、link-sh (USB を直に取る、`platform/powermac/input_boot.c` の `provider`) でも走るようにした。7a の IME の入力口は IME の slot を呼ぶアプリで、常駐の sh.bin の行入力 (`kbd_getkey`、`userland/shell/ui.c` の `sh_getkey`) は対象外と明記。kboot-sh・link-sh の FEP=0/1 のリンクの表と、対象外の像の FEP=1 を選択のエラーにする規則を §4-1b に |
| P2 後続の段の媒体 | 製品の辞書の段 (7a-辞書) の媒体の固定・切替の手順・学習の書き先を §2-7 に |
| 反論 1 (r2) | レビュー側が訂正済み。明示の依存と差分ビルドの確かめは残す |

**PM 決定**: U1 = PowerPC の `FEATURE_FEP` の既定は 0 のまま、FEP=1 を明示した像で受入。止める基準は §3 の指標で 52 KB。U2 = 辞書は host (`/host`) を搬送元にし、実行時の置き場は MAIN_DB の能力を持つ媒体。

## 1. 実物の確認

### 1-1 計画の前提と実コードの食い違い (10 件)

| # | 計画・依頼の前提 | 実物 | 設計への効き |
|---|---|---|---|
| 1 | 「fep_module.c → PPC 入力登録」 | `kernel/ime/fep_module.c の fep.mod の読み込み` は fep.mod の読み込みの常駐の当て物。状態と本体は fep.mod の中 (`kernel/ime/ime.c` の `g_ime`)。`module_load`・`module_start` (`fep_module.c のロードと開始`) は PowerPC では PC98_PENDING (`build/sources/common.mk` の `C_COMMON_PC98_PENDING`) | PowerPC は fep_module.c を使わない。本体を静的に結ぶ |
| 2 | 7a-選択で PowerPC も FEATURE_FEP=1 にできる | `build/features/fep.mk` の `C_FEATURES` が無条件に fep_module.c を選ぶ。`fep_imports.h` は x86 だけの `build/fep-module.mk` の `R1_OBJECTS` が作る。`tools/tests/test_build_selection.py` の `check_fep` は設定の照会だけ | 今の PowerPC の FEP=1 は結べない。fep.mk を ARCH で分ける (S1) |
| 3 | 「input の共通の口へ FEP を登録」 | 共通の入力の登録は `input_register(const InputOps *)` (`kernel/input_service.c` の `input_register`) で、PowerMac は起動時に使っている (`platform/powermac/input_boot.c` の `pm_input_boot`)。FEP 専用の登録の口は無い。本体は `kbd_getkey_wait`・`kbd_trygetkey`・`kbd_trygetchar` を引き (`kernel/ime/ime.c` の `ime_getchar_wait・ime_trygetchar・ime_getkey_wait・ime_trygetkey`)、定義は PC-98 の装置 `drivers/input/kbd.c` の `kbd_trygetchar・kbd_getkey_wait・kbd_trygetkey` | FEP 専用の登録の口は作らない。FEP は共通の口 `input_*` の上の読み手 (M1) |
| 4 | USB キーの経路でそのまま受かる | `platform/powermac/input_keymap.c` の `pm_input_keymap` は Space・Return・BS を ASCII だけで返す。FEP は走査符号で見分ける (`kernel/ime/ime_cui_process.inc` の `ime_cui_process`、`kernel/ime/ime.c` の `ime_feed_key`) | キーの値を (走査符号<<8)\|ASCII に (P1) |
| 5 | 同上 | `platform/powermac/input_boot.c` の `ops` の `.shift_state = zero8`。kboot-sh は USB と LAN が `try_key` (`input_boot.c` の `try_key`) で合流し、LAN の矢印は走査符号付き (`platform/powermac/input_decode.c` の `pm_input_decode`)。link-sh は USB を直に取る (`input_boot.c の USB 入力`) | §2-4 |
| 6 | 文字の表示は後端があれば出る | `platform/powermac/ime_display.c` の `ime_display_boot` は常に -1。`platform/powermac/platform_ime_render.h` の `IME_DEFAULT_RENDER` の既定の render は NULL。build/ のどの一覧にも出ない | 起動を通す (P4)、表示は P8 |
| 7 | 辞書の起動は共通 | 起動の控えは `kernel/sqlite_engine.c` の `boot_meta・boot_user_path` の static。書くのは `services_boot` (`services_boot`) と `services_settings` (`services_settings`) だけ。PowerPC は `sqlite_static_boot` しか呼ばない (`platform/powermac/ppc_kernel.c` の `platform_boot_run`) | 控えを 1 ファイルに出す (M3) |
| 8 | 「辞書 VFS」 | `assets/fep.db` は 5,826,560 バイト。ram root は核の像に結ぶ (`build/targets/ppc-powermac.mk` の `ROOT_BLOB・KB_EXTRA`)。MAIN_DB の能力は ext2 と ramfs だけ (`fs/ext2/ext2_vfs.c` の `ext2_sqlite_ops`、`fs/ramfs/ramfs.c` の `ramfs_sqlite_ops`。読むだけの open でも能力を見る、`lib/sqlite3/os32_sqlite_vfs.c` の `os32VfsOpen`)。PowerMac の mount は `/` (iso9660)・`/tmp` (ramfs、`init/boot_root.c` の `boot_root_mount・boot_root_start`)・`/host` (serialfs、能力なし `fs/serialfs/serialfs.c` の `g_serialfs_ops`)。ramfs は mount ごとに 1 ファイル 32 KiB・合計 64 KiB (`fs/ramfs/ramfs.h` の `RAMFS_FILE_MAX・RAMFS_DATA_MAX`)。ext2 は PowerPC の像に無い | §2-1、U2' |
| 9 | 終了時の片付けは本体が持つ | suspend の持ち主の追跡は常駐側 `kernel/ime/fep_module.c` の `suspend_owner・fep_start`。`HOOK_ime` は `FEATURE_PC98_PENDING` (`build/sources/hooks.mk` の `EXEC_HOOKS`) で PowerPC は 0 | S3、M4 |
| 10 | 「IME_SLOTS の直接参照 (IME_SLOTS)」 | 今は `sdk/kapi_target_ppc.py` の `IME_SLOTS`。使うのは `tools/ppc/test_kapi_binding.py` の `generate` | ime_set_render が残るので組は空にならない |

食い違いでない確認: ime_set_render の先は `gui_ime_set_render` (`kernel/gui.c` の `gui_ime_set_render`、PC98_PENDING) で、計画どおり GUI の後。

### 1-2 統合の判断 3 点 (POLICY_DEV §1)

- **同じ役目は既にあるか**: ある。FEP の本体 (`kernel/ime/` の 7 本)、静的に結ぶ型 (`kernel/sqlite_static.c`)、公開表の任意の組 (`sdk/gen_kapi.py` の `input_guard_c`)。新しく書くのは静的の起動 1 本。x86 の `services_boot` は触らず、共通にするのは控え (`fep_boot_config.c`)・`fep_meta_probe`・`os32_sqlite_outer_boot_check`・本体だけ。起動の順は 2 か所に残る (x86 は module の読み込みと 1 つの取引、PowerPC は遅延の再試行があるため)。
- **どこで分けるか**: 機種が持つのはキーの値・打鍵時の修飾・入力元・画面の行の保存・`/db` と `/dbu` の媒体。ローマ字・辞書・確定・持ち主は共通。
- **操作を機種の標準に合わせるか・互換の契約**: PowerMac は USB HID だけを FEP に通す。切替は Shift+Space。

| 契約 (`docs/decisions/ARCH_COMPAT_CONTRACT.md の 4.2 KAPI・入力・メモリ・起動・4.3 表示・字形・Host/SFS・ビルド`) | この段での当てはめ | 両機種の試験 |
|---|---|---|
| AC-INPUT-1 | キー符号は共通の形、try の空は -1。入力元と打鍵時の修飾は能力として別に照会 (`platform_fep_input.h`) | 同じキーの列 → 同じ UTF-8 の列 (x86 は NP21/W、PowerPC は QEMU の USB)。host は同じ本体のソース |
| AC-KAPI-1 | FEP=0 の像は型別の不在の値 | ppc-ime-absent-guest (新設) と x86 の stub の host 試験 |
| AC-HOST-1 | 搬送元の HOSTDOWN・STALE・不在は、途中のファイルを消して AGAIN (再試行可) | fep_static_host.c の「未接続 → 接続」「写しの途中で切断」 |
| AC-DISPLAY-1 | P8 の render は console が safe の時だけ描く。桁と行は console の幾何から | test_ime_display を両機種の backend で |

## 2. FEP を PowerPC で動かす形

| 項目 | x86 (変えない) | PowerPC |
|---|---|---|
| 結び方 | fep.mod。常駐は fep_module.c、本体は `-DOS32_FEP_MODULE` | 本体 7 本を核に静的に結ぶ、`-DOS32_FEP_STATIC`。fep_module.c・fep_body.c は結ばない |
| KAPI の先 | 常駐の当て物 | 本体の同じ名前を直に |
| FEP の無い像 | `stub_api` | wrap が型別の不在の値 (`OS32_HOOK_IME` が 0) |
| 状態の持ち主 | fep.mod の `g_ime` | 核の `g_ime`。キーの輪・修飾・入力元は `input_usb.c`、控えは fep_boot_config.c、起動の状態は fep_static.c |
| IME の入力口 | 常駐シェルと IME の slot を呼ぶアプリ | **IME の slot を呼ぶアプリだけ**。常駐の sh.bin の行入力は `kbd_getkey` (`userland/shell/ui.c` の `sh_getkey`、x86 の sh.bin と同じ) で FEP を通らない。sh.bin を IME に通すのは 7a の外 |
| 未確定の表示 | TVRAM の render | P8 までは無し (render が NULL でも落ちない、`kernel/ime/ime_render.inc` の `draw_utf8・candlist_draw・preedit_clear・preedit_draw`) |
| GUI の render | `gui_ime_set_render` | 不在のまま (GUI の後) |

### 2-1 辞書の置き場と経路 (PM 決定 U2)

| 経路 | 定数 | PowerMac での実体 | 要る能力 |
|---|---|---|---|
| 起動の辞書 | `IME_DICT_PATH` = `/db/fep.db` (`kernel/ime/ime.h` の `IME_DICT_PATH`、変えない) | ramfs `rd2` の `/db`。無ければ搬送元 `/host/db/fep.db` から写す | MAIN_DB (読み) |
| 切替 | `/db/fep_s.db`・`/db/fep_l.db` (`kernel/ime/ime_store.h` の `FEP_DICT_PATH_S・FEP_DICT_PATH_L`) | 同じ `/db`。prepare が搬送元に在る版だけ写す。無い版は本体の今の失敗の値 | MAIN_DB (読み) |
| 学習 | x86 は `/etc/fep_user.db`、許可は `/etc/` 直下 (`kernel/ime/ime_store.h` の `FEP_USER_PATH・FEP_USER_DIR`、`kernel/ime/ime_user_store.c` の `user_path_valid`、変えない) | 静的は `/dbu/fep_user.db`、許可は `/dbu/` 直下 (ramfs `rd3`)。再起動で消える | MAIN_DB (書き) |
| 書き出し | 呼び手の経路 | 書ける mount (`/host`・`/tmp`。SQLite を通さない書き、未確認) | 普通の書き |

```c
/* platform/powermac/root_boot.c の pm_root_boot (P4b) */
        {"rd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 4},   /* rd0 root、rd1 /tmp、rd2 /db、rd3 /dbu */
/* boot_root_start() の後、FEP の像だけ (#if OS32_HOOK_IME) */
    rc = vfs_mount("/db", "rd2", "ramfs");   if (rc) return rc;
    rc = vfs_mount("/dbu", "rd3", "ramfs");  if (rc) return rc;
/* kernel/ime/ime_store.h の FEP_USER_PATH・FEP_USER_DIR (M1 と同じ段の型で、静的だけ) */
#ifdef OS32_FEP_STATIC
#define FEP_USER_PATH "/dbu/fep_user.db"
#define FEP_USER_DIR  "/dbu/"
#else  /* 今の 2 行 */
#endif
```

- ramfs は mount ごとに実体を取る (`fs/ramfs/ramfs.c` の `ramfs_mount`)。`rd1` の使い回しは VFS が拒む (`fs/vfs.c` の `vfs_mount` 以下) ので名前を広げる。
- 2 つの ramfs で核のヒープを最大 128 KiB + 管理 8 KiB 使う。`PM_KERNEL_HEAP_SIZE` に入るかは未確認 (S2 で記録)。
- 製品の辞書は載らない。7a は受入用の辞書で完了とする (§5 U2')。

### 2-2 起動と遅延の再試行 (同じ手続き、出口は 1 本)

```c
/* ime.c: 今の ime_dict_boot の本文は 1 文字も変えず、静的の像だけ名前を替える */
#ifdef OS32_FEP_STATIC
static int ime_dict_open(void)
#else
int ime_dict_boot(void)
#endif
{ /* 今の本文 (ime_dict_boot): dict_loaded → ime_display_boot → 控えを読む → fep_store_open → dict_loaded = 1 */ }

#ifdef OS32_FEP_STATIC
int ime_dict_boot(void)
{
    int rc = fep_static_gate();          /* 隔離なら OS32_ERR_BUSY。dict_loaded より先 */
    if (rc) return rc;
    if (g_ime.dict_loaded) return 0;
    rc = fep_static_prepare();           /* 0 以外はここで戻る。状態は IDLE か QUARANTINED */
    if (rc) return rc;
    return fep_static_commit(ime_dict_open());   /* prepare の後の出口はここだけ */
}
#endif
```

- 起動時 (P6): `platform_boot_run` の `sqlite_static_boot()` の直後、`#if OS32_HOOK_IME` の中で `fep_static_boot()` = `ime_init(); (void)ime_dict_boot();`。`/host` は接続を待たずに公開される (`platform/powermac/hostfs_boot.c の /host の登録`) ので、ここは AGAIN が普通。起動は続ける。
- 再試行: `ime_toggle`・`ime_set_mode` は `dict_loaded` が 0 なら `ime_dict_boot` を呼ぶ (`kernel/ime/ime.c` の `ime_toggle・ime_set_mode`、今のまま)。
- `fep_static_prepare` の順:
  1. `os32_sqlite_outer_boot_check(0, 0)` が BUSY (アプリの DB が開いている、`lib/sqlite3/os32_sqlite_vfs.c` の `os32_sqlite_boot_check` は数えるだけで戻る) → **何も変えずに** `OS32_ERR_AGAIN`。巻き戻しは呼ばない。
  2. `/db/fep.db` が無ければ搬送元から写す。`fep_s.db`・`fep_l.db` も搬送元に在れば写す。写しの失敗は、その版の途中のファイルを `vfs_unlink` で消してから: HOSTDOWN・STALE・搬送元の不在 → `OS32_ERR_AGAIN`、NOSPC ほか → その負の値。どれも IDLE のまま。
  3. `fep_meta_probe` → 予約 (`FEP_DICT_MIN_RESERVE_KB` で下支え) → `outer_boot_check(0, reserve)`。失敗は `outer_boot_check(0, 0)` で戻して IDLE (戻せなければ QUARANTINED)。
  4. `fep_boot_config_set`、`fep_boot_user_path_set(FEP_USER_PATH)` と `fep_boot_user_rc_set` (§2-5)。状態を PREPARED に。
- 協調切替なので prepare から commit までアプリは走らない。1 で通れば commit の `boot_check` をアプリの DB が邪魔することは無い。
- PowerPC の SQLite の pool が予約を出せるかは未確認 (P6 で戻りを見る)。

### 2-3 巻き戻しと隔離の契約 (`kernel/ime/fep_static.c`)

| 状態 | 入る条件 | `ime_dict_boot` の戻り | 再試行 | services |
|---|---|---|---|---|
| IDLE | 初期。prepare の失敗。きれいな巻き戻しの後 | AGAIN か負の値 | 可 | SQLITE_ONLY、public=1 |
| PREPARED | prepare が 0 | (開く途中) | — | 同上 |
| READY | commit の `outer_boot_check(接続の数, reserve)` が 0 | 0 | 不要 | `SERVICES_READY`。FEP の接続を残したままアプリの DB が開ける |
| QUARANTINED | 巻き戻しの中で `ime_boot_abort()` か `outer_boot_check(0, 0)` が失敗 | `OS32_ERR_BUSY` (gate で、以後ずっと) | **不可** (再起動まで) | `SERVICES_MINIMAL_QUARANTINED`、public=0、`sqlite_module_api = &sqlite_stub_api` (x86 の `kernel/sqlite_engine.c` の `services_boot` と同じ扱い。本体の停止は呼ばない) |

- `fep_static_commit(rc)`: rc が 0 なら予約を確定して READY。rc が 0 でない・確定の失敗は巻き戻し: `ime_boot_abort()` → `outer_boot_check(0, 0)` → `fep_boot_config_clear()` → IDLE。どちらかが失敗なら控えは消して QUARANTINED。close の失敗では `dict_loaded` が 1 のまま残り得る (`kernel/ime/ime.c` の `ime_boot_abort`) ので、gate を先頭に置く。
- QUARANTINED では db の slot も不在の側に落ちる (public=0)。受入で確かめる。

### 2-4 入力元と打鍵時の修飾

- **持つ所**: `platform/powermac/input_usb.c`。輪に打鍵時の修飾を並べて積む (`keys[]` の隣に `u8 mods[PM_INPUT_KEYS]`)。`pm_input_usb_take` が取り出した時に「最後のキー = 手元、修飾 = その打鍵時の値」を覚える。kboot-sh の `try_key` は LAN のキーを返す時だけ `pm_input_usb_note_remote()` で「手元でない」に替える。link-sh は USB を直に取るので、同じ覚えがそのまま走る。
- **見分ける所**: FEP の静的の入口。

```c
/* include/platform_fep_input.h (新設、platform_*.h の口)。実装は input_usb.c */
int platform_fep_key_local(void);  /* 最後に取り出したキーが手元の鍵盤なら 1 */
u8  platform_fep_key_shift(void);  /* そのキーの打鍵時の SHIFT_* (kbd.h)。手元でなければ 0 */

/* kernel/ime/fep_input.h (新設) */
#ifdef OS32_FEP_STATIC
#include "input_service.h"
#include "platform_fep_input.h"
#define kbd_getkey_wait input_getkey_wait
#define kbd_trygetkey   input_trygetkey
#define kbd_trygetchar  input_trygetchar
#define kbd_shift_state platform_fep_key_shift()
#endif
```

- `ime.c` は `ビルド条件` の `#endif` の後に `#include "fep_input.h"` を足す。`ime_cui_process` (`kernel/ime/ime_cui_process.inc` の `ime_cui_process`) の頭に静的の像だけ `if (!platform_fep_key_local()) return keydata;`。LAN のキーは FEP が ON でも素通り。
- `InputOps.shift_state` (P3) も同じ値を返す。x86 は `platform_fep_input.h` を読まない。

### 2-5 起動の控えの書く口 (M3、`kernel/ime/fep_boot_config.c`、宣言は `kernel/ime/ime_store.h` の `fep_boot_config_get・fep_boot_user_path_get` の並び)

```c
void fep_boot_config_set(const FepMeta *meta, u32 reserve_kb); /* 控えて saved = 1 */
void fep_boot_config_clear(void);        /* saved = 0、user_rc = OS32_ERR_AGAIN。経路の中身は触らない */
int  fep_boot_user_path_set(const char *path); /* NULL・長さ >= OS32_MAX_PATH は OS32_ERR_INVAL で中身不変。通れば NUL まで写して 0。user_rc は変えない */
void fep_boot_user_rc_set(int rc);       /* user_rc = rc > 0 ? OS32_ERR_IO : rc */
```

| 呼び手 | 元の行・場面 | 呼び出し |
|---|---|---|
| x86 `kernel/sqlite_engine.c` の `services_settings` | 既定の経路 | `fep_boot_user_path_set(FEP_USER_PATH); fep_boot_user_rc_set(0);` |
| x86 `services_settings の DB open` | 設定の DB が開けない | `fep_boot_user_rc_set(OS32_ERR_IO)` |
| x86 `services_settings の値取得` | 設定の値。長さの検査は呼び手に残す | 通った時だけ `fep_boot_user_path_set(text)` |
| x86 `services_settings の読取結果` | 設定の読みの結果 | `fep_boot_user_rc_set(rc)` |
| x86 `services_boot` / `services_boot の巻き戻し` | 控える / 巻き戻し | `fep_boot_config_set(&meta, (u32)reserve / KIB)` / `fep_boot_config_clear()` |
| 静的の prepare | 設定の DB は読まない | `rc = fep_boot_user_path_set(FEP_USER_PATH); fep_boot_user_rc_set(rc);` (`/dbu` の mount が無ければ `fep_boot_user_rc_set(OS32_ERR_NOSYS)` で学習なし) |

読む口 2 本 (`fep_boot_config_get・fep_boot_user_path_get`) は本文を変えずに移す。経路の中身の検査は今のまま本体の `user_path_valid` が持つ (x86 は `/etc/`、静的は `/dbu/`)。

### 2-6 suspend の持ち主 (M4、`kernel/ime/fep_suspend_owner.c`)

```c
void fep_suspend_owner_note(int on, int rc, int owner); /* on=0: 0 に戻す (rc によらず)。on=1 で rc==0: owner を覚える。on=1 で rc!=0: 変えない */
int  fep_suspend_owner_take(int owner);  /* owner > 0 で一致なら 0 に戻して 1、ほかは 0 */
```

| 出来事 | 遷移 | その後 |
|---|---|---|
| suspend(1) 成功 / 失敗 | 無し → 持ち主 / 変えない | — |
| suspend(0) (成功・失敗とも) | → 無し | 再開の失敗は本体がログ。辞書は閉じたまま、次の切替の遅延の起動に任せる (今と同じ) |
| 持ち主の終了 | → 無し、`dict_suspend(0)` を 1 回 (戻りは見ない) | **続けて必ず** CUI の読み手の終了 (`ime.c` の `ime_owner_exit`) |
| 持ち主でないアプリの終了 | 変えない | CUI の読み手の終了だけ |

x86 は `kernel/ime/fep_module.c` の `suspend_owner・fep_start` の 2 か所をこの 2 本に替える (順は今のまま)。静的は `ime_suspend.c` の `ime_dict_suspend` の出口で note、`ime.c` の `ime_owner_exit` の頭で take → `ime_dict_suspend(0)` (どちらも `#ifdef OS32_FEP_STATIC`)。

### 2-7 後続の段「7a-辞書」(製品の辞書、ext2 が PowerPC の像に入った後。7a の外)

| 項目 | 決めておく形 |
|---|---|
| 前提 | 段 6c の先で ext2 の本体と ext2 の loop の当て物 (`build/sources/common.mk` の `C_COMMON_LOOP` の選択) が PowerPC の像に入る |
| 媒体の固定 | host の ext2 の像を loop に**読むだけ**で付ける。付ける時に大きさと CRC を控え、読みの失敗・STALE は FEP を suspend して IDLE に戻す (外からの書き換え・切断を成功として扱わない) |
| 切替の手順 | FEP が IDLE か suspend 済みの時だけ: `/db` の ramfs を umount → loop の ext2 を `/db` に mount → 次の `ime_dict_boot` の prepare が写しを飛ばして開く (`/db/fep.db` が在るので)。失敗したら ramfs を `/db` に戻す |
| 学習の書き先 | `/dbu` の ramfs のまま (host の像には書かない)。永続の学習は ATA の媒体 (段 8) の後 |

## 3. 部品の大きさ (PM 決定 U1)

| 物 | 実測 (i386、`size build/out/fep/`) | PowerPC の見積もり |
|---|---|---|
| 本体 7 本の text | 26,179 (ime 5,626・ime_dict 11,936・user_store 3,502・romkana 2,757・dict_info 1,585・suspend 405・conn 368) | 1.3〜1.5 倍で 34〜39 KB |
| bss | 8,812 (`g_ime` 8,036 ほか) | 9 KB (`arch/ppc/kernel.ld` は bss を常駐に数える) |
| fep_static.c・fep_boot_config.c・fep_suspend_owner.c・ime_display.c・入力 | — | 3 KB 未満 |
| 合計 | fep.elf は text 27,202・bss 8,872 | **46〜51 KB** |

- **指標**: 同じ構成の FEP=0 と FEP=1 の像で、`__kernel_resident_end - __kernel_image_start` の差と `__kernel_chain_end` の差 (`arch/ppc/kernel.ld` の `ENTRY`) を S2 で記録する。
- **止める基準**: 常駐の差が 52 KB を超えたら公開の段に進まず PM に返す。`arch/ppc/kernel.ld` の ASSERT と鎖の上限は FEP=1 の像でも通ること。
- x86: M1・S1・S3・M5・M6 は関数差 0。M3 は `services_boot`・`services_settings`・`fep_boot_*`、M4 は常駐側の `ime_dict_suspend`・`ime_owner_exit` だけの差を申告して NP21/W。gen_kapi の x86 の生成物はバイト一致。

## 4. 段・検査・受入

### 4-1 ファイルごとの段 (1 ファイル = 1 段)

| 段 | 担当 | ファイル | 中身 | x86 |
|---|---|---|---|---|
| M1 | 本線 | `kernel/ime/fep_input.h` (新設) → `include/platform_fep_input.h` (新設) → `ime.c`・`ime_cui_process.inc`・`ime_store.h` の静的だけの行 → `build/fep-module.mk` の `FEP_INC` の依存にヘッダを明示 | §2-1・§2-4 | 差 0 |
| M3 | 本線 | `kernel/ime/fep_boot_config.c` (新設) → `kernel/sqlite_engine.c` | §2-5 | その場の変更。NP21/W |
| M4 | 本線 | `kernel/ime/fep_suspend_owner.c` (新設) → `fep_module.c` → `ime_suspend.c`・`ime.c` の静的だけの行 | §2-6 | その場の変更。NP21/W |
| M5 | 本線 | `kernel/ime/fep_static.c` (新設) と `ime.c` の `ime_dict_boot` の静的だけの行 | §2-2・§2-3。PowerPC では `C_COMMON_COMPILE_ONLY_PPC` で通すだけ | 選ばれない |
| M6 | 本線 | `sdk/gen_kapi.py` | `optional_db` を表引きに (`OPTIONAL_HOOKS = {'db_': 'OS32_HOOK_KAPI_DB', 'ime_': 'OS32_HOOK_IME'}`) | 生成物バイト一致 |
| P1 | PPC | `platform/powermac/input_keymap.c` | 戻りを (sc<<8)\|ASCII に。走査符号 0 は ESC だけ | — |
| P2 | PPC | `platform/powermac/input_usb.c` | 打鍵時の修飾の輪、入力元の覚え、`platform_fep_key_*`、`pm_input_usb_note_remote` | — |
| P3 | PPC | `platform/powermac/input_boot.c` | LAN のキーで note_remote、`.shift_state` | — |
| P4 | PPC | `platform/powermac/ime_display.c` | `ime_display_boot` を 0 に | — |
| P4b | PPC | `platform/powermac/root_boot.c` | 名前表と `/db`・`/dbu` (§2-1) | — |
| S1 | 本線 | `build/features/fep.mk` → `build/features/fep-static.mk` (新設) | `ifeq ($(ARCH),x86)` で module と静的に分ける。静的は本体 7 本・fep_static.c・fep_boot_config.c・fep_suspend_owner.c・機種の ime_display.c。order.mk・fixture を追従 | 差 0 |
| S2 | PPC | `build/targets/ppc-powermac.mk` → `build/platform/powermac.mk` | 本体の TU に `-DOS32_FEP_STATIC`。対象外の像の FEP=1 をエラーに (§4-1b)。§3 の指標を測る | — |
| S3 | 本線 | `build/sources/hooks.mk` | `HOOK_ime = $(if $(filter 1,$(FEATURE_PC98_PENDING) $(FEATURE_FEP)),1,0)` | 差 0 |
| P6 | PPC | `platform/powermac/ppc_kernel.c` | `fep_static_boot()` の呼び出し | — |
| P7a〜f | PPC | `sdk/kapi_target_ppc.py` | 4-4 の順に 1 組ずつ外す | — |
| P8 | PPC | `platform/powermac/ime_render_text.c` (新設) → `platform_ime_render.h` → `ime_display.c` の行の保存 | 文字コンソール (`include/text_screen.h` の `tvram_putchar_at・tvram_readchar_at・tvram_putkanji_at`) の上の render。行の保存と復元の持ち主は `ime_display.c` (PC-98 と同じ持ち方、静的な 3 行分) | — |

順: M1・M3・M4・M5・M6 と P1〜P4b が先 (M3 と M4 は NP21/W が要るので直列)。**提供者が全部入ってから** S1 → S2 → S3 → P6 → P7。S1・S3 は共有の登録ファイルなので持ち主から受け取る。

### 4-1b S1〜S3 でリンクする構成

| 構成 | S1 の後 | S2 の後 | S3 の後 |
|---|---|---|---|
| x86 の既定 (FEP=1) と build-selection-host が今結んでいる構成 | 同じ (build_compare) | 同じ | 同じ |
| PowerPC kboot-sh・link-sh の FEP=0 (既定) | 今と同じ像 | 同じ | 同じ (`exec/absent/exec_ime.c` のまま) |
| PowerPC kboot-sh・link-sh の FEP=1 (SQLITE=1) | 結べない (今と同じ) | **結べる** (フックは不在、受入しない中間) | 結べる。終了のフックが `ime_owner_exit` に届く |
| PowerPC の対象外の像 (EXEC・USERSHELL・USB のどれかが無い: core・cli・noexec ほか) の FEP=1 | — | **選択のエラー** (`build/platform/powermac.mk` に `FEATURE_FEP requires USERSHELL and USB` の形。変数の名前は着手の時に確かめる。SQLITE=0 は `build/select.mk` の `FEATURE_KDEBUG` の今のエラー) | 同じ |

- kboot-sh と link-sh は、**同じ出力先**で FEP=0 → 1 → 0 と切り替えて結び、旗の変化で本体と wrap が組み直ること、0 に戻した像が最初と同じことを確かめる。
- `kernel/sqlite_engine.c` に残る動的な FEP の参照 (`services_boot` の `fep_module_load`・`fep_module_abort`、`services_report` ほかの `fep_module_pages`・`module_record_get`) は PowerPC では呼び手が無く `--gc-sections` で落ちる。S2 で `--no-undefined` のリンクが通ること、map に `services_boot`・`services_report`・`fep_module_*` が無いことを kboot_audit の記録に残す。残っていたら止めて PM に返す。
- 静的の像では `ime_boot_bytes` は `ime.c`、`ime_boot_connections` は fep_static.c が 1 つずつ定義する。

### 4-2 触る既存の検査と固定 (grep で拾った全部。実装者は着手の時にもう一度 grep する)

| 変える名前 | 当たる検査・固定 |
|---|---|
| `kernel/ime/ime.c`・`ime_cui_process.inc`・`ime_dict_boot`・`ime_store.h` (M1・M5) | test_fep_boundary・test_raw_wait・test_ime_display・test_x11_shared・test_fep_core_boot・test_kapi_audit・test_d2_services・test_fep_store_info・test_fep_store_contracts・test_sqlite_module_proto・test_np21w_gui、ime_display_host.c・ring3_guard_host.c・fep_boundary_host.c (`入力境界の試験`)、checks の d2-services-host・np21w-gui-host・x11-shared-host |
| `kbd_getkey_wait`・`kbd_trygetkey`・`kbd_trygetchar` の読み、`fep_imports.def` (M1) | test_fep_core_boot・test_arch_compat (arch_compat_vectors.c)・input_wait_host.c・d2_services_host.c。def は変えない (test_hsync_fep・test_ime_display_boundary・test_c_dialect、checks の tests-inventory・check-select-host・docs-orphans・map・c-dialect が読む) |
| `fep.mk`・`FEATURE_FEP`・`HOOK_ime`・`exec_ime` (S1・S3) | test_build_selection (`FEP の選択`)、checks の build-selection-host・fbcon-host・check-select-host (選択の規則に新しい TU 4 本と fep-static.mk)、test_exec_r1 (`ime-owner-exit`)、fixtures の build_profile_x86.json・build_profile_ppc.json (`exec/absent/exec_ime.c` の行) |
| `sqlite_engine.c`・`boot_config_saved`・`boot_user_rc`・`fep_boot_config_get`・`fep_boot_user_path_get` (M3) | test_fep_boundary・fep_boundary_host.c・test_fep_store_contracts・test_fep_core_boot・sqlite_module_host.c・kapi_audit_host.c・x11_shared_host.c・ime_display_host.c・fep_host_build.py・fep_store_host.h・test_fep_meta・test_sqlite_engine・test_module_policy・test_mem_stat・test_ver_services・test_trim_kernel・test_trim_flow・test_ci_stab、checks の sqlite-engine-host・trim-back-rs-host・le-access (covers に移した先を足す) |
| `fep_module.c`・`ime_dict_suspend`・`ime_owner_exit`・`suspend_owner` (M4) | test_fep_module (`試験対象のソース一覧`)・fep_module_host.c・fep_suspend_owner_host.c・test_module_policy・test_hsync_fep・test_d2_services・test_sqlite_module_proto、x11_shared_host_imetab.inc・x87_lifecycle_host.c |
| `sdk/gen_kapi.py`・`kapi_target_ppc.py`・`IME_SLOTS` (M6・P7) | tools/ppc/test_kapi_binding.py (`IME slots` の名前の一覧、`空グループ` の空の組の変異、`不在 slot` の本数、ALLOWED_UNDEFINED、optional の経路に IME の分を足す)・test_arch_compat・test_pcm_contract・test_remote_x9、checks の ppc-kapi-binding・ppc-stream-host |
| `input_keymap.c`・`pm_input_keymap` (P1) | ppc_input_usb_host.c、tools/ppc の usb_run.py・sc_run.py、checks の ppc-usb・ppc-sc・ppc-fp-guest・ver-ppc-input-usb。**tools/checks/ver-ppc-input-usb.py の MUTATIONS の Backspace の変異の目印 `case 0x0e: return 8;` が消える** → 新しい行 (`case 0x0e: return 0x0e00 \| 8;` の形) に移し、変異 (8 → 127) が実行時に RED になることを確かめる。`pm_input_keymap` の `case 0x%02x:` は case の札を残せばそのまま |
| `input_usb.c`・`input_boot.c`・`shift_state`・`zero8` (P2・P3) | 上に加えて test_ppc_link_sh・test_ppc_sd_run・ppc_sh_input.py・ppc_sh_boot.py・fb_run.py、checks の ppc-sh-input・ppc-sh-boot・ppc-sh-output・ppc-sd |
| `ime_display.c`・`platform_ime_render.h`・`root_boot.c` (P4・P4b・P8) | test_ime_display_boundary・test_ime_display・test_fep_boundary・test_fep_core_boot・test_raw_wait・test_sqlite_module_proto、checks の x11-shared-host、kboot_root_image.py と root の mount を見る guest (着手の時に `root_boot`・`"rd"` で grep) |
| `ppc-powermac.mk`・`powermac.mk`・`ppc_kernel.c` (S2・P6) | tools/ppc の kboot_mutations.py・kboot_accept.py、test_src_layout・test_ppc_kboot_link・test_kprintf_window、checks の p2v・ppc-compile・ppc-sqlite-static-guest・sqlite-static-host・ppc-sqlite-stack-guest (FEP=0 の像で結果が同じこと)、ppc_sqlite_static_probe.inc |

新しい TU 4 本とヘッダ 2 本は order.mk・像別の選択・build_profile の fixture・check_layers (共通の `kernel/ime/` は機種のヘッダを読まない。`<platform_ime_render.h>`・`platform_fep_input.h` は口) を追従する。

### 4-3 受入 → 試験と変異

| 受入の項目 | 試験 | 変異 (1 行) |
|---|---|---|
| x86 の FEP の起動・日本語入力・起動の失敗の巻き戻しが変わらない。`/etc/` の外の学習の経路は今のまま拒む (M1・M3・M4) | host: sqlite_module_host・fep_boundary_host (変更の前後で同じ結果)。NP21/W: `[services]` の行、Shift+Space → ローマ字 → 変換 → 確定、`services_boot_fault` の注入 | `fep_boot_config_clear` を空にする |
| x86 の hsync 中の STOP で辞書が戻り、読み手の取り消しも走る (M4) | host: fep_suspend_owner_host (§2-6 の表の各行)。NP21/W: hsync を STOP | take の後の再開を外す |
| fep.mod と x86 の生成物が同じ。ヘッダだけ触った差分ビルドで fep/ime.o が組み直る (M1・S1・S3・M6) | build_compare、gen_kapi のバイト比較、`touch kernel/ime/fep_input.h` の後の組み直し | — |
| 静的の起動: 成功 / 搬送元なし (AGAIN → 後で成功) / **写しの途中の HOSTDOWN・STALE (途中のファイルが残らず AGAIN → 後で成功)** / NOSPC / **アプリの DB が開いている (予約に触らず AGAIN、巻き戻しを呼ばない)** / probe・開く段・commit の失敗 (IDLE) / **close の失敗・予約の解除の失敗 (QUARANTINED。`dict_loaded` が 1 でも次は BUSY で何も開かない)** / READY の後にアプリの DB が開ける (M5) | host: fep_static_host.c (新設、偽の VFS と SQLite の外枠) | gate を `dict_loaded` の後ろに移す / 写しの失敗で unlink を外す / 手順 1 の BUSY で巻き戻しを呼ぶ |
| **容量**: 辞書 2 版が `/db` に、学習 DB + ジャーナルが `/dbu` に、どちらも 1 ファイル 32 KiB・合計 64 KiB の中で載る。学習・削除・書き出し・全消しが成功する間の確保容量のピークを 1 KiB 単位で記録 (P7e の前提) | host: 本物の ramfs と SQLite の VFS、`tools/fep_to_sqlite.py` で作る受入用の辞書 2 版 (fixture の作り方を試験に書く) | ramfs の合計の上限を 1 KiB 下げるとピークの余りの記録が変わる (余りが 0 なら RED) |
| 学習 DB を `/dbu` に作り、閉じて (suspend → 再開) もう一度開ける。`/dbu` の外の経路は拒む | host: fep の user store の試験を静的の旗で。QEMU: P7e のアプリ | 静的の `FEP_USER_DIR` を `/etc/` に戻す |
| USB のキーが走査符号付き。Shift を離した後に取り出しても打鍵時の Shift。LAN のキーは FEP が ON でも素通り。link-sh でも同じ覚えが走る (P1〜P3) | host: ppc_input_usb_host.c。QEMU: kboot-sh (USB と LAN)・link-sh (USB) で IME の slot を呼ぶ受入のアプリ | 修飾を取り出しの時の今の値に戻す / `platform_fep_key_local` を 1 に固定 |
| FEP=1 の像が §3 の基準に入る。§4-1b の表のとおり結べる・エラーになる (S2・S3) | check_layout・kboot_audit・build-selection-host | 対象外の像のエラーを外す |
| 各公開段: FEP=0 の像はその組が型別の不在の値、FEP=1 の像は実の呼び出し (P7a〜f) | QEMU: ppc-ime-absent-guest (新設) と段ごとのアプリ (mac99 の 7450 と 7455) | `OS32_HOOK_IME` の条件を反転 |
| 接続前に起動 → 切替は失敗の 1 行で OFF のまま → host を接続 → 切替が通り変換できる (P6・P7a・P7b) | QEMU、host の接続を遅らせる | 初回の失敗を覚えて再試行しない |
| 状態 4 本: set_mode → get_mode・is_active | QEMU | `ime_set_mode` の遅延の起動を外す |
| キー・文字 4 本: 入力なしの try は -1、Shift+Space → 「nihongo」→ Space → Return で確定の UTF-8 (受入用の辞書の語) | QEMU の sendkey で USB から | 確定の残りの取り出しを 1 バイト落とす |
| feed 1 本: OFF は素通り、ON は消費か確定のバイト、STOP で取り消し | QEMU のアプリ | STOP の取り消しを外す |
| 辞書の読み 2 本: 切替 (在る版は候補が変わる、無い版は失敗して元のまま)・情報 | QEMU、受入用の辞書 2 版 | `ime_switch_dict` を何もせず 0 で返す (計画の変異) |
| 辞書の書き 4 本: 学習の一覧・削除・書き出し・全消しが成功 | QEMU。容量の host 試験が通ってから | 削除を何もせず 0 で返す |
| suspend 1 本: suspend → 接続 0 → 再開、アプリの異常終了でも戻る | QEMU のアプリを途中で止める | 持ち主の覚えを外す |
| 未確定の表示 (P8) | QEMU の画面の読み戻し (fb_run)、候補の行が消えた後に元の文字が戻る | 行の復元を外す |

### 4-4 IME の 17 本の公開の順

| 順 | 段 | slot | 前提 |
|---|---|---|---|
| 1 | P7a | ime_toggle・ime_is_active・ime_set_mode・ime_get_mode | P6・M6 |
| 2 | P7b | ime_getkey・ime_trygetkey・ime_getchar・ime_trygetchar | P1〜P3、S3 |
| 3 | P7c | ime_feed_key | P7a |
| 4 | P7d | ime_switch_dict・ime_dict_info | 読める辞書 2 版 |
| 5 | P7e | ime_user_list・ime_user_delete・ime_user_export・ime_user_clear | 容量の host 試験と学習の操作の成功。通らなければ不在のまま残して報告 |
| 6 | P7f | ime_dict_suspend | M4 |
| 7 | GUI の後 | ime_set_render | `kernel/gui.c` の採用 (7a の外) |

最初の公開 (P7a) で `ime.h` を不在の include の集合から外す。各段で test_kapi_binding の名前の一覧と本数を追従する。

## 5. 未決 (1 件、ユーザーに確認中)

- **U2' 受入用の辞書で 7a を完了扱いにするか**: PowerMac で MAIN_DB の能力を持つのは ramfs だけで、5.8 MB の製品の辞書は載らない。この設計は「7a は `tools/fep_to_sqlite.py` で作る受入用の辞書 (1 版 32 KiB 以下) で 16 本を公開して完了、製品の辞書は ext2 が PowerPC の像に入った後の別の段 7a-辞書 (§2-7)」の形で書いた。承認されない場合は、P7a 以降の公開を 7a-辞書の後ろへ回す (M・P1〜P6・S の段はそのまま進められる)。ramfs の予算を広げる案は既存の方針から外れるので採らない。

PM の範囲 (参考): `rd2`・`rd3` の名前と mount、fixture の作り方、P1 で PowerMac のキーの値が x86 と同じ形になること、常駐の sh.bin を IME に通すか (7a の外)、P8 を 7a に含めるか、M3・M4 の NP21/W の枠。

## 反論

無し (0 件)。

## 確定 (2026-11-20、sol 第 3 回、PM が反映)

以下は本文の該当節に優先する (実装者は本文とこの節を合わせて読む)。

- **U2' (ユーザー決定 2026-11-20「受入用の小さい辞書で完了」)**: 受入用の辞書 (fep_to_sqlite.py の SQLite の fixture、32 KiB 以下) で IME の 16 本を公開して 7a を完了とする。P7e (学習の操作) が受入を通らなければ 7a は未完了 (§5 の承認待ちの分岐と「P7e を不在のまま残す」記述は無効)。製品の辞書は PowerPC に ext2 と loop が入った後の別の段 (§2-7)。ime_set_render は GUI の後。
- **§2-4 (P1)**: USB の修飾の値は共通の口へ返す時に `PM_INPUT_*` を `SHIFT_*` (include/kbd.h) に変換する (`PM_INPUT_CTRL` 0x04 をそのまま返すと SHIFT_KANA になる)。Ctrl の試験を足す。入力元と修飾の記録は、成功した dequeue と同じ割込み保護の区間でだけ更新し、覗き見・空読みでは更新しない。
- **§4-1 S2 (P1)**: SQLite のヘッダを読む静的な FEP の翻訳単位には `-DOS32_FEP_STATIC` に加えて `-Ilib/sqlite3` と既存の生成ヘッダの依存を付ける (PPC の共通のコンパイルの旗には無い)。
- **§2-1・§2-5 (P1)**: `/db`・`/dbu` の mount の失敗は pm_root_boot から返さず FEP の側で扱い、既存の root の起動 (root_ready) を通す。`/dbu` の失敗の時は学習を無効にする。P7e の受入では両方の mount の成功を必須にする。
- **§2-7 (P2)**: 後続の段の契約に、公開中の像を host の側で固定する条件と、suspend の後の開き直しの経路を書く (attach の時の CRC だけでは同じ長さの上書きを検知できない)。
- **実装の後の受入の条件 (sol 第 3 回の確認表)**: ramfs の実ヒープ (通常 1 MiB、失敗時 192 KiB) と既存の使用分込みのピーク、SQLite の pool (512 KiB) の予約と接続を開いた後のアプリ DB との併用は、S2・P6・P7e で実測する。選択の変数は FEATURE_EXEC・FEATURE_USERSHELL・FEATURE_USB。書き出しの受入先は /tmp。
- **§2-2 (M5 の実装で、PM 決定 2026-11-20)**: 静的の像の辞書を開く本体の名前は `ime_dict_open` でなく `ime_boot_open_dict(void)` (static)。ime.h の公開の `ime_dict_open(IME_Dict *, const char *)` と型がぶつかるため。
- **§2-2・§2-3 (M5 の実装で、PM 決定 2026-11-20)**: 写しの途中のファイルの削除は `vfs_rm`。HOSTDOWN・STALE の後に途中のファイルを消せなかった時は QUARANTINED に移して BUSY (再試行しない)。消せた時は AGAIN。
- **§2-2・§2-3 の失敗の経路 (M5 の実装の前の補い、2026-11-20)**: 下の「§2-2b 失敗の表」が §2-2 の prepare の手順 2・3 と §2-3 の巻き戻しの文に優先する。

### §2-2b 失敗の表 (M5 の実装の前の補い) — 第 3 版 (2026-11-20、sol の 2 回目の確かめの「保留」を受けて置き換え。行番号は付け直した)

基点 main 9d99c1b64 の実コードで確かめた関数 (読んだだけ、ビルドと試験は未実施):

| 関数 | 所在 | 返り値の意味 (この表で使う分) |
|---|---|---|
| `vfs_find_mount` (中の経路の振り分け) | `fs/vfs.c` の `vfs_find_mount-` | 一致する prefix が無い経路は `/` の mount に落ちる。`/db` の mount が無いと `/db/fep.db` は root の媒体を指す |
| `vfs_is_mounted(prefix)` | `fs/vfs.c` の `vfs_is_mounted` | prefix が**完全に一致する** mount が在り、その FS が mount 済みなら非 0。無ければ 0 |
| `vfs_sqlite_backend(path)` | `fs/vfs_fd.c` の `vfs_sqlite_backend` | その経路が落ちる媒体の SQLite の種別。`VFS_SQLITE_UNSUPPORTED` (0、`fs/vfs.h` の `VFS_SQLITE_UNSUPPORTED`) = 能力なし、正 = ext2 (1)・ramfs (2)、負 = 解決の失敗・`VFS_ERR_NOMOUNT` |
| `vfs_sqlite_fd_identity(fd, out)` | `fs/vfs_fd.c` の `vfs_sqlite_fd_identity`、型は `fs/vfs.h` の `VfsSqliteOps・VfsSqliteIdentity` | 0 で `mount_generation`・`file_id` (inode)・`kind` を返す。`VFS_ERR_STALE`・`VFS_ERR_NOSYS` (inode か mount の世代が無い)・`VFS_ERR_INVAL` |
| ramfs の inode | `fs/ramfs/ramfs.c` の `ramfs_write` | `++next_ino` で振り、使い回さない |
| ramfs の作成の失敗 | `fs/ramfs/ramfs.c` の `ramfs_write` | 空きが無い・確保できない時はファイルを登録せずに負で戻る |
| `vfs_stat` | `fs/vfs.c` の `vfs_stat` | 0 = 在る。`VFS_ERR_NOTFOUND` = 無い。`VFS_ERR_STALE`・`OS32_ERR_HOSTDOWN` はそのまま (`vfs_stat`)。ほかの負 |
| `vfs_open` | `fs/vfs_fd.c` の `vfs_open` → `vfs_open_internal` | fd か負。`VFS_ERR_NOSPC` = FD の空きが無い (`FD 割当`)。`O_EXCL` で既に在れば `VFS_ERR_EXIST`。作った後で失敗した時は中で `ops->unlink` を呼ぶ (`作成失敗時の後始末`) |
| `O_CREAT`・`O_EXCL`・`O_WRONLY`・`O_RDONLY` | `sdk/include/os32/os32_kapi_shared.h` の `KAPI_O_RDONLY・O_RDONLY` | ramfs は `create_excl` を持つ (`fs/ramfs/ramfs.c` の `ramfs_ops`) |
| `vfs_read_fd` / `vfs_write_fd` / `vfs_close` | `fs/vfs_fd.c` の `vfs_read_fd` / `vfs_write_fd` / `vfs_close` | 長さか負 / 長さか負 / void |
| `vfs_rm` | `fs/vfs.c` の `vfs_rm` | 0 = 消した。`VFS_ERR_NOTFOUND` = 元から無い。ほかの負 = 消していない |
| `fep_meta_probe` | `kernel/sqlite_engine.c` の `fep_meta_probe` | `OS32_ERR_NOTFOUND`・`OS32_ERR_IO`・`OS32_ERR_INVAL`。`OS32_ERR_BUSY` は 2 通り (枠を取れなかった `fep_meta_probe` / 接続の終了の失敗 `接続の close`) で、戻り値では見分けられない |
| `os32_sqlite_outer_boot_check` | `lib/sqlite3/generated/sqlite_outer_calls.inc` の `os32_sqlite_outer_boot_check` | 本体の値、SQLite が未起動なら stub の値 (`OS32_ERR_NOSYS` = -10)、実行を断られたら `SQLITE_MISUSE`。失敗の時に予約を書き換えない |
| `os32_sqlite_boot_check` (本体) | `lib/sqlite3/os32_sqlite_vfs.c` の `os32_sqlite_boot_check` | `SQLITE_BUSY` = 常駐でない組が在る・数が合わない。`SQLITE_NOMEM` = 予約が入らない (代入の前に戻る)。0 = 入れた |
| `ime_boot_abort` | `kernel/ime/ime.c` の `ime_boot_abort` | 0 = 閉じて `dict_loaded = 0`。-1 = 閉じられない |
| `fep_store_close` → `fep_conn_close` | `kernel/ime/ime_dict.c` の `fep_store_close` → `kernel/ime/ime_conn.c` の `fep_conn_close` | 0 か `OS32_ERR_BUSY` (接続を残す) |
| `services_public` の門 | `kernel/ime/ime.c` の `ime_toggle・ime_is_active・ime_set_mode・ime_get_mode・ime_feed_key・ime_getkey_wait・ime_trygetkey`、`kernel/ime/ime_dict_api.inc` の `ime_switch_dict・ime_user_list_facade・ime_user_delete_facade・ime_user_export_facade・ime_user_clear_facade` | `ime_suspend.c` には無い |

#### 決まり

- **媒体の確かめ (毎回の prepare の最初、どのファイル操作よりも前)**: `vfs_is_mounted("/db")` が非 0、かつ `vfs_sqlite_backend(IME_DICT_PATH)` が正。どちらかが満たされなければ、**stat も写しも削除もせず**に戻る (行 3)。root に落ちた経路は `/` の iso9660 が能力なしなので 2 つ目の条件でも止まる。以後の「消す」は、この確かめが同じ呼び出しの中で通っていることが前提。
- **版ごとの覚え** (fep_static.c の static、主 `fep.db`・`fep_s.db`・`fep_l.db`): `done` と `skip`。prepare は毎回 3 版を順に見て、`done` でも `skip` でもない版だけ扱う。`/db` の媒体を替える段 (§2-7) は覚えを全部消す。
- **1 版の写しの手順**: (a) 写し先を `vfs_stat` → (b) 搬送元を `vfs_open(O_RDONLY)` → (c) 写し先を `vfs_open(O_WRONLY | O_CREAT | O_EXCL)` → (d) 読み書きの輪 → (e) 主の版だけ写し先の fd で `vfs_sqlite_fd_identity` を取る → 両方 `vfs_close` → `done`。
- **消してよいのは写し先の fd を得た後だけ**。(c) が負なら搬送元を閉じるだけで、削除はしない (今の ramfs は作成の失敗でファイルを登録しない)。(d) 以降の失敗は、開いている fd を全部閉じてから `vfs_rm(写し先)`。**「消せた」** = その戻りが 0 か `VFS_ERR_NOTFOUND`、ほかの負は **「消せない」**。
- **主の辞書の「確かめ待ち」** (static、試行をまたいで残す): (e) で取れた `mount_generation` と `file_id` を控えて `pending = 1`。(e) が失敗したら控えない (その辞書は既設として扱い、INVAL でも消さない)。probe が 0 を返したら `pending = 0` (確かめ済み)。消した時も 0。
- **probe の INVAL で消すかどうか**: `pending` が 1 で、`vfs_open(IME_DICT_PATH, O_RDONLY)` → `vfs_sqlite_fd_identity` → `vfs_close` の結果が控えと**同じ `mount_generation`・`file_id`** の時だけ「自分が写した、確かめ待ちの辞書」として消す。開けない・取れない・違う時は `pending = 0` にして既設として扱う (消さない)。
- **prepare の順**: 0 媒体の確かめ → 1 `boot_check(0, 0)` → 2 主の版の写し → 3 主の版の probe → 4 切替の版の写し → 5 予約 → 6 控え。
- **probe の BUSY の見分け**: `fep_meta_probe` が `OS32_ERR_BUSY` を返したら、続けて `os32_sqlite_outer_boot_check(0, 0)` を呼ぶ。0 = 組は残っていない (枠を取れなかっただけ)。`SQLITE_BUSY` = probe の組が残った (**終了の失敗**)。そのほかの 0 でない値 (`SQLITE_MISUSE`・`OS32_ERR_NOSYS`) = 組を見られなかった (**確かめの失敗**)。後の 2 つはどちらも Q(接続)。
- **「閉じられた」** = `ime_boot_abort()` が 0、かつ続く `os32_sqlite_outer_boot_check(0, 0)` が 0。接続の終了の失敗も予約の解除の失敗も Q(接続)。
- **予約の失敗では巻き戻しを呼ばない (確定)**。
- QUARANTINED は 2 種。**Q(ファイル)**: 予約も接続も取る前にだけ起きる。services は `SERVICES_SQLITE_ONLY`・public=1 のまま、FEP だけ gate で止める。**Q(接続)**: `SERVICES_MINIMAL_QUARANTINED`・public=0・`sqlite_stub_api`。どちらも `OS32_ERR_BUSY`、再起動まで再試行しない。
- **gate を通る入口** (静的の像だけ): `ime_dict_boot` の先頭、`ime_dict_suspend` の先頭 (`kernel/ime/ime_suspend.c` の `ime_dict_suspend`)、`ime_switch_dict` の先頭 (`kernel/ime/ime_dict_api.inc` の `ime_switch_dict`)。
- どの失敗の行でも、戻る前に `fep_boot_config_clear()` を呼ぶ。戻り値 `OS32_ERR_AGAIN` の行の状態は IDLE。

#### 表

| # | 段 | 原因 | 後始末 | 戻り値 | 次の状態 | 再試行 |
|---|---|---|---|---|---|---|
| 1 | gate | QUARANTINED (どちらの種でも) | 無し | `OS32_ERR_BUSY` | QUARANTINED | 不可 |
| 2 | gate の後 | `dict_loaded` が 1 | 無し | 0 | READY | 不要 |
| 3 | 0 媒体の確かめ | `/db` が mount 点でない・能力なし・経路が解決できない | 無し (stat・写し・削除をしない) | `OS32_ERR_NOMOUNT` | IDLE | 可 |
| 4 | 1 `boot_check(0,0)` | `SQLITE_BUSY` (アプリの DB が開いている) | 無し | `OS32_ERR_AGAIN` | IDLE | 可 |
| 5 | 1 | `SQLITE_MISUSE` (実行を断られた) | 無し | `OS32_ERR_AGAIN` | IDLE | 可 |
| 6 | 1 | `OS32_ERR_NOSYS` (SQLite が未起動) ほか 0 でない値 | 無し | `OS32_ERR_NOSYS` | IDLE | 可 |
| 7 | 2 主 (a) stat | `HOSTDOWN`・`STALE` | 未着手、消さない | `OS32_ERR_AGAIN` | IDLE | 可 |
| 8 | 2 主 (a) stat | 不在以外のほかの負 | 未着手、消さない | 元の負値 | IDLE | 可 |
| 9 | 2 主 (b) 搬送元の open | `NOTFOUND`・`HOSTDOWN`・`STALE` | 未着手、消さない | `OS32_ERR_AGAIN` | IDLE | 可 |
| 10 | 2 主 (b) | `NOSPC` (FD の空きが無い)・`IO`・ほかの負 | 未着手、消さない | 元の負値 | IDLE | 可 |
| 11 | 2 主 (c) 写し先の open | 負の全部 (`EXIST`・`NOSPC`・`NOSYS`・`ROFS` ほか) | 搬送元を閉じるだけ。**消さない** | 元の負値 | IDLE | 可 |
| 12 | 2 主 (d) 輪 | `HOSTDOWN`・`STALE` | 両方閉じて、消せた | `OS32_ERR_AGAIN` | IDLE | 可 |
| 13 | 2 主 (d) | `NOSPC` | 消せた | `OS32_ERR_NOSPC` | IDLE | 可 (同じ結果になり得る) |
| 14 | 2 主 (d) | `IO` (読み書きの `VFS_ERR_IO`、書けた長さが足りない) | 消せた | `OS32_ERR_IO` | IDLE | 可 |
| 15 | 2 主 (d) | ほかの負 | 消せた | 元の負値 | IDLE | 可 |
| 16 | 2 主 (d) | どの原因でも | 消せない | `OS32_ERR_BUSY` | Q(ファイル) | 不可 |
| 17 | 2 主 (e) | identity が取れない | 無し (`pending` を立てずに続ける) | (続ける) | — | — |
| 18 | 3 probe | `NOTFOUND`・`IO` | 無し (辞書は残す、`pending` はそのまま) | 元の負値 | IDLE | 可 (次の回も確かめ待ち) |
| 19 | 3 probe | `BUSY`、続く `boot_check(0,0)` が 0 (枠を取れなかった) | 無し (`pending` はそのまま) | `OS32_ERR_AGAIN` | IDLE | 可 |
| 20 | 3 probe | `BUSY`、続く `boot_check(0,0)` が `SQLITE_BUSY` (終了の失敗) | 無し | `OS32_ERR_BUSY` | Q(接続) | 不可 |
| 21 | 3 probe | `BUSY`、続く `boot_check(0,0)` がそのほかの 0 でない値 (確かめの失敗) | 無し | `OS32_ERR_BUSY` | Q(接続) | 不可 |
| 22 | 3 probe | `INVAL`、`pending` が 0 (既設・確かめ済み・identity を取れなかった辞書) | 消さない | `OS32_ERR_INVAL` | IDLE | 可 (同じ結果) |
| 23 | 3 probe | `INVAL`、`pending` が 1 だが今の辞書の identity が控えと違う・取れない | 消さない、`pending = 0` | `OS32_ERR_INVAL` | IDLE | 可 (同じ結果) |
| 24 | 3 probe | `INVAL`、`pending` が 1 で identity が控えと同じ (この回に写したか、前の回に写して確かめ待ち) | 消せた、`done = 0`・`pending = 0` | `OS32_ERR_INVAL` | IDLE | 可 (搬送元を直せば通る) |
| 25 | 3 probe | 同上 | 消せない | `OS32_ERR_BUSY` | Q(ファイル) | 不可 |
| 26 | 4 切替の版 (a)(b) | 写し先に在る / 搬送元に無い | — | (`done` / 覚えを変えずに続ける) | — | 次の prepare でまた見る |
| 27 | 4 切替の版 (a)(b) | `HOSTDOWN`・`STALE` | 未着手、消さない | `OS32_ERR_AGAIN` | IDLE | 可 (主は `done` のまま、この版から続き) |
| 28 | 4 切替の版 (a)(b) | ほかの負 | 未着手、消さない | (覚えを変えずに続ける。ログ 1 行) | — | 次の prepare でまた見る |
| 29 | 4 切替の版 (c) | 負の全部 | 搬送元を閉じるだけ。消さない | (その版を `skip` にして続ける。ログ 1 行) | — | この起動ではしない |
| 30 | 4 切替の版 (d) | `HOSTDOWN`・`STALE` | 消せた | `OS32_ERR_AGAIN` | IDLE | 可 (この版から続き) |
| 31 | 4 切替の版 (d) | `HOSTDOWN`・`STALE` 以外の負の全部 | 消せた | (その版を `skip` にして続ける。ログ 1 行) | — | この起動ではしない |
| 32 | 4 切替の版 (d) | どの原因でも | 消せない | `OS32_ERR_BUSY` | Q(ファイル) | 不可 |
| 33 | 5 予約 `boot_check(0,reserve)` | `SQLITE_BUSY`・`SQLITE_MISUSE` | 無し (巻き戻しを呼ばない) | `OS32_ERR_AGAIN` | IDLE | 可 |
| 34 | 5 予約 | `SQLITE_NOMEM` | 無し (同上) | `OS32_ERR_NOSPC` | IDLE | 可 |
| 35 | 5 予約 | ほかの 0 でない値 | 無し (同上) | `OS32_ERR_IO` | IDLE | 可 |
| 36 | open | `ime_boot_open_dict()` が 0 でない | 閉じられた | open の値 (正の SQLite の値は `OS32_ERR_IO` に直す) | IDLE | 可 |
| 37 | open | 同上 | 接続を閉じられない (`ime_boot_abort` が -1) | `OS32_ERR_BUSY` | Q(接続) | 不可 |
| 38 | open | 同上 | 閉じたが予約を解除できない (`boot_check(0,0)` が 0 でない) | `OS32_ERR_BUSY` | Q(接続) | 不可 |
| 39 | commit | `boot_check(接続の数, reserve)` が 0 | — | 0 | READY (`SERVICES_READY`) | 不要 |
| 40 | commit | 0 でない | 閉じられた | `OS32_ERR_IO` | IDLE | 可 |
| 41 | commit | 0 でない | 接続を閉じられない | `OS32_ERR_BUSY` | Q(接続) | 不可 |
| 42 | commit | 0 でない | 閉じたが予約を解除できない | `OS32_ERR_BUSY` | Q(接続) | 不可 |

- 切替の版の probe はしない (切替の時に本体が開いて確かめる)。
- host 試験 (fep_static_host.c) は表の 1〜42 を行ごとに 1 件ずつ持つ。加えて「3 の時に root の媒体に何も作られず、何も消されない」「18 か 19 の後の次の prepare で probe が INVAL → 24 (消える)」「同じ場面で辞書を別の inode に置き換えておく → 23 (消えない)」「27 か 30 の後の次の prepare で、主を写し直さずにその版だけ写す」「20・21 の後は db の slot も不在の側」「Q(接続) で `ime_dict_suspend(0)`・`ime_switch_dict` が BUSY」。
- 変異: 媒体の確かめを外す (3 が RED) / 11 で `vfs_rm` を呼ぶ / 「消せない」を「消せた」として扱う (16・25・32 が RED) / `pending` を呼び出しの中だけの旗にする (18 の後の 24 が RED) / identity の比べを外す (23 が RED) / 21 を IDLE に戻す / 主が `done` なら切替の版を見ない / 33〜35 で巻き戻しを呼ぶ / 37 の `ime_boot_abort` の戻りを見ない。
- **§2-2b 第 3 版の未確認 (PM が確かめた、2026-11-20)**: (1) ramfs は `.ino = &ramfs_ino_ops` (fs/ramfs/ramfs.c の ramfs_ops) を持ち、fs/vfs_fd.c の vfs_open_internal で has_ino=1 になるので vfs_sqlite_fd_identity は file_id を返す。(2) ramfs は `.is_mounted = ramfs_is_mounted` (fs/ramfs/ramfs.c の ramfs_ops) を持つ。
- **§2-2b の最後の確かめの条件 (sol 第 3 回、PM が反映 2026-11-20)**: (1) 宛先ごとの媒体の確かめ — 各宛先 (主・切替の版) の fd を得たら vfs_sqlite_fd_identity の mount_generation を取り、主の辞書の fd の mount_generation と一致しなければその宛先を写さず (作った物は閉じてから消す) OS32_ERR_NOMOUNT・IDLE。主の辞書の mount は `/db` そのもの (vfs_fstype("/db") が "ramfs" — M5 の時点。後続の段で ext2 に替える時はこの条件を直す) であることを行 3 で確かめる (最長一致で /db/fep_s.db などに別の媒体が mount されていても写さない)。(2) M5 の変更のファイルに `kernel/ime/ime_suspend.c` と `kernel/ime/ime_dict_api.inc` を明記。gate は `OS32_FEP_STATIC` の中で関数の先頭、switch では services_public の判定より前に置く。
- **宛先ごとの媒体の確かめの出口 (M5 の実装で、PM 決定 2026-11-20)**: 比べる元は `vfs_path_mount_generation("/db")` (fs/vfs.c の vfs_path_mount_generation なら行 3 と同じく NOMOUNT・IDLE で何もしない)。各宛先は `vfs_path_mount_generation(<宛先のパス>)` を open の前に比べ、違えば開かずに NOMOUNT・IDLE (/db/fep.db そのものに別の媒体が mount された場合もここで止まる)。open の後に vfs_sqlite_fd_identity が失敗した時、または mount_generation が違った時は、作った宛先の fd を閉じてから vfs_rm — 消せたら NOMOUNT・IDLE、消せなければ方針どおり BUSY・Q(ファイル) (再試行しない)。既設・done の主の辞書を開き直して比べることはしない (比べる元は /db の mount の世代)。
- **行 3・行 6 の IDLE の意味 (M5 の実装で、PM 決定 2026-11-20)**: FEP の状態だけを IDLE に戻し、SQLite の公開状態 (services の段・services_public) には触らない — SQLite の起動の失敗の時はその時の値のまま。§2-3 の「IDLE = SQLITE_ONLY・public=1」は SQLite が起動できている時の組み合わせ。
- **宛先の世代の比べの範囲 (M5 のレビュー A、PM 決定 2026-11-20)**: done・skip の版も prepare のたびに比べる (止める側に倒す。今の実装どおり)。

## 止める (2026-11-20 夕、ユーザー)
「今から実装するものはよせて」(新しい 2 重を作らない)。この設計の静的の FEP (OS32_FEP_STATIC、本線の M1〜M5・S3 は wip のまま main に未入) は x86 の fep.mod と違う道なので止める。PowerPC の module の読み込みを x86 の形で入れてから、x86 の fep.mod の形で 7a を出し直す (票 TASK_PPC_ALIGN_X86 の B08・C04)。辞書と学習は x86 と同じパスで、書ける媒体 (ATA) が整うまで学習の公開は待つ (ユーザー決定)。PowerPC の P1〜P4b (main 1c9ed35c5) のうち USB のキーの値の形と修飾の記録は x86 の形なので残す。/db・/dbu の ramfs は module の設計で扱う (使わない方向)。
