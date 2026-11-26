---
name: os32-ci-inventory
description: 停止した旧 CI の所要とシャード分割の参考記録。
---

# os32-ci-inventory — CI の所要の棚卸し

停止前の計測値と測定条件は [CI の実測](../../../docs/design/tools/ci/gate.md) にある。
現行の所要は `tests.infra.ci collect` の bundle 別秒数と、同じ workflow 実行のジョブ所要を照合する。
