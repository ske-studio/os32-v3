---
name: os32-test-basis
description: 試験の期待の出所 (basis) を設計に結び、ソースの形への固定を外す。新しい検査を登録する時、試験がハッシュ・特定のコミット・行の並びに固定されていて直しのたびに落ちる時、変異の目印がずれた時、検査の宣言の lint が落ちた時に使う。
---

# os32-test-basis — 試験の出所と、固定の外し方

期待は現行設計の節から取る。1 契約につき `tests/registry.toml` に 1 件を登録し、対応する本体を `tests/<subsystem>/<id>.py` に置く。仕様の節 (spec)、対象 (sources)、観測と合否 (expect)、限界 (limits)、検出する変異 (mutations) を含む全欄は [登録の仕様](../../../docs/design/tools/checks/registry_v2.md) に従う。

登録を `python3 -B -m tests.infra.ci lint` で照合し、`python3 -B -m tests.infra.ci run --check <id> --mutate` で正常対照と各変異の検出を確認する。機能実装の再開後は、触る契約の検査を同じ変更で足す。

物理番地の定数や欄を足した時は、`tests/infra/p2v.py` の `PHYS_MACROS` (定数名) または `observe` 内の `refs` と照合する名前の集合 (欄名) にも足す。

古い検査は関門ではない (P7 で消す)。参考に読むだけ。
