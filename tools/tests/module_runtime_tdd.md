# Module transaction core host fixture (T4a-5)

契約: [TASK_T4_T6_MODULES §2-2 / T4a-5](../../docs/tasks/v3/TASK_T4_T6_MODULES.md#2-2-状態と公開)。
`make check-module-runtime-host MUTATE=0 HOST32_RUNNERS=qemu` は実際の
`module_runtime.c` と `module_image.c` を GNU11 ILP32 でリンクする。
VFS/owner/page のみを置換し、確保内容を破棄する free と executable body を使う。

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
