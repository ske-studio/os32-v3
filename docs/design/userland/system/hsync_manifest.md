# userland/system/hsync.c — 配備の名札と世代の確認

> 状態: **現行 (2026-11-18)** — 出所: TASK_H4 の契約を今のソースと照らして要約
ソース: `userland/system/hsync.c` (`man_load`・`man_parse`・`man_parse_entry`・`man_path_ok`・`man_gate`・`man_note_copy`・`copy_body`)・`tools/hostdrv_deploy.py` (`write_manifest_file`・`manifest_path_ok`・`manifest_line`・`remove_manifest`・`sync_failed`・`do_sync`)

名札は「この配備元がどの版か」を書いた平文で、ホストの配備 (`make deploy`) が書き、ゲストの `hsync` が読む。
**同じか違うかを見る名札であって、新旧の順序を表さない。** 写すかどうかの判定は同じ置き場の `hsync_compare.md`。

## 形式

置き場は配備元の `.deploy/manifest.txt` (ゲストからは `/host/.deploy/manifest.txt`)。UTF-8、LF。

```
format=2
build=9742a6b-dirty
generated=2026-09-16T21:45:19Z
kapi=1208
kapi_version=63
count=198
---
bin/cat.bin 16428 3b7f2a10 1789520013
```

- `---` までは `key=value`。鍵は上の 6 つで、全部が要る。重複・知らない鍵は形式違いとして捨てる。
- ファイルの行は **パス / サイズ (10 進) / CRC-32 (ちょうど 8 桁の小文字 16 進) / mtime (Unix 秒)** で、区切りは空白 1 つ、欄はちょうど 4 つ。
  CRC は `lib/crc32_core.inc` と同じ CRC-32 (ホストでは `zlib.crc32`)。行は並べ替えて書く。
- パスは配備元の根からの相対。絶対パス・空の要素・`.`・`..`・`\`・空白・制御文字は書かない・読まない。
- `build` は組み済み成果物の ID (`build/out/build_id.c` の `os32_build_commit`、作業木が汚れていれば `-dirty`)。読めなければ `unknown`。
- `kapi=` / `kapi_version=` は配備物の KAPI データ欄の配置と版。これを使う門 (`man_kapi_gate`) は TASK_KAPI_DATA_FIELDS の側の規則。

## 書く側 (`tools/hostdrv_deploy.py`)

- **全件の配備が成功した後にだけ書く。** 1 件でも失敗したら既にある名札 (と書きかけの一時ファイル) を**消す** — 古い名札が残ると「配備済み」と誤読される。
- `--no-manifest` と `--tag` の部分配備は名札を書かず、古い名札は消す (新しいファイル + 古い名札で `--expect-build <古い ID>` が一致してしまうため)。
- 書き方は**一時ファイル (`manifest.txt.tmp`) + `os.replace`**。最終パスを書き込みで開かない。
- 書けないパス (空白・絶対・`..`・`\`) や重複する配備先があれば名札を書かない (配備の失敗)。
- 名札に載るのは、写したものと「同一でスキップした」もの (どちらも配備元に在る)。名札自身は載らない。
- 守れるのは名札そのものの完全性だけ。**ファイル群のコピーと名札の更新は原子的ではない** — 名札を書く前にホストが落ちると
  「ファイルは新しく名札は古い (無い)」になり、`--expect-build <新 ID>` が `build_mismatch` で断る (安全側)。`make deploy` を打ち直せば戻る。

## 読む側 (`hsync`)

- 同期を始める前に 1 回だけ読む (本文はコピー用バッファ `file_buf` を借りる、上限 `HS_MAN_TEXT_CAP`)。**無ければ今までどおり動く。**
- 読めたら表示の 1 行目に `DEPLOY build=… count=… generated=… kapi=…/v…` を出す。
- **全件を先に検査し、1 件でも壊れていたら名札ごと捨てる** (半端な表を残さない、`g_man_valid = 0`・`g_man_count = 0`)。
  捨てる条件: 形式版違い・`---` が無い・鍵の欠け / 重複 / 未知・`count` と行数の不一致 (多くても少なくても)・
  重複パス・不正なパス・`NAME_CAP` (64) 以上の長さのパス・サイズ / CRC / mtime の桁の不正 (u32 の桁あふれを丸めない、CRC の大文字を通さない)・
  `HS_MAN_MAX` (320) を超える件数 (切り詰めて使わない)・本文が上限を超える・読み取りの失敗。
  捨てたら `DEPLOY manifest invalid: <理由>` (理由は固定文字列)。
- 名札の open が HostDrv の不通 (`HOSTDOWN`・`TIMEOUT`・`STALE`) で失敗したら `DEPLOY reason=hostdown|timeout|stale` で断る。

## `--expect-build <ID>` の門 (`man_gate`)

一致は**文字列の完全一致** (大小を区別、前方一致もしない)。断るときは 1 件も書かずに非ゼロ終了。

| 名札の状態 | 全体同期 | 範囲を絞った同期 (`hsync usr` など) |
|---|---|---|
| 読めて一致 | 続ける | 続ける |
| 読めて**不一致** | 断る (`build_mismatch`) | **断る** |
| 壊れている | 断る (`manifest_invalid`) | 表示だけして続ける |
| 無い | 断る (`manifest_absent`) | 表示だけして続ける |

「確かめた結果おかしい」(不一致) と「確かめられない」(壊れている・無い) は別。前者は絞り込みでも断る。
後者は、名札がルートの世代を表すもので絞った範囲の正しさを保証しないので、壊れている場合と無い場合を同じに扱う。
`--expect-build` が無ければ表示だけで同期を続ける (意図した巻き戻しもある)。

## 同期中の扱い

- **名札を信じて内容比較を省かない。** 同一判定は `hsync_compare.md` のまま。永続の CRC キャッシュも作らない。
- 名札があるとき、コピー元を読みながら作った CRC を名札の値と照合し、違えばその 1 件を `manifest_crc_mismatch` の errors にして公開しない。
- `manifest_extra=N` を集計の後に出す: 写した (dry-run では写す予定の) ファイルのうち名札に無いもの。数えるだけで拒否はしない。
  -v なら 1 件ずつ `EXTRA <path> reason=not_in_manifest`。名札が無効なら行ごと出さない。
- 名札自身 (`.deploy/manifest.txt`) も同期の列挙に現れて写る (除外しない) が、`manifest_extra` には数えない (名札は自分の CRC を載せられない)。
- `manifest_missing` (名札にあるのに配備元に無い) は数えない。名札を正とする走査が別に要る。
- 名札のパスは根からの相対なので、`--root` のときも根を除いて照合する。
