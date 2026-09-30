# F1 DB owner lifetime — host TDD evidence

対象: 実 `kapi/kapi_db.c` をincludeする `kapi_db_owned_host.c`。
実DB slot実装の複製はない。SQLite境界のfault injection、owner取得、SHMアドレス、
kstring/kprintfのhost shimのみ。SQLiteの実VFS/実ファイル/exec終了は試験しない。

実行: `ulimit -c 0; python3 tools/tests/test_kapi_db_owned.py`
全サイクルでcompileは成功し、REDはCのassertion/SIGABRTによりrunnerがexit 1。
各最小変更後に同じコマンドを再実行し、それ以前の全ケースを含めexit 0を確認した。

| 順 | 新しい挙動 | 観測したRED assertion | GREEN変更 |
|---|---|---|---|
| 1 | open owner記録・owner指定回収 | `db_cleanup_owned != 0` | owner記録、内部entry追加。親stmt継続/子複数回収/再回収/再利用/旧全回収を通過 |
| 2 | 共通teardown順序 | traceが`FRC`でない | 明示closeへrollback追加、旧全回収も同じ経路へ。3経路とtransactionなし`C`を通過 |
| 3 | finalize失敗を成功と返さず診断保存 | `kapi_db_close(h) == -1` | 最初のrc/messageをコピー、close成功後も再利用まで保持 |
| 4 | rollback失敗を成功と返さず診断保存 | `kapi_db_close(h) == -1` | rollback rc保存、closeは継続。close成功時のslot再利用を通過 |
| 5 | close BUSYのslot隔離 | `kapi_db_close(h) == -1` | close成功時だけ解放。3経路×先行失敗段階を試験。隔離handle操作、全/owner再回収、同nest再利用、容量枯渇でも再接触しない |
| 6 | nonNULL失敗openの初期診断と安全なteardown | `hdr->status == DB_STATUS_ERROR` | open診断保存後に共通close。rollback失敗+close BUSYでもopen診断が残り、再利用されない |
| 7 | NULL失敗openの診断 | SHM messageが`out of memory`でない | 接続がない時は`sqlite3_errstr(rc)`、close不要でslot再利用可能 |

最終host出力:

```text
PASS owned, teardown order, finalize/rollback faults, BUSY isolation, open faults
Ran 1 test
OK
```

runnerは1 unittestから6 Cシナリオ関数を呼ぶ。BUSY試験は明示close/owned/allの各経路で、
close単独、rollback+close、finalize+rollback+close失敗を注入する。
`db_prepare`の初回ROW/column値、親stmtの次ROW継続、所有者が異なるhandleへの従来のアクセスを確認。
新たなownerアクセス制限やgeneration ABIは導入していない。

## ターゲットcompile

`build/config.mk` のKERNEL_CFLAGS/INC_KAPI相当を用いて、実ヘッダで以下を実行。
Make全体を起動せず、生成物/環境ファイル/他担当objectを触らないよう出力はtmpへ置いた。

```sh
obj=$(mktemp /tmp/os32-db-f1-XXXXXX.o)
i386-elf-gcc -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie \
  -fno-stack-protector -nostdlib -mno-red-zone -fcommon -O2 \
  -Wall -Wextra -Werror -Wdeclaration-after-statement -D__KERNEL_BUILD__ \
  -I. -Iinclude -Isdk/include -Isdk/include/os32 -Ikapi -Ikernel \
  -Idrivers -Ifs -Iexec -Igfx -Ilib -Ilib/sqlite3 \
  -c kapi/kapi_db.c -o "$obj"
file "$obj"
i386-elf-nm "$obj"
```

結果: exit 0、警告なし、`ELF 32-bit LSB relocatable, Intel i386`。
`db_cleanup_owned`/`db_cleanup_all`と従来の公開DB関数を確認、`res_owner_get`は外部参照。
これはtranslation unit compileでありkernel全体のlink/boot検証ではない。

## 制限・引継ぎ

- hostはLP64。共有ヘッダのu32/i32はhostではlong幅となるため、hostのSHM構造サイズをguest ABI証拠にしない。
  この試験はslot寿命/制御順序/診断/prepare初回stepを対象とし、SHM列レイアウトはF5。
- `exec/exec.c:exec_exit`の既存呼出しを調査: 汎用FD回収が先、後で`db_cleanup_all()`。
  呼出し元は変更していない。全active slot回収を残すため実子終了で親DBが生存する保証はまだない。
- close失敗のslot隔離はFD保護ではない。main/journal/temp全体の追跡・隔離が汎用FD回収前に成立するまでF2統合blocked。
  mainだけprotect/全FD protectは実装しない。隔離slot/SQLite資源は未回収として保持し、回復・再試行APIは追加しない。
- finalize/rollbackが失敗してもclose成功ならslotは空くが、closeの返値は-1で初期診断を再利用まで保持。
  失敗openはSHMに即時診断、内部slotにも保存。owner別`handle=-1`エラー取得ABIはF4であり今回は未実装。
- 実SQLite/VFS障害、FD生存、FEP実走、kernel全link、emulator/deploy/guest試験は未実施（依頼範囲外）。
  ネットワーク/秘密情報/環境設定/他担当ファイル/commit/追加agentは操作していない。
