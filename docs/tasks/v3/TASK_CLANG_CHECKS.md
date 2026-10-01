# TASK_CLANG_CHECKS — C ソースの静的検査を clang の構文木で作り直す

> 状態: **実装中 (2026-10-01)** — ユーザー指示 (2026-10-01「現在の検査器をバックアップして作り直し」)。clang 21.1.8 と python3-clang (libclang) はホストに導入済み (ユーザーが `apt install clang libclang-dev python3-clang`)。コーダー Codex `gpt-6.1-sol`、レビュー Codex `gpt-6-astra`。
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-10-01)。関係: [TASK_C11_MIGRATION.md](TASK_C11_MIGRATION.md) (`check-c-dialect`、Codex 3 往復)、[TASK_T1_LEDGER.md](TASK_T1_LEDGER.md) §4-6 (`check-p2v`、Codex 3 往復)、[POLICY_DEV.md](../../POLICY_DEV.md)。

## 1. なぜ

自前の正規表現・字句解析で C のソースを読む検査器は、Codex のレビューで**型表記・キャスト・括弧・行継続・コメントの変形を突かれるたびに穴が見つかる**ことを繰り返した (T0 の `check-c-dialect`、T1f の `check-p2v`。どちらも 3 往復でユーザー決定により「guard、わざと作った入力への耐性は目標にしない」として決着)。型の解決・マクロの展開・GCC 拡張の解釈を **clang 本体に任せる**と、この種の穴が構造的に消え、検査器の保守とレビューが判定の規則に集中できる (ユーザー: 「独自検査器より信頼性があるのでは」)。

## 2. 範囲

| 検査器 | 今の作り | clang での作り直し (案 — 設計はコーダーが §4 に書いて Codex が見る) |
|---|---|---|
| `tools/check_p2v.py` (`make check-p2v`、[C5]) | 正規表現で物理キャスト・物理引数 | 構文木で「物理番地のマクロ・物理の名前 (`*_phys` など) に由来する整数 → ポインタのキャスト」と「ポインタ → 整数を装置・PTE・CR3 へ渡す引数」を、型とマクロ展開の位置で判定。例外一覧 `tools/check_p2v_allow.txt` は引き継ぐ |
| `tools/check_c_dialect.py` (`make check-c-dialect`、[C1]) | 前処理後の字句で C11 の機能を探す (654 行) | 言語モードの判定 (`-E -dM` で実効の `__STDC_VERSION__`) は今のまま。禁止の機能 (`_Atomic`・TLS・`restrict`・VLA など) と公開 SDK ヘッダの C89 互換は、構文木または clang の診断 (`-Wpre-c11-compat`、`-Wc11-extensions`、`-Wvla` など) で |
| `tools/check_le_access.py` (`make check-le-access`) | 外部形式 (LE) の直アクセスを正規表現で | 構文木で、媒体のバイト列への多バイト型のキャスト・逆参照 |
| `tools/check_arch_asm.py` (`make check-arch-asm`) | C の中の `hlt` / `cli` などの直書きを正規表現で | 構文木の `GCCAsmStmt` / `asm` の文字列 |
| `tools/audit_cast_align.sh` (手動の監査、docs/08_build.md §8-4) | grep で非整列アクセスの候補 | `-Wcast-align=strict` 相当の診断か構文木 |
| `tools/create_fat12_d88.py` | ビルドから呼ばれていない (`mkfat12.py` と重複) | **撤去** |

**変えないもの**: `check_privileged.py` (逆アセンブルは objdump で既に本物の道具)、`check_constraints.py` (ID の整合だけ)、試験の枠組み・変異の仕組み (`mutpar.py`、`*_host.c`)、カーネルを組むコンパイラ (i386-elf-gcc のまま。clang は検査にだけ使う)。

## 3. 進め方

1. **バックアップ**: 今の 5 本を `tools/legacy_checks/` へ写す (`git mv` ではなく写しを残し、作り直した版と同じ入力で流して結果を比べられるようにする)。`make check` からは外す。比べ終えて新しい版が受け入れられたら、撤去するかはユーザーが決める。
2. **共通の土台** (`tools/clang_ast/` など): `make -n` から各翻訳単位の実際の旗を取り (`check-c-dialect` の既存の取り方を流用)、i386-elf-gcc 固有の旗を clang 向けに読み替え (`-target i386-unknown-none-elf`、`-ffreestanding`、インクルード、マクロ)、libclang (python3-clang) で構文木を得る。clang が解析に失敗した翻訳単位は**黙って飛ばさず失敗にする**。
3. 5 本を順に載せ替える。各検査について: 今の版が見つけている違反・例外を新しい版も同じく扱うこと (結果の比較)、Codex がこれまでの往復で出した反例 (T0 §6-4・§7、T1 §4-6-R、`tools/tests/c_dialect_tdd.md` ほか) をすべて試験に入れること、変異は実行時に落ちるもの。
4. **CI**: `.github/workflows/check.yml` と `build.yml` に clang と python3-clang の導入を足す。

## 4. 設計 (コーダーが実装の前に書く)

(空欄 — コーダーが §3 の 2 の土台の形、旗の読み替えの表、GCC 拡張で clang が受け付けない箇所の扱い、各検査の判定規則を書く)

## 5. 受入

- 5 本それぞれ: 実物の木で今の版と同じ違反 0 / 例外の扱い (差が出たらその理由を列挙)、これまでの反例をすべて検出、変異が実行時に RED。
- `make check` と `make check-changed` の両方から到達する。clang の解析失敗は失敗になる。
- CI の check / build が通る。
- `make all` rc=0、`make check` rc=0。
