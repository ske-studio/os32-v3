---
name: os32-ci-inventory
description: 旧 CI と現行 CI の全件所要、登録検査と変異の数を比較する。
---

# os32-ci-inventory — CI の所要の棚卸し

変更前後の全件所要・登録数・変異数と測定条件は [CI の実測](../../../docs/design/tools/ci/gate.md) にある。
現行の所要は `tests.infra.ci collect` の bundle 別秒数と、同じ workflow 実行のジョブ所要を照合する。
