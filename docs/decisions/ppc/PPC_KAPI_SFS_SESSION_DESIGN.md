# PPC_KAPI_FILL 段 6b の確定設計 (SFS_SESSION)

> 状態: **確定 (2026-11-20)** — 本線 PM。Fable 起草、sol レビュー 2 回、確定の節を PM が反映 (6c はユーザー決定 U1・U3 も)。出所は本線の機械の ~/os32-tmp/run/kfill-6b/。段の状態は票 TASK_PPC_KAPI_FILL が持つ。

# 段 6b 詳細設計 r2 — serialfs の session の所有と PowerMac の後端の口

- 対象の計画: docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md §2 serialfs、§3 6b、§5 の 6b 受入 (serialfs)
- 基点: origin/main 112bf4cdc (行番号はこの版)。読んだだけで、ビルド・試験・build_compare は未実施 ([V4])。寸法は r1 の時の手元の map の値で、測り直していない。
- 公開する KAPI: `sfs_begin` / `sfs_end` (sdk/kapi_target_ppc.py の 'serialfs' の組)。

## r1→r2 の変更 (REVIEW.r1 の指摘ごと)

| 指摘 | 変更 |
|---|---|
| P1 G3・G4 | G3 を取り下げた (r1 は `.inc` を grep から漏らした)。PC-98 は起動時に常駐を始める (sys/platform/pc98/kernel_boot_memory.inc の kernel_boot_hostsvc)。PC-98 にも describe を配線し、`nic=82557 peer=… sid=…` と `sfs: exit=N` の行を 1 文字も変えない (tools/rshell_serial.py の lan_result のパーサーまで受入) |
| P2 G1・G2・G5〜G7 | G2 に PC-98 の起動経路、G5 の観測先を診断ログに、G1 の行番号を origin/main に更新 |
| P1 口・段の順 | `pm_hfs_describe` は static をやめ hostfs.h に宣言、像ごとに定義 1 つ。常駐の枝の SfsLan 依存の除去 (describe) を分割の**前**に置いた。begin/end の全分岐と戻り値を §2-3 に書いた |
| P1 A7 | 「対向を止めるだけ」をやめ、再 HELLO が要る状態を作ってから期限を見る形に。LINK の期限切れの handle 解放・新 sid の読取・旧 FD の STALE を足した |
| P1 scope の資格と回収 | scope に owner を記録し、end は開いた owner だけ、開いた owner の終了で scope だけ回収する (§2-2、段 M2b・M4・M5) |
| P1 A5・A8・U1 | PowerMac の kprintf は LAN の診断ログへ出る (sys/platform/powermac/log_printf.c、platform/powermac/console_log.c の pm_diag_write)。観測点は追記で link の結果 RSH OUT に確定 (診断 LOG の案は取り下げ)、W8 設計と計画の 6b 受入 の更新を段 P6 に入れた |
| P2 段の単独受入・比較 | 旧経路が動いたまま口を足す → 登録 → 切替 → 純移動、の順に分けた。関数差は「見込み」でなく実測の申告。直前の版と a76398a35 の両方と比べる。PPC の公開表の欠落の期待 (tools/ppc/test_kapi_binding.py の EXPECTED_ABSENT_SLOTS) の更新を P5 に |
| P2 試験の届き方 | A2 は製品の登録を通るゲスト、A6・A7 は実物の常駐所有者を組む host、A8 は実物の boot 経路を組む host に割り当て直した。A8 の変異は begin 側の再初期化に |
| P2 契約 ID | AC-SFS-1・AC-CTX-1/4・AC-KAPI-1 を §1-2・§2-2 に結んだ。LINK の FG1 待機と非 LINK の TB 時計の poll を明記 |
| 未決 | U2 は PM 決定 (a) で確定。U1 も PM 決定 (追記) で確定し、未決は 0 件 |

## r2 追記 (2026-11-20、ユーザーの指摘「SerialFS は廃止予定」を受けた PM の方針)

出所: docs/decisions/network/LAN_OPERATOR_DESIGN.md §1 統合判断 3 (運び方の統合)、§2 (結果の扱い)、§5 (SFS CALL)。生の SFS の LAN の経路 (lanfs_host 相手) は廃止予定、/host の mount・VFS・SfsClient は link の SFS CALL の上に残る。本文の該当行は書き直し済み。

| 項目 | 追記後の扱い |
|---|---|
| PowerMac の session が載る後端 | **link の SFS CALL だけ** (sys/platform/powermac/hostfs_link_io.c + hostfs_link_peer.c、FEATURE_LINK=1 の像)。非 LINK の PLN1 の後端 (hostfs_io.c・hostfs_peer.c) は LAN_OPERATOR §5  で link へ寄せる側なので、6b では触らず describe も足さない (r2 の P2 を削除)。PC-98 の生の LAN の常駐 (SfsLan・82557) を PowerMac に持ち込まない |
| 公開の範囲 | `sfs_begin`・`sfs_end` を受入するのは LINK の像だけ。非 LINK の像は入口が入っても受入の対象外で、公開表を像の機能で分けられるかは P5 の着手時に確かめる (未読)。分けられなければ非 LINK は「常駐の scope は動くが describe 無し (`nic=?`)・受入なし」と記録する |
| x86 の常駐 (生の LAN) | 今のまま。廃止は後続関門 (LAN_OPERATOR §5、TASK_LAN_BULK) で、6b は触らない・壊さない。describe の配線は x86 の側に要る分だけ (M3、行を今と同一に保つため) |
| U1 (exit code の観測点) | **PM 決定でユーザー判断から外す**: link の結果で受ける。LAN の rshell の結果 `RSH OUT <boot-token> <id8> <drop> <len>` の本文 (echo / 本文 / EOT) に入る既存の `sfs: exit=N` の行で判定する (LAN_OPERATOR §2「SFS は既存 `sfs: exit=N`」)。診断 LOG・SFS_T_EXIT は判定に使わない |
| 上を成り立たせる直し | 常駐の 2 行 (`sfs: transport=lan …`・`sfs: exit=N`) は端末向けの `console_printf` で出す。PowerMac の `kprintf` は診断ログ行きで結果の capture に入らないため (sys/platform/powermac/log_printf.c の 診断ログへの出力)。x86 は `kprintf` が `console_printf` の alias (sys/kern/kprintf.c の kprintf) なので出力も呼び先も変わらない見込み (実測で申告)。M4 に含める |
| PowerMac の nic の表示 | `nic=link` (GEM の名を出さない。後端は link で、NIC は link の下)。peer は `pm_hfs_peer()` |

## 1. 実物の確認

### 1-1. 計画の前提と実コードの食い違い (6 件。r1 の G3 は取り下げ、番号は据え置き)

| # | 計画の前提 | 実物 | 影響 |
|---|---|---|---|
| G1 | 共通に移せば足りる | 置き場は共通だが PC-98 の装置・設定に直結 (契約ヘッダ経由): 取り込み sys/fs/serialfs/serialfs_session.c の PC-98 用の依存ヘッダ、`g_nic = { e82557_* }` の初期化、`SFS_LAN`・`lan_module_session_acquire` の取得、`serial_gate_*`・`serial_hold_*`、`res_owner_get` serialfs_session_begin・serialfs_session_end。build/sources/common.mk の C_COMMON_PC98_PENDING の C_COMMON_PC98_PENDING で PowerPC の像に無い | 「移す」でなく「分ける」段 |
| G2 | session の所有は未確定 | 常駐の所有者は既にある (sys/fs/serialfs/serialfs_persistent.c の sfs_persistent_start)。始める所は 2 つ: PC-98 = kernel_boot_hostsvc (sys/platform/pc98/kernel_boot_memory.inc の kernel_boot_hostsvc、`SFS_LAN=1` とネットワークの開始の成功が条件、ctx は `SfsLan boot_sfs_lan`)、PowerMac = pm_hostfs_boot (sys/platform/powermac/hostfs_boot.c の pm_hostfs_boot、ctx は `pm_hostfs_transport`) | 所有は常駐の所有者に寄せて確定 (§2) |
| G4 | (記述なし) | 常駐の枝が `c->ctx` を SfsLan と読み `nic=82557` と出す (sys/fs/serialfs/serialfs_session.c の serialfs_session_begin)。PC-98 では正しいが、PowerMac に公開した時に別の型を peer として読む | describe の口 (§2-3)。直さずに公開しない |
| G5 | 変異「EXIT frame の送信抑止」 | 常駐の end は scope を外して `sfs: exit=%d` を kprintf するだけ (sys/fs/serialfs/serialfs_session.c の serialfs_session_end、sys/fs/serialfs/serialfs_persistent.c の 常駐セッションの終了)。PowerMac の kprintf は LAN の診断ログ行き (platform/powermac/console_log.c の pm_diag_write)、端末のキャプチャには出ない | 2 行を console_printf で出し、観測点は link の結果 RSH OUT (追記) |
| G6 | 「PPC hostfs_link_io.c」が後端 | 後端は既にあり SfsIo の 6 口を満たす (hostfs_link_io.c、非 LINK は hostfs_io.c、選択 build/platform/powermac.mk の C_PLATFORM_USERSHELL)。足りないのは説明だけ | PowerPC の段は小さい |
| G7 | 常駐も shell だけ | owner の検査 (serialfs_session_begin・serialfs_session_end) は常駐の枝の後。常駐は誰でも scope を取れ、owner の記録が無く、end せず終わると後続が BUSY のまま、別の owner の end も通る | 公開前に資格と回収を決める (§2-2) |

補足: 計画外の前提として docs/decisions/ppc/PPC_SHELL_W8_DESIGN.md  の KAPI の扱い「KAPI の sfs_begin/end 不在指定は維持」があり、6b の公開はこれを更新する (P6)。

### 1-2. 統合の判断 (POLICY_DEV §1)

| 問い | 判断 |
|---|---|
| 既存の機能・計画 | ある。常駐の所有者、SfsIo (sys/fs/serialfs/sfs_client.h)、両機種の常駐の起動、PowerMac の 2 つの後端を使う。新しい所有者・transport の型は作らない |
| HAL で 1 つの実装か | する。入口と scope は両像で 1 つ。機種の差は (a) transport の説明 (describe)、(b) 前景の一時 session の有無 (登録の有無)。同名の代役は作らない |
| 計画の向き | 合う。U2 の PM 決定どおり PowerMac に UART の前景は足さない |
| 互換契約 (docs/decisions/ARCH_COMPAT_CONTRACT.md) | AC-SFS-1 (can_wait は IRQ 外、絶対期限の共有、新 sid で write を再実行しない)、AC-CTX-1/4 (FG1 の協調点だけで idle_work)、AC-KAPI-1 (不在は NOSYS、公開後は通常 provider)。能力差: PC-98 = 前景あり + 常駐は設定次第、PowerMac = 常駐だけ。共通の試験: scope の列 (A3・A5・A6) を両機種の後端の模擬で同じ試験に掛ける |

## 2. session の所有と口の形

### 2-1. 所有の表

| 物 | 持ち主 | 初期化 | 閉じる |
|---|---|---|---|
| 常駐の SfsClient・/host の mount・世代 | serialfs_persistent.c | `sfs_persistent_start` を起動時に 1 回 (PC-98: kernel_boot_hostsvc、PowerMac: pm_hostfs_boot) | 閉じない (起動の寿命)。scope の回収でも触らない |
| 常駐の transport (SfsIo + ctx) | 機種の後端 | 後端が起動時に | 閉じない (cfg に貸す) |
| scope と **その owner** | serialfs_persistent.c (`scope`、新 `scope_owner`) | scope_begin (owner を記録) | 開いた owner の scope_end、または開いた owner の終了の回収 |
| 前景の一時 session (PC-98 だけ) | 新設 sys/fs/serialfs/sfs_session_fg.c | 前景の begin | sfs_finish (今のまま) |
| 入口と振り分け | sys/fs/serialfs/serialfs_session.c | — | — |

### 2-2. 協調進行、scope の資格と回収

- 待ちは SfsIo の `can_wait` → `idle` → `now` だけ。PowerMac LINK: `pm_hfs_can_wait` = IRQ 外かつ EE=1 (sys/platform/powermac/hostfs_link_io.c の pm_hfs_now・pm_hfs_can_wait)、idle = `idle_wait_step` (FG1 の待機、登録済みの `pm_hfs_link_poll` が OPEN→WRITE→STATUS→READ を進め、期限切れの handle の解放も poll の仕事 の期限管理)。非 LINK: TB 時計で期限を見て poll (sys/platform/powermac/hostfs_io.c の pm_hfs_now・pm_hfs_can_wait)。
- scope_begin の期限は `now + 100 tick`。**sid が生きていれば通信せず成功する** (sys/fs/serialfs/serialfs_persistent.c の sfs_ensure_session)。HELLO は sid 無し・reconnect・idle 3000 tick・seq 上限の時だけ。
- 資格: 常駐の scope は**誰でも開ける (今のまま、x86 を変えない)**。ただし owner (`res_owner_get()` の値を入口が渡す) を記録する。
  - 二重の begin = BUSY (owner を問わず)。begin 無しの end・開いた owner でない end = INVAL (r2 で足す検査)。
  - 開いた owner 自身が end せず終了 → exec の終了の回収が scope だけ外す (mount・client・sid・transport は残す)。
  - 子の異常終了 (開いたのは shell、落ちたのは子) → scope は残り、shell の `sfs_end` が閉じる (userland/shell/rshell.c の sfs_run_child は子がどう終わっても end に来る)。
- 前景 (PC-98) の資格は今のまま APP_ID_SHELL だけ。整数演算だけ。

### 2-3. 口の形 (sys/fs/serialfs/serialfs.h。機種・装置のヘッダは読まない)

```c
struct SfsTransportInfo { const char *nic; u8 peer[6]; u8 peer_valid; };
struct SfsPersistentConfig {
    const SfsIo *io; void *io_ctx; int lan_enabled;
    int (*describe)(void *io_ctx, struct SfsTransportInfo *out); /* 末尾追加、NULL 可 */
};
int  sfs_persistent_describe(struct SfsTransportInfo *out);   /* 負 = 説明なし */
int  sfs_persistent_scope_begin_owned(int owner);             /* 0 / INVAL / BUSY / ensure の負値 */
int  sfs_persistent_scope_end_owned(int owner, int exit_code);/* 0 / INVAL (scope 無し・owner 違い) */
void sfs_persistent_scope_owner_exit(int owner);              /* scope だけ外す。一致しなければ何もしない */

struct SfsSessionFg { int (*begin)(void); int (*end)(int exit_code); };
void serialfs_session_set_fg(const struct SfsSessionFg *fg);
void serialfs_session_owner_exit(int owner);                  /* exec の終了の回収から */
extern const struct SfsSessionFg sfs_session_fg;              /* PC-98 の前景 */
```

入口の全分岐 (最終形):

```c
int serialfs_session_begin(void)
{
    if (sfs_persistent_enabled()) {
        struct SfsTransportInfo ti = { "?", {0}, 0 };
        int rc = sfs_persistent_scope_begin_owned(res_owner_get());
        if (rc != 0) return rc;                 /* INVAL / BUSY / HOSTDOWN / IO */
        (void)sfs_persistent_describe(&ti);     /* 負でも ti は初期値のまま出す */
        console_printf(KLOG_LEGACY_CYAN,
                "sfs: transport=lan nic=%s peer=%02x:%02x:%02x:%02x:%02x:%02x sid=%u\n",
                ti.nic, ..., sfs_persistent_client()->sid);  /* peer 未観測は 00 */
        return 0;
    }
    return g_fg ? g_fg->begin() : OS32_ERR_NOSYS;
}
int serialfs_session_end(int exit_code)
{
    if (sfs_persistent_enabled()) {
        int rc = sfs_persistent_scope_end_owned(res_owner_get(), exit_code);
        if (rc == 0) console_printf(KLOG_LEGACY_CYAN, "sfs: exit=%d\n", exit_code);
        return rc;
    }
    return g_fg ? g_fg->end(exit_code) : OS32_ERR_NOSYS;
}
```

- 行の形は固定 (peer を出さない分岐は作らない)。PC-98 の describe は nic="82557"・peer = boot_sfs_lan.peer で、今と同じ文字列になる。
- 後端の describe: PC-98 = `boot_sfs_describe` (kernel_boot_memory.inc、static で足りる)。PowerMac = `int pm_hfs_describe(void *ctx, struct SfsTransportInfo *out)` を hostfs.h に宣言し、hostfs_link_peer.c (LINK の像だけ) に定義を 1 つ。非 LINK の後端には足さない (追記)。nic="link"、peer = `pm_hfs_peer()`。hostfs_boot.c の `.describe` は `#if KBOOT_LINK` の中 (同じファイルの既存の KBOOT_LINK の条件に合わせる)。

### 2-4. 「ユーザー shell の transport を二重に初期化しない」の守り方

| 守り | 仕組み | 確かめ方 |
|---|---|---|
| 入口は常駐なら transport に触らない | 常駐の枝が呼ぶのは scope_begin_owned・describe だけ。`sfs_client_init`・`sfs_lan_init`・`serialfs_permit`・`vfs_mount` を呼ばない | A3・A8 |
| 前景が像に無い | PowerMac は sfs_session_fg.c を選ばず set_fg を呼ばない | A9 (map) |
| start は 1 回 | 2 回目は BUSY (sys/fs/serialfs/serialfs_persistent.c の sfs_persistent_start)。呼び元は起動の 1 か所 | A8 |
| 常駐が始まらなかった像 | PowerMac は NOSYS (前景へ落ちない)。PC-98 は今どおり前景 | A6 |
| scope の回収が transport を閉じない | owner_exit は `scope` と `scope_owner` だけ戻す | A10 |

## 3. 部品の大きさと x86 の振る舞い

| 項目 | 見込み (実測は各段で申告) |
|---|---|
| x86 の .text | +150〜250 バイト (describe 2 本、owner の 3 本、set_fg、間接呼び出し、exec のフック 1 行) |
| x86 の .data/.bss | +約 20 バイト |
| 起動 FD・MINIMAL | 触れない見込み。上限の実数は読んでいない。PM が `make all` の fd144 の容量の検査と MEM_KERNEL_IMAGE_MAX の余りで確かめる |
| PowerPC の像 | 入口 + owner の分 (約 0.5KB)。前景は入らない |

- build_compare は各段で**直前の版**と**計画の基準 a76398a35** の両方に掛け、関数差を実測で申告する (0 を前提にしない)。純移動の段 M9 だけは「関数差 0」が合格条件で、0 でなければ段を分け直す。
- x86 で変えない: 前景の流れ・戻り値・文面・フレームの順、常駐の `sfs: transport=lan nic=82557 peer=… sid=…` と `sfs: exit=N` の行。
- x86 で変わる (申告する): 常駐で、開いた owner でない end が INVAL になる。開いた owner の終了で scope が外れる。

## 4. ファイルごとの変更と段、受入

### 4-1. 段 (1 ファイル = 1 段。どの段の後も両像が今どおり動く)

| 段 | 担当 | ファイル | 中身 | 型・受入 |
|---|---|---|---|---|
| M1 | 本線 | sys/fs/serialfs/serialfs.h | §2-3 の宣言 (足すだけ、旧 scope_begin/end は残す)。cfg が 4 バイト増える → `sfs_persistent_start` と cfg を作る関数の差を申告 | D。host |
| M2a | 本線 | sys/fs/serialfs/serialfs_persistent.c | `sfs_persistent_describe` | D。host |
| M2b | 本線 | 同上 (M2a の後) | `scope_owner`、`_owned` の 2 本、`scope_owner_exit`。旧 2 本は owner=-1 の薄い包みで残す | その場。host (実物の所有者) |
| M3 | 本線 | sys/platform/pc98/kernel_boot_memory.inc | `boot_sfs_describe` と cfg の `.describe` | その場。host |
| M4 | 本線 | sys/fs/serialfs/serialfs_session.c | 常駐の枝を §2-3 の形に (SfsLan の読みを外す、owner を渡す)、`serialfs_session_owner_exit` | その場。**NP21/W ①**: 常駐 (`SFS_LAN=1`) の `sfs run` を rshell_serial.py の lan_result で通す |
| M5 | 本線 | sys/kern/exec/exec_finish.inc | `exec_host_owner_exit(id)` (exec_finish.inc) の並びに回収を 1 本。USERSHELL の無い像での解決は exec_host の型を写す (exec_host の作りは未読、着手時に確かめる) | その場。host + NP21/W ① に同乗 |
| M6 | 本線 | sys/fs/serialfs/serialfs_session.c | 前景を `sfs_fg_begin/end` に包み、表 `sfs_session_fg` と `set_fg` を足す。`g_fg` の初期値は同じファイルの表 (旧経路のまま動く) | その場。host |
| M7 | 本線 | sys/platform/pc98/boot.c | `serialfs_init()` の後に `serialfs_session_set_fg(&sfs_session_fg)` (この時点では同じ値の上書き) | その場。host |
| M8 | 本線 | sys/fs/serialfs/serialfs_session.c | `g_fg` の初期値を NULL に (切替。登録だけが経路になる) | その場。**NP21/W ②**: UART の `sfs run`、変異は A2 |
| M9 | 本線 | sys/fs/serialfs/sfs_session_fg.c (新設) | 前景の本体と `sfs_lan_peer_mac` を SRC_MOVE §3 の手順で純移動。common.mk (共有の登録ファイル、持ち主から受け取る) で入口を C_COMMON_USERSHELL へ、fg を PC98_PENDING へ。order.mk・試験の covers・変異の目印 (tools/tests/test_serialfs.py の SESSION_SRCS・SESSION_MUTATIONS を全件洗う)・test_src_layout・test_dep_dir_a_l06・docs/design の追従 | C。関数差 0 |
| M10 | 本線 | sys/fs/serialfs/serialfs_persistent.c | 旧 scope_begin/end の包みを外す (試験の呼び元を `_owned` へ) | 片付け。host |
| P0 | PowerPC | sys/platform/powermac/hostfs.h | `pm_hfs_describe` の宣言 | M1 の後 |
| P1 | PowerPC | sys/platform/powermac/hostfs_link_peer.c | 定義 (LINK の像だけ)。r2 の P2 (hostfs_peer.c) は追記で削除 | P0 の後 |
| P3 | PowerPC | sys/platform/powermac/hostfs_boot.c | cfg に `.describe = pm_hfs_describe` (`#if KBOOT_LINK` の中) | P1 の後 |
| P5 | PowerPC | sdk/kapi_target_ppc.py + 生成物 + tools/ppc/test_kapi_binding.py の EXPECTED_ABSENT_SLOTS | 'serialfs' の組を公開、欠落の期待から `sfs_begin`・`sfs_end` を外す。map と nm -l で wrap → platform_serialfs_session_* → serialfs_session_* のパス一致 | M4・M5・M9・P3 の後。最後 |
| P6 | PowerPC (文書) | docs/decisions/ppc/PPC_SHELL_W8_DESIGN.md の PPC_SHELL_W8_DESIGN — PowerPC の到達点 S-D、docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md の 5. 受入と変異 | W8 の「不在指定は維持」を 6b の公開で更新。計画の 6b 受入 の観測を「link の結果 RSH OUT の `sfs: exit=N`」、変異を「exit の行を落とす」に書き直す | P5 と同じ取り込み |

- 並列: 本線の M1〜M10 は直列 (同じ 3 ファイルを行き来する)。P0〜P3 は M1 の後なら M2〜M9 と並列可。NP21/W は ①・② の 2 回、直列。
- 層の検査: M9 の後、入口は serialfs.h・kprintf.h・appslot.h (res_owner_get)・os32_kapi_shared.h だけを読む。両機種で違反 0 を記録。

### 4-2. 受入 → 試験と変異

| # | 受入の項目 | 試験 | 変異 (1 行) |
|---|---|---|---|
| A1 | x86 前景: begin → 読取 → end、対向が EXIT フレームで exit code を得る | host: serialfs_session_host.c (既存)。NP21/W ② | fg の `SFS_T_EXIT` の送信を外す |
| A2 | x86 前景: 製品の登録を通って今と同じ戻り値 | NP21/W ② のゲスト (製品の boot.c を通る。host の試験は登録を通らないので割り当てない) | boot.c の set_fg を外した版をゲストで 1 回回し NOSYS で RED (受入の時に手で、CI の変異の枠には入れない) |
| A3 | 常駐: begin は scope を取るだけで transport を初期化しない | host: **実物の** serialfs_persistent.c + 入口を組む (hostsvc_open_host.c の型を広げる)。PC-98 の模擬 (SfsLan) と PowerMac の link の模擬 (SFS CALL、別の型の ctx) の 2 通りで同じ列 | 常駐の枝で `sfs_client_init` を呼び直す → init の数で RED |
| A4 | 常駐: 行が後端の説明どおり。PC-98 は `nic=82557 peer=<実の peer>` で今と同一 | host: describe あり / NULL / peer 未観測。PC-98 の行は rshell_serial.py の正規表現に掛ける。NP21/W ① | describe の NULL の判定を飛ばす / PC-98 の nic の文字列を変える |
| A5 | 常駐: end が `sfs: exit=N` を出し、BYE も unmount もしない | host (A3 の組)。NP21/W ①。PowerMac のゲスト (mac99 7450・7455、LINK の像): LAN の rshell で `sfs run` を投げ、**link の結果 RSH OUT の本文**の `sfs: transport=lan nic=link … sid=N` と `sfs: exit=7` を照合 (診断 LOG・SFS_T_EXIT は判定に使わない)。drop 非 0・EOT 欠落は取得失敗として別に数える | end の console_printf を外す / exit_code を 0 に固定 / console_printf を kprintf に戻す (PowerMac の RSH OUT から行が消えて RED、x86 は変わらない) |
| A6 | 入れ子 = BUSY、begin 無しの end = INVAL、別 owner の end = INVAL、fg も常駐も無い = NOSYS | host (A3 の組、実物の BUSY の判定に届く) | scope の BUSY の判定を外す / owner の照合を外す |
| A7 | 切断・再開 | host (A3 の組 + LINK は hostfs_link_io.c を模擬の platform_host_* で組む): (1) `sfs_persistent_stale()` か対向の世代の変更で再 HELLO が要る状態にする → 対向なしで begin = HOSTDOWN、100 tick 以内に戻る (2) LINK は期限切れで poll が handle を閉じ pending が下りる (close の数) (3) 対向を戻す → begin = 0、新しい sid で読取 (4) 世代が変わった回は旧 FD が STALE、変わらない回 (idle だけ) は旧 FD が生きる。ゲスト (PowerMac): host_agent (SFS CALL の backend) を世代を変えて起こし直す。lanfs_host 相手の生の経路は PowerMac の受入に使わない | `sfs_ensure_session` の期限の判定を外す / poll の期限切れの `finish` を外す (handle の漏れで RED) |
| A8 | PowerMac: begin〜読取〜end を 3 回、初期化は起動時の 1 回 | host: 実物の hostfs_boot.c + 実物の所有者 + 模擬の SfsIo で `pm_hfs_set_clock`・start・mount の数 = 1。ゲスト: `sfs run` を 3 回、/host の既知のファイルを照合 | begin 側で `sfs_persistent_start` (または set_clock) を呼び直す → 数で RED。公開表から組を外す → NOSYS |
| A9 | PowerPC の像に前景が無い・同名の新規定義が無い | map / nm -l の記録 (sfs_session_fg.o・e82557_*・serial_gate_* が無い) | — |
| A10 | scope の回収 | host (A3 の組 + exec のフックを直に呼ぶ): 開いた owner の終了 → 次の begin が 0、mount・sid・init の数は変わらない。子 (別 owner) の終了 → scope は残り、開いた owner の end が 0。ゲスト (両機種): scope を開いたまま終わる小さい試験アプリの後で `sfs run` が通る | exec のフックを外す (後続が BUSY) / owner_exit が owner を照合しない (子の終了で外れる) |

- 検査は既存の check (test_serialfs.py・hostsvc の試験) に足し、重みの追従も完了条件。

## 5. 未決 (ユーザー判断、0 件)

- U1 (PowerMac の exit code の観測点): PM 決定 (2026-11-20) で確定 — link の結果 (RSH OUT の `sfs: exit=N`) で受ける。r2 の案 (a) 診断 LOG・(b) 常駐の end で SFS_T_EXIT を送る、はどちらも採らない。
- U2: PM 決定で確定 — PowerMac は常駐 (link の SFS CALL) だけ、UART の前景も生の LAN の経路も足さない。
- PM・担当で決める物: 新設ファイルの名前 (M9 の着手時に SRC_MOVE §3)、公開表を LINK の像だけに絞る手段 (P5 の着手時)。
- 後続関門 (6b の外): x86 の生の SFS の LAN の経路の廃止と SFS CALL への載せ替え (LAN_OPERATOR §5)。その時 PC-98 の `boot_sfs_describe` と `nic=82557` の行、前景の SFS_LAN=1 の枝 (sfs_session_fg.c) が片付けの対象になる。

## 反論

- なし (追記の PM の方針にも反論なし。レビューの 9 件とも受け入れた。scope の資格は「shell に限る」でなく「誰でも可 + owner の記録と回収」を採った。x86 の常駐の今の振る舞いを狭めないためで、指摘の修正案の範囲内)。

## 確定 (2026-11-20、sol の第 2 回「条件付きで可」、PM が反映)
- C1 (P1 A5/A8): PowerMac の kboot-link-sh の SHELL_REMOTE_APP は rshell_lan.c の rsh_lan_execute() が行をそのまま execute_command() に渡し、常駐シェル側の `sfs run` の特別処理に入らない。P5 の後に PowerMac の LAN の行全体を受ける入口を段に足し (PowerPC の段 P5b)、子の実行を sfs_begin/end で囲んでから A5/A8 を通す。観測は console_printf → console sink → capture → RSH OUT で成り立つ。
- C2 (P1 P5): LINK だけに公開する。FEATURE_LINK を像ごとの生成の引数に渡し、serialfs の不在の指定を kboot-link-sh でだけ外す (kboot-sh・kboot-shell は NOSYS のまま、同じ ABI のスロット)。P5 の検査は不在の期待の更新に加え、LINK 側の未定義の参照の許可・必須の集合に platform_serialfs_session_{begin,end} を足し、両プロファイルの生成の結果と ABI のヘッダの一致を確かめる (tools/ppc/test_kapi_binding.py)。
- C3 (P1 M5): 回収は FEATURE_USERSHELL に連動して配線する (PC-98・kboot-link-sh・kboot-sh に共通の常駐の所有者が入る、EXEC だけの kboot-shell は no-op、EXEC の無い像には呼び元が無い)。既存の HOOK_reclaim は PC98_PENDING 連動なので、SerialFS 用の条件は独立させる。§4 の入口の include は appslot.h でなく sys/fs/fd_redirect.h (res_owner_get の宣言元)。
- C4 (P2 A7): 期限の後の poll を試験の手順に: OPEN の成功の後に STATUS を止め、復帰の後に poll を 1 回進めて close=1 と pending の解除を確かめる (handle の解放の変異はこれで届く)。
- C5 (M4): kprintf → console_printf は PC-98 では ELF の alias で呼び先は同じ番地だが、.o の再配置の先のシンボルは変わり得る。M4 は書式の %s 化と owner の検査も含むので、出力は lan_result、生成物はリンクの後の逆アセンブルと build_compare で確かめる。既存の kprintf を指す変異の目印は M4 で移す。
- pm_hfs_peer() は観測の前に NULL を返すので、callback で nic=link とゼロの MAC の扱いを明記する (PowerPC の段)。
