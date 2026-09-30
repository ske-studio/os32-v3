# TASK_KAPI_OUTPUT_GUARD — 出力ポインタを受ける既存 KAPI 43 本が、読み取り専用ページに書ける

> 状態: **受入完了 (2026-09-23)** — NP21/W 受入 (§5)。その後 WM の文脈の判定 `ring3_guard_active` (POLICY_DEBUG §4-61) まで着地して v2.1 に同梱。実機は v2.1 の確認 (CHECKLIST_2026-09-26、kselftest 225/225) の範囲で問題なし。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-fable-5-1`、2026-09-23) / それまでの状態: **実装済み・NP21/W 受入済み (2026-09-23、§5)**。
> 出所: Codex 設計レビュー往復 10 (TASK_HAL_WIRING R10-1)。**カーネル層の分かっている不具合**なので POLICY_DEV §1 により新機能より先。

## 0. 症状と原因

- OS32 は **`CR0.WP = 0`** で動いている (`arch/x86/arch_cpu.h` の MMU 有効化。`kernel/shlib.c` がカーネルからの
  書き込みで共有ライブラリを張る前提)。CPL=0 (KAPI の wrapper) は PTE の RO 保護を**受けない**。
- CPL=3 のアプリが KAPI の出力引数に **共有ライブラリの `.text` の番地** (RO、全アプリで共有) や `.rodata` を渡すと、
  `exec/exec.c` の早期検査 (`kapi_argptr` → `ring3_ptr_ok`) は「帯の中か」しか見ないので通り、wrapper がそこへ書く。
  #PF は起きず、アプリは kill されず、**共有コードが壊れる**。
- `ring3_ptr_ok` の注釈「`.text` への書き込みは PTE が RO なので #PF で捕まる」は**誤り** (WP=0)。
- 影響: `sdk/kapi.json` で **非 const のポインタ引数を持つ 43 本** (下の棚卸し)。

## 1. 棚卸し (2026-09-23、kapi.json v58)

| 種別 | 本数 | 例 | 検査 |
|---|---|---|---|
| A. 長さ引数つきの出力バッファ | 12 | `sys_read(fd, buf, size)`, `np2_get_*(buf, size)`, `sys_get_build_info`, `con_sink_read(buf, cap)`, `host_read(h, buf, cap)`, `launch_take(buf, cap, …)`, `ime_user_list(prefix, out, max)`, `dev_get_info(idx, name, nm, …)`, `sys_redirect_fd_buf(fd, buf, size, len)`, `dev_blk_read(dev, lba, count, buf)` (count × セクタ長) | `writable(buf, len)` |
| B. 固定長の構造体 / スカラ出力 | 24 | `rtc_read`, `ide_identify` / `ide_get_info`, `path_parse`, `sys_stat` / `sys_fstat`, `mouse_poll`, `gfx_screen_info`, `gfx_stats`, `pci_get` (40B), `console_get_size(int*, int*)`, `gfx_get_palette(idx, u8*, u8*, u8*)`, `kcg_read_ank` / `kcg_read_kanji` (16B / 32B)、`tvram_readchar_at(x, y, u16*, u8*)`, `loop_status`, `con_sink_stat`, `launch_poll`, `host_status`, `exec_last_result`, `serial_get_status`, `ide_read_sector(drv, lba, buf)` (512B), `gfx_get_framebuffer(fb)` | `writable(p, sizeof)` — 1 本の KAPI に複数あるものは**全部検査してから 1 つも書かない** (TASK_HAL_WIRING 1-5 と同じ規則) |
| C. 入力だけのポインタ (解放・ロック) | 3 | `mem_free(ptr)`, `sys_shm_lock(ptr)`, `sys_shm_free(ptr)` | 書かないので対象外 (既存の所有者検査のまま) |
| D. 関数ポインタ / 表 | 4 | `sys_ls(path, cb, ctx)`, `gui_register(handler, pump)`, `gfx_present_raster(table)` (読むだけ), `ime_set_render(table)` (後で CPL=0 で呼ぶ — §6 追記 2 で常駐側だけに) | 書かないので対象外。ただし **cb / handler はアプリのコード帯 (present + USER) であること**を確かめる (別の穴: 帯検査だけでは SHM や VRAM を関数として呼べる) |

計: A 12 + B 24 + C 3 + D 4 = 43。**A と B の 36 本が対象**。

## 2. 設計

1. **helper は TASK_HAL_WIRING 1-5 の `ring3_user_range_writable(p, len)` をそのまま使う** (実装 A が新設。IF=0 で master CR3 に
   切り替え、保存したアプリ PD の PDE/PTE が present + RW + USER であることを範囲の全ページで確かめ、CR3 → IF の順で復元。
   CPL=0 の直呼びは対象外。1 回の呼び出しで複数の範囲をまとめて検査する形 `ring3_user_ranges_writable(n, ranges[])` を足す)。
2. **検査の置き場は生成される wrapper** (`kapi/kapi_generated.c`)。`sdk/kapi.json` の各エントリに任意キー
   `"out": [{"arg": "buf", "len": "size"}]` / `{"arg": "info", "size": 40}` / `{"arg": "buf", "len": "count", "unit": 512}` を足し、
   `tools/gen_kapi.py` (名前は実装時に確認) が wrapper の先頭 (target を呼ぶ前) に検査を出す。**36 本すべてに `out` を書く**
   (書いていない非 const ポインタ引数があれば `make check` (`check-kapi-out`) が落ちる — 種別 C/D は `"out": "none"` で明示)。
3. 検査不合格は **`ring3_fault_kill()`** (TASK_HAL_WIRING 1-5 と同じ。NULL は wrapper の既存の扱い (負や無視) を保つ)。
4. `ring3_ptr_ok` の誤った注釈を直し、WP=0 の事実を `docs/02_memory.md` の方針に 1 行足す。
5. **KAPI の版は上げない** (署名は不変。検査は追加だけ)。`make clean` → `make all` → `make external` は生成物が変わるので必要。

## 3. 受入

| ID | 見るもの | 手段 |
|---|---|---|
| G1 | ホスト試験: `out` 記述の解釈 (len 引数 / 固定 size / unit) と生成コードの検査順 (全範囲を検査 → 書く)。`out` 無しの非 const ポインタで生成が落ちる | `check-par` + `check-kapi-out` |
| G2 | NP21/W、CPL=3 の試験バイナリ: 36 本のうち代表 6 本 (A: `sys_read` / `np2_get_version`、B: `rtc_read` / `console_get_size` / `pci_get` / `ide_read_sector`) に **shlib の `.text` の番地** を渡す → kill、`.text` の内容が不変 (md5)。2 つ目の出力だけ RO → 1 つ目も不変。正常系 (heap / stack / shlib `.data`) は成功 | kselftest post-exec + 試験バイナリ |
| G3 | 種別 D: `sys_ls` の cb に SHM の番地を渡す → kill (コード帯でない) | 同上 |
| G4 | 既存アプリの回帰: `make external` の全アプリと gshell、shell の `ls` / `cat` / `stat` / `serial` / `lspci` が通る (出力バッファは heap / stack) | NP21/W |
| G5 | コスト: CR3 の往復が 1 KAPI 1 回。`sys_read` 4KB × 1000 回の所要時間の前後差を kselftest で測って記録 | NP21/W |

## 4. しないこと

- `CR0.WP = 1` への切り替え (shlib のロードがカーネルからの書き込みに依存。対象 CPU (386 は WP 無し) の確認も要る)。
- 可変長引数 (`kprintf` 系) の検査 (フォールトガードのまま)。
- const ポインタ (入力) の present 検査 (読みの #PF はフォールトガードが拾う)。

## 5. 受入の記録 (PM、2026-09-23、NP21/W)

| ID | 結果 |
|---|---|
| G1 | 合格: `check-kapi-out` (解釈 8 件の拒否、45 本すべてに `out`)、`make check` 全通過 |
| G2 | 部分: `kout_test` (CPL=3) で 6 本の代表 (`sys_read` / `np2_get_version` / `rtc_read` / `console_get_size` / `pci_get` / `ide_read_sector`) の正常系と NULL の既存挙動を確認 (PASS、skip 2 = 従来から NULL で死ぬ 2 本)。**shlib `.text` を出力に渡して kill される経路はアプリから安全に作れないので未実施** (アプリ自身の `.text` は RW 帯) |
| G3 | 未 (`sys_ls` の cb にコード帯以外を渡す検査は今回の対象外) |
| G4 | 合格: 起動、kselftest 194/194、`ls` / `cat` / `serial` / `lspci`、`time_test`。gshell は FD 起動では出ない (NHD 環境で次回) |
| G5 | 未 (CR3 往復のコストの実測) |

実装で決めたこと: `"out": "target"` (自前で検査する `sys_time_now` / `pci_bind_info`) を第 3 の形として認め、二重の CR3 往復を避けた。
対象は v58 の 43 本 + v59/v60 の 2 本 = 45 本。`dev_blk_read` は wrapper の 512 単位に加えて**本体で `sect_size` の実長を検査** (7ae93c7)。
残る穴: `np2_recv_str` は `maxlen <= 0` でも 1 バイト書く (長さ 0 は検査しない規則の外。別途)。既知: NULL を渡すと死ぬ 4 本 (`np2_get_*` / `rtc_read` / `dev_get_info(name)` / `ide_read_sector`) は従来どおり。

**実装レビュー (Codex、2026-09-23) → Request changes 5 件を反映 (コミット後の NP21/W 再確認: 194/194、`kout_test` PASS、`make check` 通過)**:
(1) `dev_get_info` は `nm <= 0` で終端の 1 バイトを書いていた → 拒否。(2) `dev_blk_read` の負の `count` が CD-ROM 経路で大量読みになる → 拒否。
(3) `sys_redirect_fd_buf` の `len > size` で検査範囲外へ書けた → 拒否。(4) リダイレクト登録後に `sys_shm_lock` でページが RO になっても書けた →
書くたびに再検査 (ユーザ帯のバッファだけ。`fs/fd_redirect.c`)。(5) `dev_blk_read` の 512 単位の先行検査が 128/256 バイトセクタの正常呼び出しを
殺す → 先行検査を外して本体の実長検査だけに。非 blocker のうち `np2_recv_str` の `maxlen <= 0` の 1 バイト書きも直した。
残る非 blocker: 生成器の const 判定は文字列一致 (`char *const p` などを見分けない。現行 45 本には無い)、`kout_test` は RO 拒否そのものを踏まない (アプリから安全に作れない)。

## 6. 追補 — WM の文脈では検査を効かせない (2026-09-26)

この検査が入って以後、**ポンプ / OP_WAIT を通る GUI アプリは起動直後に kill されていた** (filer が窓も出さずに消える、
`fault_kill_count` +1、呼び手は `wrap_mouse_poll+0x35`)。WM (gshell、CPL=0) はアプリの syscall の中で走る (契約 T8) ので
`ring3_in_syscall` だけでは「アプリ由来」と「常駐側の直呼び」を区別できず、gshell のスタックの `MouseInfo` が拒否された。
直しは「カーネルが WM のコードへ入っている深さ」`ring3_wm_depth` (gui_call のハンドラ / ポンプ / owner_exit の前後) と
`ring3_guard_active(in_syscall, wm_depth)` (`exec/ring3_str.c`)。3 つの門 (書き側 / 読み側 / `tramp_copy`) が同じ判定を使う。
書き側の拒否も `RING3_RANGE_WR_*` (7〜10) で数えるようにした (それまで `ring3_range_reject_count` は書き側を数えていなかった)。
安全性: 深さが 1 以上なのはカーネルが WM を同期的に実行しているあいだだけで、その間 CPL=3 は走らず、KAPI に渡るポインタは
WM が選ぶ (gshell は `arg` をポインタとして解釈しない — 入力は SHM のスロット経由)。#PF/#GP の帰属は変えない。
試験: `tools/tests/ring3_guard_tdd.md` (ホスト、変異 4/4 RED) + kselftest `test_ring3_wm_guard` (ゲスト)。
教訓: `docs/POLICY_DEBUG.md` §4-61。
追記 (同日、代行レビュー P2): 実装レビュー 4 の「最後の砦」(`fd_redirect_write`) はアプリが登録したバッファ
(`user_origin`) なら深さに関係なく `ring3_user_ranges_writable_always` で歩き、登録時も同じ歩きで RW + USER を確かめる
(試験 `ring3_guard_host.c` §4、変異 7/7 RED、kselftest `test_fd_redirect_origin_guard`)。
追記 2 (同日、代行レビュー P2): §1 の D 行の `ime_set_render(table)` は「読むだけ」ではなかった — カーネルは控えた
`IME_Render` 表の関数を**以後ずっと CPL=0 で呼ぶ**ので、アプリの表を受け取るとアプリのコードが CPL=0 で走り、アプリの終了後は
解放済みの物理へ飛ぶ。KAPI の target を `gui_ime_set_render` (`kernel/gui.c`) に替え、owner 1 かつ `ring3_call_from_user()` が偽の
呼び手 (gshell の top-level) だけを通す (戻り void・番号・引数は不変、断った回数は `gui_ime_render_rejected`)。同じ形の残り:
`gui_register(handler, pump)` は owner 1 の検査だけで `ring3_call_from_user` は見ない (owner 1 は常駐シェルだけなので現状は穴ではない。
**同日の代行レビュー P3 で同じ門に揃えた** — `ring3_guard_host.c` §6)、
`sys_ls(path, cb, ctx)` の `cb` は**同期的に** CPL=0 で呼ばれる (CPL=3 の呼び手のコードが CPL=0 で走る。寿命の問題は無いが特権の
問題は残る — 別票)、`gfx_present_raster(table)` はデータの表で関数を含まない。試験 `ring3_guard_host.c` §5 (変異 11/11 RED)、
kselftest `test_ime_render_gate`。
追記 3 (同日、代行レビュー P3): WM を信頼境界の内側に置く (深さ 1 以上で門を素通し) 一方、フォールトの帰属は `ring3_in_syscall`
のままなので **WM の中で落ちるとアプリの kill として畳まれ、`fault_kill_count` しか増えない**。挙動は変えず、観測点
`ring3_wm_fault_count` (`exec/exec.c`) と ISR の kill の行の ` (in WM)` を足した。関係の説明は `docs/POLICY_DEBUG.md` §4-61 の追記 3。
