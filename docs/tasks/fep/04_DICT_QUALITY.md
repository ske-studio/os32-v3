# [P4] 辞書品質向上

> 索引: [`00_INDEX.md`](00_INDEX.md)
> 優先度: **中** ／ 依存: P3 (切替 UI)

[`FEP_FUTURE.md`](FEP_FUTURE.md)「辞書品質向上」。ランタイム (SQLite) とビルド時 (Python) の両面。

---

## 1. PRAGMA 最適化 (ランタイム)

確定時の学習 UPSERT (`ime_dict_learn`) のラグを削減する。
`ime_dict_open()` で DB オープン直後に PRAGMA を発行:

```c
/* ime_dict_open() 内、prepare 前に追加 */
sqlite3_exec(db, "PRAGMA synchronous=OFF;", 0, 0, 0);
sqlite3_exec(db, "PRAGMA journal_mode=MEMORY;", 0, 0, 0);
sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", 0, 0, 0);
sqlite3_exec(db, "PRAGMA cache_size=-256;", 0, 0, 0);  /* 256KB */
```

| PRAGMA | 効果 | リスク |
|--------|------|--------|
| `synchronous=OFF` | fsync 省略で書込即返り | クラッシュ時に直近学習が失われる |
| `journal_mode=MEMORY` | ロールバックジャーナルを RAM に | 書込中クラッシュで DB 破損可能性 |
| `cache_size` | ページキャッシュ拡大で検索高速化 | MEMSYS5 圧迫 (要計測) |

> **トレードオフ判断:** 学習辞書は「失っても再学習で回復する」性質なので
> `synchronous=OFF` の損失リスクは許容範囲。ただし `journal_mode=MEMORY` の
> DB 破損リスクは重い。**まず `synchronous=NORMAL` + `journal_mode=MEMORY` を
> 試し**、ラグが残るなら `OFF` に下げる段階導入とする。
> `cache_size` は MEMSYS5 の空き ([`00_INDEX.md` §0](00_INDEX.md#0-設計の前提条件)) を `mem_stat` で確認してから設定。

---

## 2. 頻度データ取り込み (ビルド時 `fep_to_sqlite.py`)

IPADIC 単独のコストに、外部頻度コーパスを重畳する。

```
入力: IPADIC CSV (現状) + 追加頻度表 (Wiktionary / 青空文庫 n-gram)
処理: 表層形をキーに頻度をマージ → log 正規化 → 既存コスト式に -freq_bonus
出力: dict.cost (小さいほど優先)
```

`fep_to_sqlite.py` のコスト計算に頻度ボーナス項を追加 (既存の常用漢字ブースト・
圧縮率ヒューリスティクスと加算):

```python
# 擬似コード
cost = base_cost_from_ipadic(entry)
cost -= freq_bonus(freq_table.get(surface, 0))   # 高頻度ほど減算大
cost = apply_joyo_boost(cost, surface)
cost = apply_compression_heuristic(cost, yomi, surface)
```

頻度表のライセンス/入手は別タスク。スキーマ・検索ロジックは不変なので
**DB 再生成のみで反映** (カーネル側変更不要)。

---

## 3. 辞書バリアント動的切替

[`03_DICT_COMMAND.md` §3](03_DICT_COMMAND.md) の `ime_switch_dict()` で実装済み。`ime dict s|m|l` から呼ぶ。

動的切替を実運用するには `deploy.yaml` に S/L 辞書を配備する:

```yaml
- host: assets/fep_s.db
  guest: /db/fep_s.db
  tags: [data]
- host: assets/fep_l.db
  guest: /db/fep_l.db
  tags: [data]
```

> フロッピー/HDD イメージ容量との兼ね合いに注意 (M=~5.5MB)。
> S/L を常時同梱せず、必要時に手動配置する運用も選択肢。

---

## 4. 学習辞書の永続化検証

回帰テスト項目として明文化:
1. 数語を変換確定 (学習発生)。
2. NP21/W を**ソフトリセットせず再起動** (電源 off→on 相当)。
3. ext2 上の `/db/fep.db` の `dict_user` が保持されていること。
4. 同じ読みで再変換し、学習候補が最優先に来ること。

`synchronous=OFF` 採用時は「正常 `ime off` / シャットダウン経由なら保持」を保証する。
→ シャットダウンシーケンス (`sys_shutdown` 等) で `ime_dict_flush()` (新規、
`PRAGMA wal_checkpoint` 相当 or `sqlite3_db_cacheflush`) を呼ぶフックを検討。

---

## 5. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `kernel/ime_dict.c` | `ime_dict_open` に PRAGMA、`ime_dict_flush` (任意) |
| `tools/fep_to_sqlite.py` | コスト式に頻度ボーナス項 |
| `deploy.yaml` | S/L 辞書配備 (任意) |

**テスト:** §4 の永続化検証 + 確定ラグの体感比較 (PRAGMA 適用前後)。
DB 再生成後は `analyze_dict.py` / `check_candidates.py` で候補順を確認。

---

*前: [`03_DICT_COMMAND.md`](03_DICT_COMMAND.md) ／ 次: [`05_GFX_MODE.md`](05_GFX_MODE.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
