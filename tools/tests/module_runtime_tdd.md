# Module transaction core host fixture (T4a-5)

契約: [TASK_T4_T6_MODULES §2-2 / T4a-5](../../docs/tasks/v3/TASK_T4_T6_MODULES.md#2-2-状態と公開)。
`make check-module-runtime-host MUTATE=0 HOST32_RUNNERS=qemu` は実際の
`module_runtime.c` と `module_image.c` を GNU11 ILP32 でリンクする。
画像読込用 VFS FD/owner/page を置換し、FS/Device 登録と mount は実 `vfs.c` / `dev.c` をリンクする。確保内容を破棄する free と executable body を使う。

- owner 作成・load_span・init 内 2 回の割当をそれぞれ失敗させ、FD、owner、全ページが開始前へ戻る。
- CRC・export slot/重複の拒否で、回収直前の像全体と未使用 BSS が読込直後から不変。
- prefix 返却失敗は回収済み ABSENT でも `MODULE_INIT_FAILED` を返し、隔離時は `MODULE_ISOLATED` を保持する。いずれも成功を返さず export は未公開。清掃 1 回・owner の回収または保持も確認する (T4a Q1 / T4c-1)。
- init の最中は内部 export 未公開。init 失敗・export 改変も公開せず逆順に清掃する。
- prefix を 0xdd で破棄した後に body の export を呼んで 42 を得る。依存 handle の入力表を破棄した後も record 内の generation で解決する。
- record 満杯・依存容量超過は open/割当前に拒否。二重 abort・古い generation は二重 free しない。
- STARTING の IRQ/tick/callback は owner/text を照合して登録可能。quiesce→detach/drain→DMA停止の順序を観測する。
- 各 cleanup 失敗と、成功を返す cleanup の IRQ/tick/callback/DMA/実行中参照の残りを個別に注入し、全所有ページを隔離する。
- boot transaction は依存利用側から逆順に清掃する。生きた利用側を残して依存先を abort してもページを返さない。

PM 用の `--mutate` は正常対照の後に、STARTING の先行公開・参照にかかわらない
無条件 free・依存先の先行 free・relocate の拒否結果無視・export 件数/slot の転記誤り・prefix 返却失敗後の偽の成功を実行時 assertion で検出する。T4c-1 は依頼に従いこの検査の変異も実行する。
既存 `check-kdebug-init-host` は実 kdebug 像・pair/拒否理由・OFF の open/owner 0・
RUNNING から実自己試験の dispatch を継続して確認する。ゲスト起動はこの fixture の合格に含めない。

T5c-0: FS の8枠/Device の16枠を埋め、拒否時不変・重複・未登録解除・中央の削除と詰め直し・再利用を実行する。FS は mount 中の BUSY と unmount 後の解除を対照にする。10 record の満杯でファイル/owner確保0を確認する。
3段の依存を load/start し、末端の STARTING が Device/FS を登録・mount した後で失敗する。解除成功なら検索/登録/キャッシュ参照0を **reclaim の直前** に検査し全ownerを回収する。mount/Device キャッシュ/各停止段の失敗なら末端と依存2段を逆順 abort して全owner保持、export非公開、再回収0を確認する。永久 DEVICE 予約のコードは変更しない。
STARTING 中に実 body を IRQ consumer 相当から呼ぶ試験は、CPU の IRQ/PIC/DMA を模擬しない。各装置の実 IRQ 有効化/停止の観測は切出し段で行う。新しい変異は登録残存無視・STARTING拒否・満杯の偽成功・mount中解除・Device解除欠落を runtime assertion で RED にする。

## T5b-5: CD 登録アダプタ

`make check-cd-module-host MUTATE=0` は実物の cd アダプタ・module runtime・FS/Device
登録表を ILP32/qemu で組む。既存のページ fixture に init を注入し、RELOCATED 以降の
登録・満杯・init 失敗・prefix 返却失敗・unmount 後解除・mount 中の body 隔離を確認する。
実物の `cd.mod` は mkmod との一致、PAIR/phase/短読/CRC 拒否、relocate、core に残る
IDE/FDC/diskio と部品へ移した ATAPI/ISO のシンボルも照合する。
`MUTATE=1` の正常対照後、FS 拒否無視・装置解除削除・登録結果無視を runtime RED にする。

PIO と部品 init は `check-cd-read-host` の既存 NP21/W 装置モデルで実行する。
追加対照は無媒体の装置公開 0、512B の hd/loop 上の ISO、破損/短読 PVD の mount 0。
無媒体の公開・セクタ換算省略・PVD 版確認省略の変異を runtime RED にする。
これは PC-98 の実起動の代用ではない。FD/HDD からの起動と実 CD の利用は PM が確認する。
