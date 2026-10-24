# T3d1 補遺 — ファイルの組表をカーネル所有ページへ読む契約

> 上位: [TASK_T3_LAYOUT](TASK_T3_LAYOUT.md)

> 状態: **受入完了 (2026-10-17)** — T3d1a (b68b5d7)・T3d1b (244e268) で着地、NP21/W 64MB の受入に合格 (423a5b5、`~/os32-tmp/evidence/2026-10-17/t3c2_t3d1_np21w/RESULT.md`)。GUI の端末からの確認は台帳 T3C2-GUI。
> それまでの状態: **設計中 (2026-10-17、設計は確定し実装待ち)** — T3 票 §6 のユーザー決定 (Unicode の組表は像に入れない) を受けた T3d1 の契約の補遺。astra 起草 (r1・r2)、Opus 5.5 の独立レビュー (r1「P2 を追記すれば確定してよい」、r2 で反映)。往復の記録は `~/os32-tmp/run/t3d1/`。上位は [TASK_T3_LAYOUT](TASK_T3_LAYOUT.md) §3・T3d1 の行・§6。


2026-10-17。設計案。OS32 は単一利用者・単一タスク・協調切替、攻撃者を想定しない。装置の誤動作・実装の誤りから戻れる範囲を扱う。以下の file:line は `/home/hight/os32-v3` の参照時点。実装・ビルド・ゲスト試験は未実施。
`docs/archive/v3/TASK_T3_LAYOUT.md:182` の決定を継承し、表は像に含めない。通常 +10,158 B、R1 +11,527 B の上限超過という T3d0 の結論を変更しない。

## 1〜3. ファイル形式・配置・会計 / 読込順序と失敗 / 媒体

受入後 (2026-10-22) に現行の契約として [KAPI_SPEC §4-13](../../KAPI_SPEC.md#4-13-unicode_to_jis-v76) へ移した (RAM の勘定は [02_memory §2-1](../../02_memory.md))。設計時の file:line と旧低位 128KiB の扱い (T7a-2e で V86_LOW に統合) は移していない。

## 4. 撤去・置換と caller

| 現在の箇所 | T3d1 の置換先 |
|---|---|
| `kernel/kernel.c:670-690` | 低位read/ready設定/registerを削除、新読込を§2へ。post_unicode自己試験は新契約へ接続。 |
| `lib/utf8.c:28-105,288-308`、`lib/utf8.h:61-63`、`lib/utf8_internal.h:6-9` | 表ポインタ、ready、probe、setterを撤去。kernel版は製品探索を直接呼び、ユーザー版 `unicode_to_jis` はKAPIだけを呼ぶ。かな算術もkernel側に集約。 |
| `exec/system_surface.c:7-18,29-35`、`.h:4` | Unicode登録・source・ready参照を削除、TVRAMを維持。 |
| `exec/surface_query.c:105,141,238` | Unicode role の許可・RO特例・source分岐を撤去。公開queryは `OS32_ERR_INVAL`、他role維持。公開role番号4と既存KAPI slotは再利用・削除しない。 |
| `userland/lib/gfx/libos32gfx_core.c:12-14,52-67,78-112,198`、`libgfx_attach_internal.h:26-27` | Unicode port/ref/view、CPL0低位直読、query/lease/unlease、unicode_initと呼出しを削除。静的SDK・shlibとも `lib/utf8.c` のKAPI stubへ。 |
| `include/memmap.h:117-118`、`drivers/kcg.c:220-221,251`、`kernel/memory_boot.c:210-214` | Unicode定数を撤去、scratchを `MEM_KCG_LZ4_TEMP_BASE/END`（0x4A000〜MEM_CONV_END）へ分離。低位予約は§1、closeは§2。 |
| `tools/gen_memmap.py:264,270-272`、`docs/02_memory.md` §2-1 | フォントキャッシュ終端を `MEM_KCG_LZ4_TEMP_BASE - 1` に、Unicode行をKCG scratch（FIXED・boot限定）へ変更し、MEMMAP.md・layout-manifestも整合させる。 |
| `kernel/pgalloc.h:280`、`sdk/include/os32/os32_surface.h:8`、`tools/mkmod.py:20`、`userland/rust/about/src/info.rs` | role 4は予約済み公開番号として定義だけ残し用途説明を更新。aboutの旧Unicode表説明を更新し、mkmodの `selftest_post_unicode` ops位置は維持する。 |
| `kdebug/kselftest.c:971-979` | RO surface登録の期待を、非公開・owner頁数・変換結果の確認へ置換。kdebug import/対の部品も更新。 |
| `kernel/console.c:338,431`、`kernel/ime_render_tvram.c:40` | 既存の関数名のままkernel内探索へ。 |
| `userland/lib/gfx/text/gfx_kcg.c:98`、`text/lconsole.c:234` | 静的SDK/shlibの同じKAPI stubへ。`build/programs.mk:188` のutf8_progとSDK/shlib双方のリンクを確認。 |
| `userland/gshell/src/chrome.rs:271`、`src/taskbar.rs:522` | gfxのUTF-8描画経由で新KAPIへ。`src/ffi.rs:18` のANK用FFIは維持。 |
| `userland/rust/t5a_display/src/guest.rs:980-986`、`userland/rust/libos32gui/src/ffi.rs:49` | 前者の `unicode_to_jis` FFIは同じC stubへ、後者の不要unicode_init宣言を削除。Rust生成KAPIにも新slotを反映。 |

§4の撤去受入では `rg -n 'MEM_UNICODE_TABLE|utf8_set_jis_table|utf8_validate_jis_table|jis_table_ready|system_unicode_register|ROLE_UNICODE|SURFACE_UNICODE|unicode_init' tools/tests kernel exec kdebug sdk userland tools/*.py` の全一致を確認し、旧契約を置換する。許可リストは `kernel/pgalloc.h` の `LEDGER_ROLE_UNICODE=4`、`sdk/include/os32/os32_surface.h` の `OS32_SURFACE_UNICODE=4` の予約定義と、非公開（INVAL）を確認する試験参照だけ。
対象には `tools/tests/` の kcg_boot・memory_boot・memmap_audit・kdebug_init・kdebug_loader・app_bb_overlap・gfx_attach・surface_query・surface_lease・r1_lease の各 `_host.c`、`gui_reattach_host.rs`、`r1_recovery.py`、`test_shm_user.py`、`test_e10c_lifecycle.py` 等の差替え定義も含める。新kernel読込・探索ファイルを `check_map.d` の対応表へ追加する。
`docs/tasks/v3/T2H_EXTERNAL_GATE.md:17` のRO lease経由という説明をKAPI経由へ更新し、`game/app/main.c:183-207` の旧setter撤去・KAPI移行はH-7の再開時へ持ち越す（いまgameは直さない）。

`sdk/kapi.json` の75を起点に機能版を予約・更新（他の追加がなければ76）、`u32 cp → u16 JIS` を末尾追加、cdecl wrapperとC/Rust生成物・stampを揃える。[ABI1]〜[ABI3]を遵守し、表ポインタはどのcallerにも渡さない。

## 5. 観測可能な受入（実装後、NP21/W 64MB）

1. 同世代一式をHDD/FDへ配備する予行で既存deploy-setの長さ・CRC照合と28,252 Bを確認。CUIの日本語、GUIのgshell・静的SDK描画・shlibアプリを同じ文面で基点と比較し、かな・漢字・未収録文字の表示が一致、GUI終了後CUIとFEPも一致する。
2. rootのunicode.binを除いた試験用媒体で起動し、診断1行、shell到達、かな算術の成功、漢字・未収録/BMP外の0、Unicode用owner頁増分0を確認。復元後の再起動で全文字が戻る。切詰め・過長（旧128KiB含む）・逆順・重複・read/割当失敗はhost fixtureで同じ失敗契約と頁返却を確認する。
3. 正常時に表はkernel所有7頁の1実体、ユーザーへの公開0、Unicode lease取得0、queryはINVAL。他roleは従来どおり。[mem] pages は `ok diff=0`、旧低位32頁の勘定移動を照合する。同じビルドの「表あり／なし」でowner使用頁+7・空き頁−7を比較し、正常ログの先頭PFNから配置領域を判定する。SLACK配置時もpost_execのslack_cycleが通る。
4. close後のfont_load_testはNOSYSでPASS、BB/mailbox不変と次描画成功をhost/kdebugで確認。`tools/tests/guest_tests.txt:27` と `test_result_conv_host.c:856-866` のPASS/FAIL/SKIP判定は維持する。
5. T3d0の `unicode_lookup_*` / `test_unicode_lookup.py` を**製品探索と生成ファイルを読んだ表**へ接続。7,063組・全BMP・かな端点・BMP外を旧結果と照合し、kernel直接/SDK KAPI経路を確認。`test_unicode_surface.py:15-35,60-65` とfixtureの旧lease/ready期待を非公開・KAPI・fallbackへ置換し、TVRAM/描画等の既存回帰は残す。`tools/tests/test_fdc_track.py:42-49`・`fdc_track_host.c:1479` の `font_replay` はMINIMALに残る64KiB以上のファイルへ対象を替え、複数トラック読込の検証力を保つ。
6. 通常/R1各々で同じ設定・基点との差と圧縮長を記録し、**基点 +512 B 以下**、かつ **≤520,192 B**。縮小量は記録だけとし下限を設けない。超過は理由・実測を戻して再判断し、表埋込や上限変更で逃がさない。像・7頁RAM・KHEAP・SLACK予算を別記する。旧低位128KiBを残すため **T7aまでRAM +7頁（28KiB）**、2MB構成でnomemにならないことはT3d2で確認する。C/Rust版・stamp一致、旧直読/旧低位read/lease取得0を静的にも確認する。外部apps/gameはH-7の再開ゲートを維持し、未実行を合格に数えない。

## 6. 実装の段

**2段のTx** とする。d1aは新形式生成と読込/検証/探索のhost器具を準備し、製品用の内部関数を試験で実行する（稼働系の形式・読込経路はまだ切り替えず、試験用生成物を分離）。d1bで製品生成・boot読込・KAPI・全caller・lease撤去・scratch名・既存試験を一体切替する。中間版を配備しない。c2/d0の依存は維持する。
各段は `make kernel`（触ったuserland像も）＋直接試験 `MUTATE=0`＋`check_select.py --lint`。d1b後にABI3の `make clean → make all`、Tx全差分の候補 `MUTATE=0`、静的5本（p2v/c-dialect/constraints/arch-asm/privileged）を1回。その後の独立レビュー・PMの全体検査・§5のゲスト受入を経て完了とする。本補遺の作成時にはこれらを実行しない。
