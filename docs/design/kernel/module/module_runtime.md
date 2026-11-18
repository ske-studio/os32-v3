# kernel/module/module_runtime.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・HWDEV_DESIGN (D1 で移した)
ソース: `kernel/module/module_runtime.c`

- **R7 モジュールの寿命 — 読み込み失敗の巻き戻しと、取り外しの契約。** (Codex 往復 6 P3)
  - **読み込み**: ローダが池から code/data/bss を owner=モジュール名で取り、再配置し、init を呼ぶ。init が池から取るもの (MEMSYS5・代替スタック・KCG ROM のキャッシュ・ドライバのバッファ) も**同じ owner** で取る。
  - **読み込み失敗** (init が非 0、例: MEMSYS5 確保後に代替スタックの確保が失敗): ローダは **owner=モジュール名 の台帳上のページを全部回収**する — 部分初期化の巻き戻しを init の逆順コードに頼らず owner 単位の一括回収に任せる。ただし**台帳の外の状態 (IRQ 登録・DMA の起動・装置レジスタ) は init 自身が失敗を返す前に戻す**。

**恒等写像に置くモジュール** (code/data/bss、MEMSYS5、代替スタック)
池の恒等ページ = 仮想も物理も同じ番地なので、まとまりは物理連続でなければならない
取れなければ読み込みを拒否する (アプリのページを動かさない)。

R7 (owner 単位の回収、装置状態は init が戻す) はそのまま。

| 項 | 契約 |
|---|---|
| 信頼の範囲 | 署名なしで保証できるのは構造の妥当性だけで、**任意の CPL=0 コードの安全性は保証しない**。**信頼する配布物 (起動一覧と対の `/boot` 部品) からだけ読む**契約を明示し、チェックサムを実行権限の証明として扱わない |
| IRQ 登録と初期化状態の結び付け | `irq_register()` は登録直後に PIC のマスクを開く (`irq.c:45`) が、owner や handler の所属範囲は見ない (`irq_math.c`)。**IRQ 登録成功 → 残りの init に失敗 → コードを回収**、の順で登録だけ残ると次の割り込みが解放済みコードへ飛ぶ — 取り外しを実装しなくても**起動時の失敗だけで到達する**。既存 PCI 契約の **STARTING → RUNNING** (`pci_bind.h`) をモジュールにも適用し、登録時点で handler と引数を使用可能にする。**通常利用者への export 公開は init 成功後**に限る。init 失敗時はローダが (init 自身の戻しに加えて) owner=モジュール名の IRQ / tick callback の登録が残っていないことを確認してから回収へ進む (R7) |
| 「戻せた失敗」と「停止を証明できない失敗」 | R7 の一括回収へ進めるのは **IRQ・tick・登録済み callback・DMA からの参照が切れたとき**だけ。装置の停止が確認できない失敗は、既存の `PCI_PROBE_QUARANTINE` (`pci_bind.h`) と「DMA 停止を証明できないリングは返さない」(`pcm_cs4231.c`) と同じく、**必要資源を隔離保持するか起動を止める**。`irq_unregister()` だけでは tick 経由のアクセスは止まらない (`pcm_cs4231.c`)。省略可能モジュールの「未搭載 stub で継続」(R7) は**戻せた失敗にだけ**使う |
| 実行可能として公開する境界 | コピー・BSS 初期化・再配置・import 書込を終え、必要なアーキテクチャ処理 (命令キャッシュの整合、M0 §6 の「ロードしたコードへ飛ぶ経路」) を済ませてから init / callback を公開する順序にする。x86 でキャッシュ障害を確認した所見ではなく設計上の順序。ARM の実装は v3 の完了条件に入れない |

| 実物・根拠 (file:line) | 確認したこと |
|---|---|
| `kernel/module/module_runtime.c`、`module_abort()` | 依存 handle 世代、停止・参照数検査、失敗時隔離がある。依存は関数解決そのものではない。装置所有権の仲裁は別途必要 |

## 読み込み・開始・巻き戻しの順序 (`module_load`・`module_start`・`module_abort`)

> 出所: TASK_DBGREF (§3-4) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `kernel/module/module_runtime.c`。像の検査は `kernel/module/module_image.c` (`docs/design/kernel/module/module_image.md`)。

1. `module_load`: 記録の空きを取る → ファイルを開く → 先頭の 144 B (`MODULE_HEADER_SIZE`) を読み、`module_image_header` でヘッダの全欄を検査 (ここまでページを取らない) → 動的な MODULE owner を作り、`load_span` 分のページを連続で取る → 残りを読み切る (短読は SHORT_READ) → `module_image_relocate` で全体を検査し BSS をゼロにして再配置 → import slot に本体の口を位置で書く → export を記録の表に写す。この時点では未公開 (RELOCATED)。
2. `module_start`: 記録の表の写しを候補表として部品の開始関数 (KDEBUG なら `init`) に渡す。戻りが 0 でない、または候補表が記録の表と違えば `module_abort` して INIT_FAILED。成功したら表の手前 (prefix) のページを返し、残るのは body のページだけ。状態を RUNNING にする。
3. `module_abort` (どの段の失敗でも): まず状態を ROLLING_BACK にして未公開にする → 依存する部品が残る・停止を確認できない・IRQ / tick / callback / DMA などの参照が残るなら QUARANTINED (資源を返さない) → そうでなければ owner のページを全部回収し、owner を retire して ABSENT。init を一度も呼ばない失敗では公開表は空のまま、FD は閉じ、ページは全部戻る。

検査: `make check-kdebug-init-host` (`tools/tests/test_kdebug_init.py` のローダの段)。
