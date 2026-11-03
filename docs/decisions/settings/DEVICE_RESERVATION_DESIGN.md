# 任意デバイス予約 broker — 改訂の確定設計

> 出所: ~/os32-tmp/run/devres-design/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、確定の直しは PM)。
> 状態: **現行 (2026-11-04)** — 確定設計。段の状態は票が持つ。ユーザー決定 (2026-11-04): §9 の未決 1 は「直接の init を残す」(推奨どおり)。


## r1→r2 の変更
- ID1: CUI 単独 activation を唯一のユーザー未決とし、直接 init 維持を推奨。
- ID2: GF owner 取得 helper を統一し、固定値依存の本体・試験を担当表に追加。
- ID3: 20B region による部品 ABI 変更、16B 案との比較、PM 一括生成・再ビルドを明記。
- ID4: PM 決定として BARLEN 上限を予約幅に採用し、width_basis を追加。
- ID5: RAM のオンライン化抑止・起動停止案を削除し、衝突候補だけ除外。
- ID6: PM 決定として HWDEV consumer 結線・E_OWNER・FAILED 保持を削除。
- ID7: 同じ handle の acquire を冪等化し、正常 CUI 復帰時の release と再入を明記。
- ID8: PM 決定として選択装置だけ BB を確保し、非選択は MMIO 予約のみ。
- ID9: 試験を対応実装の取り込み条件に統合し、C1 も --mutate を必須化。
- ID10: PM 決定として Ra266 の構成別 CUI 起動・GUI 往復を受入と繰延表へ追加。
- ID11: PM 決定として resource 上限32、RAW と検証済みは別 slot。
- ID12: ファイル段ごとの像増分予算を割り当て。

- 作成: 2026-11-04 / GPT-6 ベースの Codex（正確なモデル ID は環境から確認不能）。状態: 実装提案。
- 前提: PC-9821、シングルタスク・協調切替、整数演算、GNU11、kstring、番地定数の三層管理、P2V/V2P。[C1]〜[D3] を維持。
- U6/X4 の承認済み事項を採用。T1/T2 の作り直し、9801 互換、新しい DMA、hot-unplug、exec 再配置は対象外。
- 調査は指定範囲の静的読取りのみ。ビルド・試験・NP21/W・実機操作は未実施。r2 の再確認はレビュー指摘の file:line と該当節のみ。その他の根拠は r1 を継承。

## 1. 実物の確認

| # | 旧票との食い違い / 実物の根拠 | 改訂での扱い |
|---|---|---|
| 1 | 「owner・複数範囲 transaction なし」に対し `mm/pgalloc_resource.inc:52-144` に `ledger_reserve_set` が存在 | T1 完了部分を再利用。旧 `pgalloc_device_reserve` / `sys_device_reserve_core` は現行実体にない |
| 2 | 固定配置 snapshot の追加要求に対し `mm/pgalloc_resource.inc:99-125` は区間台帳と L2 owner を照合済み | 別 broker 台帳を重複作成しない |
| 3 | GUI で初回予約に対し `init/kernel.c:198-207` は `gfx_boot_reserve` → `exec_init` | 起動時予約を維持、残りは装置集合と公開の原子性 |
| 4 | 後発 PEGC BB 未解決に対し `gfx/gfx_core_surface.inc:191-227` は BOOT owner で上端から取得し freeze | BB を MMIO transaction に混ぜず既存 RAM/SURFACE 経路を使う |
| 5 | GUI=0/希望値のみなら予約0に対し `gfx/gfx_core_surface.inc:135-173` は希望候補を起動時予約 | GUI=0 でも実在資源を予約、probe/enable は表示 activation 境界へ（CUI 単独は §9） |
| 6 | PEGC と Xe10 を別 owner で拒否する旧案に対し `gfx/gfx_core_surface.inc:10-13,173` は共有 GFX owner の一括集合 | 固定表示の重なる窓は明示した同一予約グループ、利用権は排他 |
| 7 | PEGC-alt 未使用に対し `gfx/gfx_core_surface.inc:53-55,162` は別名窓も登録 | 現行 identify 条件下の alt を落とさない。無条件な別名推定はしない |
| 8 | 「資源レコード込み失敗不変」に対し `gfx/gfx_core_surface.inc:163-173` と `geforce_platform.c:44-47` は add が先に可視化 | 新 transaction で資源登録・owner 新設まで同時 commit |
| 9 | 初回は PEGC/Xe10 のみに対し `drivers/video/geforce/geforce_platform.c:27-65` に GF の全 BAR 予約・部分写像がある | GF 部品を移植し直さず broker に接続。`gfx_core_surface.inc:159,223-225` の候補排他は実在装置の排他に使わない |
| 10 | 面の隔離が未実装という旧受入に対し `mm/pgalloc.h:187-192,294-310` に SURFACE・世代・lease_count・release がある | T2 の面/lease を使用し、予約範囲を USER に直接公開しない |

- 表の略記 `geforce_platform.c` は `drivers/video/geforce/geforce_platform.c`、`gfx_core_surface.inc` は `gfx/` 配下。
- `drivers/pci/pci.c:8,201-207` は BAR を読むだけ。`platform/pc98/pci_resources.c:17-24` の RAW は幅未確定で権限ではない。
- `tools/tests/test_device_reservation.py:14-25,160-247` に第二 span、exact retry、部分一致、永久保持等の試験が既存。合格を今回確認した意味ではない。
- 指定 `docs/decisions/realhw/HWDEV_CORE_API.md` は存在せず、同名ファイルも検索で見つからなかった。
- 現行 `docs/decisions/MEMMAP_V3.md` に D33 本文・§6 T1 はない。アーカイブは未参照。U6/X4 は票冒頭のみを根拠とする。
- HWDEV の現行制約は `DEVICE_STATES.md:14,29-32`：表示所有中・未分類は claim 拒否、GF の BM 禁止は維持。

## 2. 部品境界と正典

| 部品 | 持つ状態 / 責務 | 持たないもの |
|---|---|---|
| pgalloc / T1 台帳 | DEVICE owner、資源、正規化区間、L1/L2、会計、原子的予約 | PCI I/O、表示の選択、probe |
| PCI | BDF/ID/revision/BAR の読取りと資源の検証用照合 | 表示 owner、汎用の BAR sizing 書込み |
| pc98 資源 adapter | 固定窓の由来、PCI 資源の検証済み幅との結合 | 生 BAR からの幅推測 |
| kernel broker | boot 世代、予約 handle、表示 gate、準備/有効状態、直列調停 | module 内の関数ポインタの永久保存 |
| gfx | handle 経由の表示 gate、既存 SURFACE/lease、装置別停止 | MMIO 永久 owner の回収 |
| GeForce 部品 | 現行 identify/prepare/stop、必要 map 範囲、描画状態 | GFX 全装置の予約 owner、予約の解除 |

内部ヘッダ `include/device_reservation.h` の契約案（公開 SDK/KAPI には追加しない）:
```c
/* PFN は半開。4GiB end は 0x100000。物理 byte の加算は u64 で検査。 */
typedef u32 devres_handle; /* boot 中に再利用しない slot+1、0 は無効 */
struct devres_request {
    u32 device_key, group_key, count;
    const struct ledger_resource *resources; /* 検証済み・map は全て空 */
    const u8 *width_basis; /* 各資源の EXACT / BARLEN_UPPER。adapter が設定 */
};
struct devres_view {
    u32 owner, count, boot_gen, state;
    u32 rid[DEVRES_MAX_DEVICE_RES];
};
int devres_boot_commit(const struct devres_request *req, devres_handle *out);
int devres_view(devres_handle h, struct devres_view *out);
int devres_acquire(devres_handle h); /* 表示 gate のみ。同一 h は冪等 */
int devres_release(devres_handle h); /* 表示利用権のみ */
int devres_revalidate(devres_handle h); /* 表示 activation 境界の読取り */
int devres_owner_for_resource(u32 rid, u32 *out); /* 唯一の DEVICE owner 取得 helper */
```

- `DEVRES_MAX_DEVICE_RES=6`（PCI type 0 の BAR 数）、固定表示グループは現行4窓。ROM・I/O BAR の権限を足さない。
- `device_key` は bus種別+BDF、固定装置は定数 ID。`group_key` は kernel が決定し caller の任意共有指定を認めない。
- PEGC/Xe10 の既存重複だけを固定表示グループにまとめ、集合を起動時に一度作る。PCI の Trident/Cirrus/GF は各 BDF の DEVICE owner。
- 固定窓と PCI BAR が同じ装置を指す場合は adapter が既知の対応を明示し同一 group に統合。不明な重複は E_CONFLICT。
- owner helper は C1 が commit 済み handle の rid→owner を引き、未登録は E_STATE・出力不変。GF context に rid を保持し、WC/native/platform/kselftest は同 helper で比較する（定数 GFX や単なる DEVICE 型一致で代用しない）。部品からは PM 管理の内部 import を経由。
- 全装置の表示出力/低位固定表示資源は単一の利用 gate。内蔵装置を取り外した扱いにせず、非選択装置の予約も残す。

## 3. 残りの transaction：登録・予約・公開

```c
/* mm/pgalloc.h に追加。戻り値は DEVRES_*、既存 1/0 API の ABI は保つ。 */
int ledger_device_commit(u32 group_key,
    const struct ledger_resource *records, u32 n,
    u32 *owner_out, u32 *rids_out);
/* PCI は保存 g_pci ではなく現在の config を読み直す。0=一致。 */
int pci_resource_match(const struct ledger_resource *saved);
```

1. 起動専用 gate は ONLINE、master CR3、live AS=0、exec 初期化前、通常文脈、非再入を要求。`paging_boot_context()` だけに依存しない。
2. request を private scratch にコピー。kernel adapter 由来の evidence と boot 世代、BDF/ID/revision/BAR種別・raw値・幅を検査。
3. byte 範囲を外向きにページ丸めし overflow を検査。予約は decode 全体、map は確認した必要範囲だけ。RAW/PROBE_UNVERIFIED は拒否。
4. IRQ を保存禁止し、owner 空き・資源空き・区間空き・handle 空きを全て事前確認。**PM 決定（ID11）**: resource 上限32、診断 RAW と検証済み資源は別 slot、置換しない。
5. `ledger_reserve_set` の検証/正規化部分を内部 helper に抽出し、未公開の資源 view に対して実行。固定区間/L2との衝突・完全一致条件は維持。
6. 全資源を常に decode 全域の span に変換。既存核の「decode 内の部分 span も可」を broker の予約要求には許さない。
7. 失敗し得る操作が終わってから owner、資源 slot、区間、eligible、会計を commit。broker は外側でIRQ排他を保持して呼び、事前確保した未公開slotを成功後に失敗なく公開、最後に out を書く。
8. 失敗時は資源・owner・bitmap・会計・handle・出力・IF 不変。既存診断カウンタだけは増加可。I/O/map は0。
9. 同一 boot/device/group・同一資源集合の再要求は既存 rid を返す。並び替えは正規化、部分一致・増設・BAR変更は拒否。
10. `ledger_resource_add` →失敗時に資源を消す疑似 rollback は使わない。既存 add は RAW 診断等に残し本経路から外す。

- `ledger_device_commit` は `mm/pgalloc_resource.inc` 内で既存静的状態を操作し、sys.c に旧 broker を復活させない。
- RAW は現行の起動収集順のまま保持。RAW約8+GF3+固定4+Trident3=18を32枠に収め、超過は E_CAPACITY。診断 RAW を予約権限に数えない。
- 内蔵 PCI GPU は選択に関係なく読取り識別。検証済み decode は全て予約する。未知幅は予約済みと表示せず E_UNVERIFIED。
- **PM 決定（ID4）**: Trident は `BARLEN_DESIGN.md:22-25` の保守的上限を予約幅に使う。1023:9660/rev=d3/endpoint/BAR0〜2/attr=0 と BIOS 非重複配置・再割当てなしの条件を adapter が確認。
- 基準の BAR0/1/2 は各4MiB/64KiB/8MiB。根拠は `kdebug/gfinit_resources.c:24-26`、`gfinit_resources/bars.inc:26-28`。正確な SIZING と呼ばず width_basis=BARLEN_UPPER を handle と再要求照合用 scratch に保持する。
- 実行中の sizing（全1書込み）は追加しない。既存 RAM 割当との重なりは E_CONFLICT で当該候補を外すだけ。RAM オンライン化の巻戻し・起動停止は追加しない。

## 4. owner・寿命・表示利用の解除

| 資源 | owner / 寿命 | 解除の順 |
|---|---|---|
| PCI/固定 decode | DEVICE / boot 全体 | 解放 API なし。map失敗・probe失敗・MODULE回収でも残す |
| PEGC/GF RAM CLIENT | 現行 BOOT→既存移譲先 / SURFACE寿命 | 公開停止→lease終了→surface_release→許可された RAM 回収 |
| MODULE のコード/作業 RAM | MODULE / load〜停止 | callback解除→実行終了確認→既存 reclaim |
| 表示利用権 | broker の active handle / 操作期間 | 新規lease停止→既存lease終了→装置停止→release |

- **PM 決定（ID6）**: HWDEV の acquire/release 結線は外す。既存 `hwdev_claim`（`hwdev/owner.h:18-22`）と `hwdev_owner_state=DISPLAY` の拒否を維持。E_OWNER・broker の FAILED 保持は追加しない。
- broker の状態は RESERVED/MAPPED/READY/ACTIVE、表示 gate は空または一つの handle。同じ h の acquire は成功無操作、別 h は E_BUSY。正常 CUI 復帰で release し、次回 GUI で再取得する。
- release は lease 終了・既存停止処理の完了後のみ。未完了なら既存停止経路に従い gate を保持する。MMIO/写像と再利用 BB は保持し、二重 release は E_STATE。
```c
acquire(h): if (active == h) return 0; if (active) return E_BUSY; active = h; return 0;
activate(h): if (state[h] == ACTIVE) return 0; acquire(h); /* 失敗なら戻る */
             revalidate_map_probe_enable(h); /* 失敗時は既存停止後 release、面は非公開 */
             publish_ready(h); state[h] = ACTIVE;
to_cui(h):   end_leases_and_stop(h); /* 完了時だけ */ state[h] = READY; release(h);
```

## 5. 起動・exec/BB・mapping・公開順

```c
/* 新 gfx/device_activation.c の内部入口。boot で用意した handle のみ。 */
int gfx_device_activate(devres_handle h);
/* GF adapter は既存 reserve callback の内側を置換する。 */
/* gf_production_identify -> devres_boot_commit -> view.owner/rid を保存 */
/* identify callback -> devres_revalidate; stop callback -> 既存 stop */
```

- 起動: RAM地図確定→PCI読取り→検証済み資源 commit→必要PT/写像準備→BB/SURFACE記述→arena freeze→exec_init。
- `paging_init/pgalloc_init` は PCI 取込み（`platform/pc98/pci_resources.c:82`）より先。予約時に使用中 RAM と衝突した候補は E_CONFLICT で外す。
- GUI=0 も識別・予約・選択装置の BB 準備までは同じ。CUI 起動だけでは probe/enable しない。希望値は BB と activation の選択に使い、MMIO 収集から非選択装置を外さない。
- `gfx_prepare_backend` の起動時 PEGC probe（`gfx/gfx_core.c:324-326`、`init/kernel.c:250`）は表示 activation gate へ移す。起動は表示同期を変更しない read-only prepare のみ。
- GUI=1 と CUI→GUI は同一 master/live AS=0・exec不在境界。CUI 描画アプリの既存直接 probe/init（`gfx/gfx_core.c:308`）は §9 の唯一の未決。推奨案では起動済み予約/map/BB を使い既存 gfx_init 境界で同じ表示 gate に入り、exec不在条件は GUI 経路にだけ課す。
- exec arena 全域を禁止しない。実際の FIXED/STAGING/BB/live allocation を T1 で拒否。BB は既存 top-down、未公開失敗時だけ RAM を返す。
- map は supervisor RW/UC を基本とし、GF の既存 WC 条件・同期は `gf_wc.c` に任せる。broker に新 WC 制御を作らない。
- map対象全体の既存PTE属性を事前検査。RAM identity alias は既存 paging の cache/TLB 同期経路でのみ変更し、単なるPTE置換で済ませない。
- 複数 map 後半失敗は成功済み supervisor map を保持し、実際に成功した範囲だけ `ledger_resource_set_map` に記録。候補の面は非公開、完了済み段階までの状態を記録して E_PREPARE を返す。
- 既存PTEを無条件に0へ戻さない。永久予約のため安全な部分写像は boot 終了まで保持可。BAR2 の予約済みPT/非present運用も維持。
- 表示 activation gate（CUI は §9）: acquire→ID/BAR/世代を現在値で再照合→必要map完了確認→probe→enable→READY面の公開→gshell AS作成。
- 再照合で差があれば書込み0、E_CHANGED、予約差替え0。装置を探し直して別BDFへ結び替えない。
- 起動時の SURFACE 記述と USER 貸出可能状態を区別。T2 の unready/generation を用いて準備途中の面を query/lease させない。
- CLIENT のみ既存 USER lease、DISPLAY は通常のアプリに非公開。既存の明示的表示権限経路を無条件な USER 化で置換しない。
- 再initは同じ handle/rid/BB を使用し、lease中の共有PTを supervisor 属性で塗り直さない。AS側map失敗なら既存launch abortへ。

## 6. 誤りコードと大きさ

| 内部戻り値（成功0、負数は以下の順に -1〜-9） | 意味 / 呼出側 |
|---|---|
| E_INVAL / E_CTX | 範囲・型・引数不正 / 起動・GUI境界違反。出力不変 |
| E_UNVERIFIED / E_CHANGED | 幅の証拠不足 / 装置再照合不一致。probe/enable禁止 |
| E_CAPACITY / E_CONFLICT | 台帳・owner・handle満杯 / 固定配置・他owner・RAM使用中衝突 |
| E_BUSY | 別表示 handle が利用中・lease残存。状態を維持 |
| E_STATE / E_PREPARE | 部分一致・順序違反 / map・BB・probe・enable失敗。後者は段名を診断 |

- 下位既存1/0 APIは維持。新helperは失敗分岐でコードを確定し、曖昧な0を事後の再走査で分類しない。
- **PM 決定（ID11）**: owner64維持、resource16→32、region48→64、handle8、1装置6資源。3 GPU×6 BAR+固定4=22資源を収容。
- resource32に合わせ `ledger_region.res_mask` を u16→u32、commitのu16 castも除去、シフトを32bit unsignedに統一。31番を試験、32以上は拒否。
- region は20B案を採用。span_set を他フィールドへビット詰めして16Bを保つ案は追加の符号化・全アクセス修正が必要で、res_mask拡幅による部品契約変更も残るため不採用。20B案の追加RAMは64件で256B。
- 表を無制限に増やさないが上限を物理限界と称しない。超過は原子的 E_CAPACITY、実構成が超える時に予算込みで拡張する。

| 項目 | 増分の設計予算（実測ではない） |
|---|---|
| kernel text/rodata | broker/PCI照合/結線で約8〜16KiB、既存コード移設は二重計上しない。圧縮像への増分は未測定 |
| 台帳 RAM | 資源 +512B。regionを16→20Bに変更して64×20−48×16=512B（現行 `mm/pgalloc.c:36` は16B、実装時sizeofを固定） |
| broker常駐 | handle8×64B=512B、width_basis は8×6=48Bをhandle予算内に含む。資源/正規化scratch・計画領域は約2〜4KiB。増分総計は約4〜6KiB |
| PT RAM | 実際にmapする異なる4MiB帯の数×4KiB。decode全域をmapしない。新broker自体は常駐PTを増やさない |
| BB RAM | **PM 決定（ID8）**: 選択装置の分だけ（PEGC または GF client 300KiB）、PC98 fallbackは現行サイズ。非選択は MMIO 予約だけ |
| 永久MMIOの番地帯 | GF定数は16+256+16=288MiB（`hwdev/gf/production.h:8-10`）。描画mapはBAR0と300KiB等の既存必要部のみ |
| 固定表示の番地帯 | PEGC512KiB、alt512KiB、Xe10 bank32KiBとlinear4MiB。bankはPEGC内なのでunionは最大5MiB |
| 共存時 | 上記unionにTrident/PCI Cirrusの検証済みdecodeを加算。Trident基準上限の合計は12MiB+64KiB、他窓とのunionで計算する。通信帯域の消費ではなく番地占有 |

## 7. ファイル別変更・実装段の所有

各行を「1ファイル＝1段＝1実装者」の発注単位とする。日数は各0.5〜1日、複数行を同時担当しても他段のファイルは触らない。
各段の持ち主は段IDと同名の実装者。試験ファイルも別担当とし、依存実装と同時に取り込む。予算はtext/rodataの上限KiB（0はhost/文書/配置のみ）。
| 段/持ち主 | 新規/変更ファイル | 機能・概算行数 | 依存 / 並列 | 像KiB |
|---|---|---|---|---|
| A1 | 新 `include/device_reservation.h` | 契約 95 | 最初 | 0 |
| A2 | 変更 `mm/pgalloc.h` | API・容量・mask 45 | A1後 | 0 |
| A3 | 変更 `mm/pgalloc_resource.inc` | private view・commit 220 | A2後、B1並列、F1と取込 | 3 |
| A4 | 変更 `mm/pgalloc.c` | sizeof 15 | A2後 | 0 |
| B1 | 新 `drivers/pci/pci_resource_match.c` | 現在config照合 100 | A1後、A3並列 | 1.5 |
| B2 | 変更 `drivers/pci/pci.h` | 宣言 8 | B1統合前 | 0 |
| B3 | 変更 `platform/pc98/pci_resources.c` | BARLEN・width_basis 120 | A3/B1後 | 1.5 |
| C1 | 新 `kernel/device_reservation.c` | gate・handle・owner helper 260 | A3/B1後、F2のbroker試験と取込 | 3 |
| C2 | 変更 `gfx/gfx_core_surface.inc` | 固定group・選択BB 130 | C1後、D1並列、G1と取込 | 1.5 |
| C3 | 新 `gfx/device_activation.c` | 表示gate・再入 150 | C1後、F2のactivation試験と取込 | 2 |
| C4 | 変更 `gfx/gfx_core.c` | prepare/直接init結線 65 | C2/C3後、ID1決定を反映 | 0.5 |
| D1 | 変更 `drivers/video/geforce/geforce_platform.c` | broker・rid/helper 100 | C1後、C2並列 | 0.75 |
| D2 | 変更 `drivers/video/geforce/geforce_module.c` | publish/停止 45 | D1/C3後、F2のGF試験と取込 | 0.5 |
| D3 | 変更 `drivers/video/geforce/gf_wc.c` | :157 owner helper 15 | D1後、G2と取込 | 0.25 |
| D4 | 変更 `hwdev/gf/native.c` | :19 owner helperのみ 15 | C1後、G4と取込、claim結線なし | 0.25 |
| D5 | 変更 `kdebug/kselftest/gfx_ledger.inc` | :21 rid/helper 20 | C1後 | 0.25 |
| E1 | 変更 `init/kernel.c` | 起動・gate 35 | 部品完成後、直列 | 0.5 |
| F1 | 変更 `tools/tests/test_device_reservation.py` | 原子性・容量・変異 180 | A3と一括 | 0 |
| F2 | 新 `tools/tests/test_device_activation.py` | C1/C3/D2のtrace・変異 260 | 同一担当が依存段ごと追記 | 0 |
| G1 | 変更 `tools/tests/gfx_boot_host.c` | :194,568 owner/選択BB 35 | C2と一括 | 0 |
| G2 | 変更 `tools/tests/gf_wc_host.c` | :187 BDF別owner・他owner拒否の変異 25 | D3と一括 | 0 |
| G3 | 変更 `tools/tests/lease_host.c` | :285 owner fixture 15 | A3と一括 | 0 |
| G4 | 変更 `tools/tests/gf_production_host.c` | :243 owner fixture/native経路・誤rid拒否 30 | D1/D4と一括 | 0 |
| G5 | 変更 `tools/tests/access_walk_host.c` | :317 owner fixture 20 | A3と一括 | 0 |
| P1/PM | 変更 `docs/tasks/DEFERRED_TESTS.md` | Ra266受入行 5 | E1統合時 | 0 |

- 上限合計15.5KiB（kernel+部品+kdebugの合計で保守的に計上）、§6の8〜16KiB内。共有登録の増分もこの枠内、圧縮像・常駐RAMは統合時実測。
- 共有登録 `build/*.mk`、module imports/context、試験登録、文書入口は PM 統合ブランチのみが所有。各実装者は追加行案を提出、PM がファイルごとに段を割り当てる（PARALLEL_BRANCHES §2）。
- KAPI/SDK ABI は不変だが **部品 ABI は変更**。`hwdev/generated/hwdev_contract.h:2731,2745-2748` の16B/offset12/2B契約を20B/u32へ更新。`tools/hwdev_layout.json:1452` と `kdebug/generated/kdebug_imports.json:2688` を含む関連生成物は PM が全変更のmerge後に一回生成する（同§2）。部品とkdebugを同一契約で再ビルドし、旧像と混在させない。

## 8. 受入→試験・変異と検査順

| 受入 | host / ゲスト / 実機の確認 | 変異1件 |
|---|---|---|
| T1 全件原子性 | host: 第二span衝突、owner/resource/region/handle満杯で全state/out/IF比較、map/I/O=0 | 資源slotだけ先commitにして検出 |
| T1 exact retry | host: 同集合並替えで不変、同owner部分一致/追加/他owner一致は拒否 | mine==n条件を除去 |
| T2 範囲と容量 | host: 4GiB端、丸め衝突、管理外、fixed/workspace/BB/live割当、rid31と32 | res_maskをu16へ戻す |
| T2 寿命 | host: map/probe失敗後・module回収後の予約保持、free/markで復活なし | DEVICE回収拒否を除去 |
| T3 順序 | host trace: commit→map→照合→probe→enable→公開→AS、途中失敗の部分map保持 | probeをmap前に移す |
| T3 再照合 | host: BAR/ID/revision/boot世代変更で書込み0、GF callback経由も確認 | 現在configでなく保存g_pciを読む |
| T4 共存 | host: 内蔵2台+GF、非選択も予約、固定重複groupは一回、PCI重複は拒否 | GF選択で内蔵収集をskip |
| T4 GUI境界 | host＋ゲスト: GUI=0の起動だけは予約ありprobe0、GUI往復/単独アプリ（§9決定に追随）/再init、BB/USER/PCD維持 | unready確認を外す |
| T4 表示gate | host: 同一h acquireは冪等、別hは拒否、release→再acquire成功。HWDEV DISPLAY拒否は既存回帰だけ | 正常CUI復帰のreleaseを除去 |
| T5 回帰 | host: 旧票T5群＋T1/T2/SURFACE関連選択検査、ゲスト: CUI/GUI/abort/ネスト/隔離 | 表示面を通常CLIENTとして貸す |
| 実機資源 | host: BARLEN条件・上限とwidth_basis、RAW別slotの18資源構成、GF選択でPEGC BB無し | width_basisをRAWへ変更 |
| 実機表示 | **PM 決定（ID10）**: Ra266+液晶でPEGC/Cirrus/GF各選択のCUI cold boot・GUI往復。カメラで同期/桁ズレ、シリアルでprobe順を確認 | hostでPEGC probeを起動へ戻す |

- 実装各段: 対象像ビルド→`make -k check-changed BASE=<基点> MUTATE=0`→変更試験`--mutate`→`tools/checks/run.py --lint`。A1/A2は利用側hostコンパイルで契約を確認。
- A/B契約確定後に並列段、C/D完了後Eを直列結線。A3+F1、C1+F2初版、C3/D2+F2追記は一括取り込み、各段で対応host試験と --mutate が通るまで取り込まない。PM統合時に全体検査・ABI再ビルド・像予算確認、ゲスト/実機は直列。
- 実機実施は後日。P1で DEFERRED_TESTS に「Ra266+液晶／PEGC・Cirrus・GF選択別／CUI起動・GUI往復／カメラ+シリアル証跡／未実施」を一行登録する。今回リポジトリには書かない。
- 今回は設計書のみ作成し、上記検査は一切実行していない。未実施の実機受入とmissing文書は合格扱いしない。

## 9. ユーザー決定

- **ユーザー決定 (2026-11-04、ID1)**: CUI 単独の描画アプリの activation は、現在の直接 init を残す (推奨どおり)。
- 理由: GUI=0・GFX=cirrus/pegc の既存アプリが PC98 に落ちる退化を防ぐ。CUI 起動だけでは probe/enable せず、描画要求時に既存 gfx_init から表示 gate を呼ぶ。
- 影響: boot 済みの予約/map/選択BB を再利用し、CUI 描画経路でも再照合と公開順を守る。禁止案を選ぶ場合だけ PC98 fallback を受入に明記する。

反論: 0件。

## 確定 (2026-11-04、claude-opus-5-5 の第 2 回、PM が反映)
- N1: §7 の表の C2 の組に `tools/tests/test_gfx_boot.py:220-224` の変異の目印 (`ledger_reserve_set(LEDGER_OWNER_GFX, sp, n)` の行を core.index で探す) の追従を足す。C2 がその行を書き換えるなら、同じ段で目印を新しい行に直す。
- N2: PM 決定 — 固定表示 group (PEGC/Xe10) の owner は今の `LEDGER_OWNER_GFX` (固定 7 番、mm/pgalloc.c:96) のまま。`ledger_device_commit` は新しい owner を作らない。`tools/tests/test_ledger.py:256,258` と `device_reservation_stage_host.c` は変えない。BDF ごとの DEVICE owner は GF など PCI の増設装置だけ。
- N3: PM 決定 — Trident の上限の計算 (`kdebug/gfinit_resources/bounds.inc:72-73`、隣の BAR の番地から実行時に縮める) は B3 の中で共有のファイル (`drivers/pci/pci_barlen.c`/`.h`、製品のカーネルに入る) へ移し、kdebug はそれを引く。計算の正典は 1 つ。B3 の触るファイルに bounds.inc と kdebug のビルドの入力を足す。
- N4: `devres_request.width_basis[]` は置かない。`struct ledger_resource` の既存の width_basis の欄を使い、enum に `LEDGER_WB_BARLEN_UPPER` を足して `pgalloc_resource.inc:74` の照合で受け入れる (A2/A3)。
- N5: CUI 単独アプリが表示 gate を acquire した後の release は、`to_cui` に加えて exec の後始末 (正常終了・abort・longjmp の共通の出口) に 1 行置く。同じ h なら冪等。
- ID11: §6 の「追加 RAM 256B」は「16B のまま 64 件にした場合との差」、表の「64×20−48×16=512B」は「今の 48×16B との差」。
