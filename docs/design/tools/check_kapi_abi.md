# KAPI の末尾追加と互換性

公開スロットとデータ欄の比較基点、互換性を破る場合の宣言、生成物の同期は
[KAPI 仕様 §3-4・§3-5](../../KAPI_SPEC.md) に従う。

`python3 -B -m tests.infra.ci run --check kapi_abi` は環境変数 `OS32_KAPI_BASE`
の完全なコミット SHA と候補を比べる。C と Rust の生成物および仕様表は
`python3 -B -m tests.infra.ci run --check kapi_sync` で照合する。
