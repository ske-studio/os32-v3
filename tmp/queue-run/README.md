# OS32 の段の待ち行列

PM は配布後、ひな形の `QUEUE.tsv` を `~/os32-tmp/run/QUEUE.tsv` に置く。
各段の PACK を用意し、実行の合間に TSV に段を足す。
1 行 1 段、8 列をタブで区切る。先頭が `#` の行は注記。
依存は id をカンマで区切り、依存なしなら空欄。空の note にも直前のタブが要る。
作業木と PACK の相対パスは `~/os32-tmp/run` 基準。
build のパスは既存の起動スクリプトに合わせ、空白やシェルの特殊文字を含まない形を使う。

```tsv
# id	kind	model	workdir	pack	deps	state	note
inspect	read	gpt-6.2-astra	/home/hight/os32	inspect/PACK.md		queued	調査
implement	build	gpt-6.2-sol	/home/hight/os32-work	implement/PACK.md	inspect	queued	実装
```

`~/os32-tmp/bin/queue_run.sh --dry-run` で予定を確認し、引数なしで 1 回処理する。
上限の既定値は build=3、read=8。本線は `~/os32-tmp/run/QUEUE.conf` に `build=4` を書く。
設定は 1 行に `build=N` または `read=N`。環境変数 `QUEUE_BUILD_LIMIT`、`QUEUE_READ_LIMIT` が優先する。
依存がすべて done で、PACK と作業木があり、枠が空いた queued の段から、行順に起動する。
build は `codex_run.sh`、read は read-only の Codex で実行する。
見張りは従来どおり `watch_new.sh <段>/impl.log`。通知後に queue_run.sh を回すと、
末尾の `rc=0` は done、それ以外の `rc=N` は failed になり、次の段を処理する。
終了行がない running は枠を確保する。止まった段の扱い、失敗した段を held/queued に戻す判断、
取り込みと設計の確定は PM が行う。
