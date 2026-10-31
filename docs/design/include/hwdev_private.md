# include/hwdev_private.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: HWDEV_CORE_API・HWDEV_DESIGN (D1 で移した)
ソース: `include/hwdev_private.h`

核 (kdebug) と装置部品 (`/boot/hwdev.mod`) の境界の番号・型・定義だけを置く。
i386 cdecl、u32 固定、size/version は完全一致で照合する。

## 1. private ABI の構造体

```c
/* 新 private ABI の骨格 (include/hwdev_private.h)。 */
struct hwdev_identity {
    u32 abi, size, kind, build_id, profile_version;
    u32 kernel_pair_crc, core_image_crc, contract_crc;
};
struct hwdev_ops {
    u32 abi, size;
    int (__cdecl *call)(u32 op, void *copied_req, u32 size,
                       const struct recover_ops *base,
                       struct recover_transport *transport,
                       const struct recover_profile *s0);
    /* G1: gfinit_call (kdebug/gfinit.h:20-21) と同じ 6 引数。wrapper は名前を変えずに素通しする
       (GF は transport と S0 profile を受けて開始する、gfinit.c:154) */
    void (__cdecl *release_work)(void); /* cleanup 後のみ、HW 操作なし */
};
```

装置境界は `recover.h:200–219` の recover_ops/opaque を再利用し、HWDEV 固有の口は call/release_work だけ。

## 9. 部品の寿命 manifest v1 (M2/M3a の入力契約)

すべて u32、i386 cdecl、自然整列4。

| offset | `hwdev_manifest` のフィールド | 固定値/照合 |
|---:|---|---|
| 0 | `struct hwdev_identity identity` | 32B、abi=1、size=32、kind=1 (GF6200)、profile_version は RECOVER_PROFILE_VERSION |
| 32 | `u32 version, size` | 1、68 |
| 40 | `u32 layout_crc` | §8 の配置表 CRC |
| 44 | `u32 lifetime_flags` | 7 = BOOT_BODY(1) / SYNC_CALLBACKS(2) / NO_DMA(4)、完全一致 |
| 48 | `u32 work_max_pages, dma_max_pages` | 269、0 (下記の試行 RAM 上限、DMA なし) |
| 56 | `u32 ops_abi, ops_size` | 1、16 (`hwdev_ops`: prefix 8B + call/release_work 各4B) |
| 64 | `u32 reserved` | 0 |

- `hwdev_ops.call` の第5引数は実宣言どおり **`struct recover_transport *`** (const なし)。GF が max_polls を更新する。第4・6引数の const は維持。

- 核: recover singleton、profile/plan 検証、snapshot・BAR 測定 cache、期限/abort/REC、共通 98 復帰、window/PTE 排他、装置所有 lease の形と拒否判定、FAILED/cold latch、S0 用の最小 GF 復帰モデル。
- GF 部品: 許可表、固定 plan、ROM decode、GF exec、式/layout/PLL、NV44A bus 変換・局所 cleanup、GF owner と A4 作業領域。

- GF bus は核で許可済みの `{target,dir_substep,space,bar,offset,width}` のみ受ける。核の checked 窓・PCI accessor へ戻して実アクセス直前にも identity/所有 lease を照合。装置 C の生 I/O は禁止し、リンク import と静的検査で確認する。
- 装置固有 expr は有限の純整数関数。PLL は limits を検査して事前展開し、実行時も登録式 / rule で照合。callback の番地はロード済み部品 text 内、表はその RO 領域か登録済み専有作業領域内、長さは checked 算術で検査。
