# 試験一覧

実失敗は集計の最終診断と、同じ attempt の checks 成果物にある実失敗の構成ログ・evidence/ 以下の証拠を照合する。

登録検査の契約、対象構成、期待値、変異は [検査の登録](../tests/registry.toml) にある。
登録欄の意味と実行結果の読み方は [登録の仕様](design/tools/checks/registry_v2.md) を参照する。

```sh
python3 -B -m tests.infra.ci lint
python3 -B -m tests.infra.ci run --check <id>
python3 -B -m tests.infra.ci run --check <id> --mutate
```

## ゲスト受入

| 名前 | 種類 | 何を守っているか | いつ走る |
|---|---|---|---|
| [`sys/platform/pc98/kdebug/kselftest.c`](../sys/platform/pc98/kdebug/kselftest.c) | 起動時自己試験 (KDEBUG=1 のときだけ。OFF は 0/0) | カーネルが**実際に使う**プリミティブ (`kstring_asm.asm` / `kmalloc.c` / `kprintf`) の境界ケース。外部プログラムの `klibc_test` は newlib を測るので代替にならない。E08 の time・launch・dbv50・resume・irq・con_sink を統合し、人工状態はホストへ移管、con_sink の描画2判定は維持。[現行受入の正典](../tools/guest_acceptance.yaml) の f13-mem-stat は共通下限217、IRQ3 IRR空なら223、fail=0 (統合後ゲスト未実測)。結果は `kselftest_pass` / `kselftest_fail` に残り `kernel.map` 経由で読む | 毎起動 (実機) |
| `link_selftest()` ([`sys/dev/net/ne2000/lgy98.c`](../sys/dev/net/ne2000/lgy98.c)) | 起動時自己試験 | LGY-98 リンク層の HELLO + PING/PONG。**`make kernel-lgy98-link` (LGY98_FLAG_LINKTEST) でビルドしたカーネルでのみ**走る。観測値は `link_*` グローバルから読む | LINKTEST 版の毎起動 |
| [`tools/ops/gui_gate.py`](../tools/ops/gui_gate.py) | ゲスト観測 (判定は人) | GUI のゲート操作列 (`v11` / `v12g1` / `v12g4` / `shot` / `click`)。**自動照合は `wab_relay` / `scrn_ymax` / `fault_generation` の 3 数値だけ**、他は PM が目で見る | 手動 |
| `tools/emu_agent/tasks/regress.txt` | ゲスト観測 (ローカル AI) | 配備後の定型回帰 — kselftest カウンタ / `klibc_test` / `alloc_demo` / `ring3_fault` で落ちてもシェルが生きている / パイプライン 2 本 / screenshot。**モデルは観測を転記するだけで合否は人** | 手動 |
| `tools/emu_agent/tasks/v86_dos.txt` | ゲスト観測 (ローカル AI) | V86 の DOS 起動。`dos5hd.nhd` / master は 2026-09-10 に削除済みで、再作成は FORMAT から 30 分 | 手動 |

受入項目・合否は [ゲスト受入一覧](../tools/guest_acceptance.yaml)、未完了の観測と再開条件は [持越し台帳](tasks/DEFERRED_TESTS.md) を参照する。

## 撤去した検査の記録

P7-2 で撤去したホスト試験と CI の最終版はコミット `dafe81642` にある。設計書や実装の註にある旧版の識別子は Git の blob ID で、`git show <ID>` が当時の本文を表示する。現在実行する検査の対象と限界は登録表を参照する。
