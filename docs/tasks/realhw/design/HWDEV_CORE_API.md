# HWDEV の核の API・private ABI・台帳 — 参照の表

> 状態: **設計中 (2026-10-22)** — 確定 (G1〜G5 を反映)、実装はユーザーの合図待ち ([TASK_HWDEV.md](../TASK_HWDEV.md))。
> 出所: ~/os32-tmp/run/devmod/DESIGN.final.md を 2026-10-22 に粒度別に分けて写した。

核 (kdebug) と装置部品 (`/boot/hwdev.mod`) の境界の番号・型・定義だけを置く。分け方と理由は [HWDEV_DESIGN.md](HWDEV_DESIGN.md) §4。i386 cdecl、u32 固定、size/version は完全一致で照合する。

## 1. private ABI の構造体

```c
/* 新 private ABI の骨格 (include/hwdev_private.h)。 */
struct hwdev_identity {
    u32 abi, size, kind, build_id, profile_version;
    u32 kernel_pair_crc, core_image_crc, contract_crc;
};
struct hwdev_lease { u32 slot, generation, session; };
struct hwdev_bundle {
    const struct recover_profile *profile; /* targets/rules/guards/exprs/layouts */
    const struct recover_plan *plan;       /* capture/依存/全辺も既存形式 */
    struct recover_plan_buffers buffers;  /* checked 範囲・長さは別途検査 */
};
struct hwdev_ops {
    u32 abi, size;
    int (__cdecl *call)(u32 op, void *copied_req, u32 size,
                       const struct recover_ops *base,
                       const struct recover_transport *transport,
                       const struct recover_profile *s0);
    /* G1: gfinit_call (kdebug/gfinit.h:20-21) と同じ 6 引数。wrapper は名前を変えずに素通しする
       (GF は transport と S0 profile を受けて開始する、gfinit.c:154) */
    void (__cdecl *release_work)(void); /* cleanup 後のみ、HW 操作なし */
};
/* 式は既存 recover_expr の id/argc/kinds/eval。eval は検査済み整数のみ。 */
/* core API は §2 の既存宣言から生成。関数名・型を変えない。 */
struct hwdev_core_api { u32 abi, size, contract_crc; /* 生成済み型付き関数表 */ };
/* kernel/module.h: kdebug_ops 末尾へ追加 (§5) */
const struct hwdev_core_api *(__cdecl *hwdev_core_get)(void);
/* facade 内の接続順序 (擬似コード、各失敗は登録前に拒否) */
if (kdebug.state != KDEBUG_RUNNING) return 0; /* ログ・load/start なし */
core = kdebug.ops.hwdev_core_get();
check_core_contract(core); /* size/version/contract_crc を完全一致で照合 */
h = hwdev_load_validate(); /* loader の型/CRC/PAIR 検査、依存 handle 保持 */
fill_hwdev_import_slots(h, core); /* 生成 slot 順・型でコピー、未解決は拒否 */
hwdev_start_initialize(h); /* slot 充填後に開始し call/release_work を核へ登録 */
/* loader 由来の handle/base/size/CRC は部品登録情報に保持する。 */
```

装置境界は `recover.h:200–219` の recover_ops/opaque を再利用し、HWDEV 固有の口は call/release_work だけ。

## 2. 核の関数の slot (25 関数、既存宣言と同型)

| 生成 stub/slot の対象 | 確認した呼出し元 |
|---|---|
| trial_begin / trial_step / trial_end、recover_owner / recover_owner_configure | gfinit.c:128,242–280 |
| recover_safe_point / recover_plan_enter / recover_plan_leave / recover_plan_access / recover_plan_check | gfinit.c:199–209、gfinit_exec.c:109–123、gfinit_plan.c:232–239 |
| recover_bind_plan / recover_budget_apply / recover_bar_probe / recover_profile_crc | gfinit.c:172–243、gfinit_plan.c:209 |
| recover_access / recover_restore_point / recover_target_address | gfinit_bus.c:75,121–137 |
| recover_window_hal_init / recover_window_access / recover_window_cleanup | gfinit_bus.c:27,98–137 |
| recover_gf_pre / recover_gf_result / recover_gf_screen_safe | gfinit_exec.c:110–121、gfinit_plan.c:242、gfinit.c:72 |
| recover_hal_wait_frame / kpeek_call | gfinit.c:80,95 |

- **G2**: `recover_hal_wait_frame` (recover.h:373・recover_profile.c:119) は gfinit.c:80 の static const 初期化子で関数ポインタとして使う。アドレスは stub のアドレスで、R_386_32 再配置を受ける。
- M1 で GF オブジェクトの未解決 symbol と生成表を照合し、上表以外の既存 kernel imports (確保等) も型付きで供給する (§3)。部品間の symbol 探索は追加しない。

## 3. HWDEV の import の生成 (G3)

- GF は核の関数のほかに kernel の関数 (pgalloc_alloc_phys・pgalloc_free_n_owner・serial_get_setup・pci_cfg_read16/32・kprintf・kmemset/kmemcpy) を使う。
- kdebug ではこれらを生成 stub (`kdebug_import_stubs.S`) が `jmp *kdebug_import_slots+N` で呼ぶので、HWDEV に同じ stub をリンクすると kdebug の slot を指して未解決になる。
- HWDEV は自分の .def から `hwdev_import_stubs.S` と `hwdev_import_slots` を生成し、§2 の核の関数と kernel の関数を 1 つの表に並べる。`kdebug_import_stubs.o` は HWDEV にリンクしない。
- gfinit.c・gfinit_resources.c は `generated/kdebug_import_macros.h` を取り込むが getter マクロは使っていない — 取り込みを HWDEV の生成物に差し替える。
- 表は facade が開始前に充填する (§1)。依存 handle だけで symbol を解決したとは扱わない。

## 4. contract_crc (G4)

- 含めるもの: slot 順・全関数型と、recover.h の共有構造体 (struct recover と推移的に参照する型、recover_ops 等) のメンバ順・型・sizeof/alignof/offsetof・enum 値を i386 ABI で正規化したもの。GF が transport/log_limit/expected_node/cache.bar 等を直接扱うため、配置差は開始前に拒否する。
- **計算の方式 (確定)**: 両方の像に、(1) 生成した `STATIC_ASSERT(sizeof/offsetof/alignof)` の表 (共有構造体の全メンバ) と、(2) 前処理後の recover.h・gfinit*.h を空白とコメントを除いて正規化したテキストの CRC を埋め込み、読み込み時にその 2 つを照合する。型を自前で解析する生成器は作らない。
- CRC は破損・取り違えの検出で、権限の根拠ではない。profile CRC・plan hash・部品 CRC・核 CRC の役割分担は [HWDEV_DESIGN.md](HWDEV_DESIGN.md) §4。

## 5. 部品の台帳の 11 枠目 (PM 決定 2026-10-22)

```python
# tools/module_schema.py: 既存 ID/slot は維持。11 番を一つ追加。
MODULE_RECORD_CAP = 11  # 10→11、生成 C・台帳試験も更新
# COMPONENTS 末尾へ (既存 schema の dict 形式)
dict(name='HWDEV', path='/boot/hwdev.mod', dependencies=(1,), required=False,
     module_id=11, phase=CONSTANTS['PHASE_POST_ROOT'],
     required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
     stop_allowed=0, ops_version=1, ops_slots=1,
     image_max=196608, mem_max=196608, load_span_max=262144,
     reloc_max=16384, import_max=128, export_max=1,
     init_symbol='hwdev_init', imports_symbol='hwdev_import_slots',
     ops_symbol='hwdev_ops', ops_names=('initialize',))
```

- schema:23–24 の注記と、台帳試験 T5c-6 の「11 番目を拒否」を HWDEV の受入へ変える (未定義の 12 番目・容量超過の拒否は維持)。

## 6. KDEBUG の ops の版 3→4・slot 19→20 と波及 (G5)

- KDEBUG_OPS_NAMES の末尾に `hwdev_core_get` を足し、ops_version 3→4、ops_slots 19→20。既存 19 口の順序は維持。
- 揃えるもの: kernel/module.h の構造体、KDEBUG ops の初期化子、生成の MODULE_OPS_SLOTS / KDEBUG_OPS_VERSION。module.c:16 の STATIC_ASSERT を版 4 の 20 口 + version で成立させる。
- **G5 (M2 の依頼文に書く)**: KDEBUG_OPS_VERSION は kernel/module.h:7 の手書きの定義で、GF の identity (gfinit.c:148 → recover.c:130 の CRC) にも入る。新しい口には `kdebug_export_contracts[MODULE_OPS_SLOTS]` (gen_kdebug_imports.py:213) の型契約も 1 行要る。identity の CRC が変わるのは想定どおり。
- kdebug の既存 import は未使用になっても削除・並替え禁止、必要分だけ末尾追加。内部 ops 版 / 契約 CRC も更新。
- 公開の入口 (旧 S7) は既存の kdebug_call のまま。KAPI は生成元だけを変え、[ABI1]〜[ABI3] の版更新・clean → all を守る ([KAPI_SPEC.md](../../../KAPI_SPEC.md) §3-1)。
