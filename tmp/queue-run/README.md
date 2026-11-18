# OS32 の段の待ち行列

PM は配布後、ひな形の `QUEUE.tsv` を `~/os32-tmp/run/QUEUE.tsv` に置く。
各段の PACK を用意し、実行の合間に TSV に段を足す。
1 行 1 段、9 列をタブで区切る。先頭が `#` の行は注記。
旧 TSV には note の前に accept を足し、旧 done の段は報告とレビューを確認して returned または accepted に直す。
依存は id をカンマで区切り、依存なしなら空欄。空の note にも直前のタブが要る。
作業木と PACK の相対パスは `~/os32-tmp/run` 基準。
build のパスは既存の起動スクリプトに合わせ、空白やシェルの特殊文字を含まない形を使う。

```tsv
# id	kind	model	workdir	pack	deps	state	accept	note
inspect	read	gpt-6.2-astra	/home/hight/os32-tmp/run/inspect	inspect/PACK.md		queued	none	調査
implement	build	gpt-6.2-sol	/home/hight/wt/os32-v3/implement	implement/PACK.md	inspect	queued	np21w	実装
```

`~/os32-tmp/bin/queue_run.sh --dry-run` で予定を確認し、引数なしで 1 回処理する。
上限の既定値は build=3、read=8。本線は `~/os32-tmp/run/QUEUE.conf` に `build=4` を書く。
設定は 1 行に `build=N` または `read=N`。環境変数 `QUEUE_BUILD_LIMIT`、`QUEUE_READ_LIMIT` が優先する。
依存がすべて accepted で、PACK と作業木があり、枠が空いた queued の段から、行順に起動する。
稼働中の Codex は対応する行の kind で数える。行がなければ `-C` などで指定した作業先が `/home/hight/wt/` 配下なら build、それ以外は read。
build は `codex_run.sh`、read は read-only の Codex で実行する。
見張りは従来どおり `watch_new.sh <段>/impl.log`。通知後に queue_run.sh を回すと、
末尾の `rc=0` は returned、それ以外の `rc=N` は failed になり、枠が空く。
PM は報告とレビューを確認し、受け入れた段を手で accepted にする。
accept は `none`、`np21w`、`g4`、`qemu-local` のいずれか。none 以外の returned は末尾の「手元の受入待ち」に表示する。
終了行がない running は枠を確保する。止まった段の扱い、失敗した段を held/queued に戻す判断、
取り込みと設計の確定は PM が行う。
