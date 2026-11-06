# TASK_DICT_META — FEP 辞書のメタ情報と、学習データの別ファイル化

> 状態: **実装中 (2026-11-07)** — 段 A〜N は main。段 P はゲスト試験・文書の段。ゲスト受入は PM が行う。
> 上位: [FEP の索引](00_INDEX.md)。契約・形式・実装分担・受入の正典は [DICT_META_DESIGN](../../decisions/fep/DICT_META_DESIGN.md) §2〜9 と末尾の確定事項。

## 0. 決定

| # | 決定 | 出所 |
|---|---|---|
| M1 | 辞書に形式の版・ID・原文ライセンス等のメタ情報を持たせる | ユーザー 2026-09-30、確定設計 §2 |
| M2 | 学習データをシステム辞書と別ファイル `/etc/fep_user.db` に分ける | 同日、確定設計 §3 |
| M3 | `mem_reserve_kb` を起動時の FEP 予約量の出典とし、settings で上書き可。予約を超える切り替えは現辞書を残し、再起動で再確保 | MEMMAP_V3 D27 [^memory] |
| M4 | SQLite・FEP モジュール化後に実装する | ユーザー 2026-09-30、実装着手は 2026-11-05 |
| M5 | `license` / `attribution` は原文全文を辞書に収録し、`ime_dict_info` で分割取得する | 確定設計 §2・末尾の公開契約 |
| M6 | 旧い学習データは移さず、新しい学習 DB は空から作る。`fep_user.db*` を hsync の保護名にする | ユーザー 2026-11-05、確定設計 §4 |

[^memory]: 現行 [MEMMAP_V3 D27](../../decisions/MEMMAP_V3.md#0-決定事項の一覧-d1d36) は同ファイルの 30 行目。D28 と §4-6 は現行文書に無い。メタ情報・別ファイル化の契約は確定設計 §2・3、予約の具体的な手順は同 §5・6 を参照する。

## 1. 現行の契約

- システム辞書は RO、学習 DB は RW の 2 接続。切り替え時の probe 用に RESIDENT は 3 枠。
- S/M/L は共通 ID の学習を使う。ID または品詞表の版が違えば学習を停止し、system の変換を続ける。
- 設定のキーは scope=`system` の `fep_mem_reserve_kb` と `fep_user_dict_path`。学習の custom path は `/etc` 直下に限る。
- 使用中のシステム辞書は差し替えない。非選択辞書の更新と切り替え、または `hsync --fep-off` による停止中更新を使う。

## 2. 段 P のゲスト試験

`userland/tests/fep_meta_test.c` は実 SQLite と公開 KAPI で 20 キーの全 byte・語数、学習後の S→L→S、settings の予約境界、ID/品詞不一致時の学習停止と system 変換を検査する。各項目の PASS/FAIL と集計行・終了コードを返す。

PM は退避した試験用イメージに S/M/L と試験プログラムを配備し、空の専用学習 DB を使う。起動前に以下を設定し、再起動してから実行する。

```text
cfg set system fep_mem_reserve_kb int 224
cfg set system fep_user_dict_path text /etc/fep_meta_user.db
```

試験は L の予約量と専用学習 DB の ID・品詞表の版を一時変更し、終了時に元へ戻す。中断した場合は退避イメージから戻す。試験後は元の settings を復元して再起動する。

## 3. 受入の残件

- ゲスト未実行。登録と NP21/W での実行・観測は PM が担当する。
- 配備・hsync・再起動後の学習 4 列保持と FDRECOV の消失条件は [FEPLEARN-1](../DEFERRED_TESTS.md) に残す。PM のゲスト受入後に閉じる。
- 壊れたヘッダ等のゲストで作らない状態は `tools/tests/test_fep_meta.py`、ROFS・hsync 保護等は確定設計 §9 の受入で扱う。
