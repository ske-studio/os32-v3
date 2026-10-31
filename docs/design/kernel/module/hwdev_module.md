# kernel/module/hwdev_module.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: HWDEV_CORE_API・HWDEV_DESIGN (D1 で移した)
ソース: `kernel/module/hwdev_module.c`

```c
/* facade 内の接続順序 (擬似コード、各失敗は登録前に拒否) */
if (kdebug.state != KDEBUG_RUNNING) return 0; /* ログ・load/start なし */
core = kdebug.ops.hwdev_core_get();
check_core_contract(core); /* size/version/contract_crc を完全一致で照合 */
h = hwdev_load_validate(); /* loader の型/CRC/PAIR 検査、依存 handle 保持 */
fill_hwdev_import_slots(h, core); /* 生成 slot 順・型でコピー、未解決は拒否 */
hwdev_start_initialize(h); /* slot 充填後に開始し call/release_work を核へ登録 */
/* loader 由来の handle/base/size/CRC は部品登録情報に保持する。 */
```

- 表は facade が開始前に充填する (§1)。slot 25〜35 は kernel の関数のアドレスで埋め、既存 kdebug index は宣言の取得元だけに使う。依存 handle だけで symbol を解決したとは扱わない。

- KMOD export の `initialize` は `int (__cdecl *)(const struct hwdev_manifest **manifest, const struct hwdev_ops **ops)`。slot 充填後に一度呼び、静的な RO の2ポインタを返すだけ。登録・確保・I/O・core 呼出しをしない。失敗時は公開しない。manifest/ops の全 span が当該 body の RO 内、call/release_work が text 内であることを facade が checked 範囲検査する。

- 登録した body・ops・expr callback・静的許可表は boot 寿命、依存 KDEBUG handle を保持、stop=0、再初期化/差替え/hot unload なし。登録失敗は module rollback/隔離へ返し、公開前の参照だけを取り消す。

- kernel の小さい HWDEV facade が既存 module API でロード。KDEBUG≠RUNNING ならログなしで飛ばす。RUNNING 時だけ 1 回、HWDEV を依存 handle 付きで開始し、核へ登録。

- ロード順: KMOD の版/型/CRC/PAIR 検査 → HWDEV manifest と loader 実測 identity 照合 → 核の image CRC との対照合 → 登録 → 対象を照合 → claim → 確保/decode/bind → snapshot/arm。登録中は HW アクセス 0。
- kernel PAIR だけでは別 kdebug との混在を検出できない。ビルド順を kernel→kdebug→HWDEV とし、HWDEV へ最終 kdebug image CRC を埋込む。核から部品 CRC を要求する循環は作らない。

- lease は常駐 GF control と同じ boot body に置き、単調増加する claim 世代・所有世代・session を作業領域の世代と照合する。部品入口は kernel facade で依存/自分の loader 世代を毎回照合し、公開成功後の registration ref は boot 中保持する (開始失敗の rollback は保持しない)。
