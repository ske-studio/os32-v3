# 旧 S7 確定設計への追記 r2 — 2026-10-23 PM
- r1との差分：D1を採用し観測専用snapshotを削除、snapshotはS8 A1の既存RECへ移す。
- D2を採用しINSPECTはROM/chipの1行と終わりの行だけ。BAR/resources表示・import/契約変更を削除。
- 新trialのmonitorは--session 0＋新state、PAUSED継続は取得sid＋同じstateへ修正。
- wire移設は再生成差0と二重定義なしを確認。T5後のIDLE abortによるlease返却を先行段に追加。
- monitor追加は拒否検出だけ。S7bは実機の回とし、実装段から分離する。

## 1 実物の確認 (file:line)
- パスは `/home/hight/os32-v3/` 起点。REVIEW.r1.mdのPM決定D1・D2と全指摘を採用。本紙は設計のみ。
- `userland/shell/cmd_recover.c:5,52` と `kdebug/recover_wire.h:3` がprivate wire＋既存kdebug_callの先例。導入版78・slot253を使い、KAPIは変えない。
- `kdebug/gfinit.h:5-17` は既存op5〜10と40B要求。`kdebug/gfinit.c:297-307` は版・内外size・旗・4096 steps・出力欄ゼロを検査する（r1確認時の行番号）。
- `kdebug/kdebug_main.c:105-123` はGFを一般PAUSED門より先にhwdevへ渡す。GF自身のsession・状態・main順序検査を維持する。
- `kdebug/gfinit.c:117-147,174-203` のINSPECTはROM二重読取・PCIR・checksum・decode。表示を足すだけでsnapshot経路は作らない。
- レビュー確認箇所：`recover.c:1174,384` のsnapshot_crc/既存snapshot行をA1で採取。`gfinit.c:327-329` のtrial_beginは新sidを採る。
- `tools/rshell_recover.py:36-49` は--session 0の場合だけ最初のRECのsidを採用。`gfinit.c:302,334-336` のIDLE abortは旗を要するが解放のみ。
- 実機main 2b28094、kdebug on、hwdev読込済み、status済み、recover now 0のPARTIAL合格は既知条件。GFのS0成功とは区別する。

## 2 wire とコマンド
- 新 `kdebug/gfinit_wire.h` に既存op・版・旗・40B構造体をそのまま移す。u32定義後に読めるGNU89互換privateヘッダとする。
- [C4] GF_INIT_SCHEDULE=4096も一本化し、gfinit_decode.hから参照。旧:7との二重定義が残らないことを確認する。
- gfinit.hはwireをincludeし40B assertと関数宣言を保持。gen_hwdev_contract.py:98のglobで新ヘッダも拾われる。DWARF inventory・契約は再生成で差0を確認し、hwdev importも変更しない。
```c
/* kdebug/gfinit_wire.h: 既存の順序・数値・意味を維持 */
struct gfinit_request {
    u32 version, size, flags, session, script_id, max_steps;
    u32 status, fail_pc, fail_reg, steps;
};
/* userland/shell/cmd_gfinit.c: shell.hの後、ゼロ初期化して入力のみ設定 */
#include "../../kdebug/gfinit_wire.h"
rc = g_api->kdebug_call(op, &q, sizeof(q));
```
| 段 | コマンド | op・役割 |
|---|---|---|
| 先行T5 | `gfinit inspect <sid>` | 5、flags=0、ROM/version.chip確認 |
| 先行T5・復旧 | `gfinit abort <sid> --allow-write` | 10、IDLEではlease/ROM解放、それ以外は既存終端 |
| S8 | `gfinit size-bars <sid> --allow-write` | 6、A1測定と終端 |
| S8 | `gfinit prepare <sid> --allow-write` | 7、A2後PAUSED |
| 後続 | `gfinit run-one <sid> <main> --allow-write` | 8、main 0〜4の指定1本 |
| 後続 | `gfinit verify <sid> --allow-write` | 9、全main後のA4・終端 |
- sid/mainは十進u32。過不足・未知語・負数・桁溢れ・main範囲外はusage/終了値2、呼出し0。正常0、拒否/失敗1。出力4欄は毎回ゼロ。
- コマンド引数sidは現在値（statusまたは前回応答）を使う。PAUSED中は補助命令を挟まない。自動再試行・自動次段は設けない。
- KDEBUG=0でも登録し既存NOSYSを表示、失敗終了、装置I/Oなし。API版不足/NULLはunavailable。hwdev未公開の-1と区別する。

## 3 出力と受け方
- D2：INSPECTは通常行 `GF inspect sid= rc= rom_len= crc= chip= expect= match=` と `GF inspect-end sid= rc=` だけ。RECのsid/seqを進めない。
- hwdev側で実ROMから長さ/CRC/version.chipを表示し、既存importのkprintfを使う。新import・契約・可変長wireは追加しない。chipの位置/期待値は部品内の根拠に揃える。
- 数値はsid/rc/rom_len/matchが十進、crc/chip/expectが16進。未取得値はunknown、matchは1/0/unknownとし、取得失敗・不一致を合格にしない。
- D1：T5無書込みはstatus（済み）とROM/version.chipだけ。BAR/resources・snapshotはS8 A1の既存RECとsnapshot_crcで記録する。
- T5は `rshell_serial.py --port <port> cmd 'gfinit inspect <sid>'` でecho〜EOTを保存。2行の一致・rc=0・match=1を照合し、EOTだけでは合格にしない。
- S7aのhostまたはNP21/W入口確認でhwdevのkprintfがrshell応答へ届くことを検証する。出力欠落は未完了。T5後のIDLE abortもrshell_serialで受ける。
- 新trial（A1/A2）は `rshell_recover.py --session 0 --state <新規パス> --image-crc <CRC> --log <ログ> --command 'gfinit size-bars <現在sid> --allow-write'` 等で開始する。
- 例：現在sid=5ならコマンド引数は5、monitorは0。最初のREC sid=6を採用し、その後のabort_frameも6を使う。初回REC前は既存の0の扱いを維持する。
- A1の証拠を保存し、A2は再び--session 0＋別の新state。PAUSED継続は取得sidを--sessionとコマンドに渡し同じstate/logを保持する。受信者は常に1個。
- monitor変更は `gfinit: rejected` の拒否検出だけ。PARTIAL/overrideは現状どおり失敗終了・証拠保存、OK条件や終端表示は変更しない。

## 4 書込みの op の既定
- op6〜10は毎回--allow-write必須、無指定は呼ばない。inspectは旗を拒否。旗はGF_ALLOW_WRITEへ写すだけで、追加の対話確認・権限分離は設けない。
- T5後はIDLEを前提に `gfinit abort <sid> --allow-write` でleaseを返す。この経路は装置書込みなし。同じabortでもtrial中のcleanupは書込み得るため区別する。
- A1→結果確認→A2、mainごと、A4の承認と立会いは運用。初回0FACh←00h・読戻し・98画面目視もS8で承認を取る。直接書込みopは作らずrecoverの②を使う。
- RUN中は既存abort frame/安全点、PAUSED復旧は既存recover nowまたはGF門のabort。timeoutだけを復帰成功にしない。

## 5 確定設計の書換え (差分)
| 箇所 | 確定する置換・追記文 |
|---|---|
| GF6200_4A_DESIGN §8:207 S7行 | 「SDK共有ヘッダ・ABI版更新」を「private gfinit_wire.h、cmd_gfinit.c・shell登録/ビルド、ROM/chip表示・host。既存40B/op5〜10、KAPI変更なし。S7a/c各1〜2h、S7bは実機確認回」に置換 |
| 同:241 PM注記 | 「recover先例のprivate wire採用、KAPI版・slot・生成元不変。本追記で確定」に置換 |
| 同 §6:163 | 「trial証拠はRECのみ。例外としてtrial外INSPECTは本追記§3の通常1行と終わりの行」を追記 |
| HWDEV_CORE_API §6:140 | 「公開入口は既存kdebug_call、shellはprivate gfinit_wire.hを直接読む。sdk/kapi.json・KAPI版・slot・SDK生成物は変更しない」に置換 |
| 同:227 PM注記 | 「決まるまでは本文を正」を削除し「§6を本追記のprivate wire方針で確定」に置換 |
| TASK_HWDEV §3-1:58 項6（PM反映） | 「書込みなしの部分 (status・version.chip の実 ROM 確認 (snapshot は S8 の A1 の REC))」へ置換 |
- TASK_HWDEVの旧S7行/項4も同期。KDEBUG内部ops版4/20口の説明は維持し、完了済みM5へ遡及しない。

## 6 試験・変異・段
| host試験（各1行） | 検出する変異（各1行） |
|---|---|
| CLIの40B全欄/op/sid/main/4096・旗省略拒否・NOSYSを照合 | op交換、出力欄非ゼロ、旗既定1、NOSYS成功化 |
| 合成ROMの2行・chip/CRC・失敗、INSPECTとIDLE abortの装置write0/lease返却を照合 | chip固定値、不一致成功化、装置書込み追加、解放除去 |
| sid=5→新trial REC=6を--session 0で受け、PAUSED継続/abort_frame=6を照合 | monitor初期sid=5固定、保存sid不使用、stale許可 |
| EOTのみ・REC欠落・gfinit拒否・PARTIAL失敗・PAUSED期限停止を照合 | EOT合格、拒否見逃し、PARTIAL→OK、待機中積算 |
- S7a（1〜2h）：wire・inspect/IDLE abort・2行表示・host・KDEBUG=0/1 build・kprintfのrshell到達確認。生成差0と二重定義なしも確認。
- S7b（実機確認回、実装段ではない）：配備像確認後Ra266でROM/chipを保存、IDLE abortでlease返却。snapshot/restore/0FACh書込みは行わない。
- S7c（1〜2h）：残りopのCLI・trial abort・monitor拒否検出・対応host/両build。S8実測は別の承認済み立会い回。
- 変更対象hostはMUTATE=0、変異はPMレビュー後。NP21/Wは入口確認のみ。本起草では実装・試験を実行していない。

## 7 未決 (ユーザー判断が要るものだけ)
- 0件。実機書込みの承認と立会い日時は運用のゲート。

## 確定 (2026-10-23、Opus r2 条件付き確定可、PM が P3 2 件を決めた)
- C1 (inspect の再実行): 同じ sid の 2 回目の `gfinit inspect` は gfinit.c:309 の重複検査で保存済みの結果が返り `GF inspect` 行が出ない。再実行は `gfinit abort <sid> --allow-write` (IDLE では装置への書込み無し) の後に新しい sid で行う、と運用で決める。結果を出し直すコードは足さない。
- C2 (行の受け持ち): `GF inspect` 行は hwdev が出す (取れなかった欄は unknown)。`GF inspect-end` 行は shell が kdebug_call の戻り値から必ず出す (hwdev が早く失敗して 1 行目が無くても、終わりの行で rc が分かる)。
- 確認事項 (S7a の中): hwdev が import している kprintf が rshell_active のとき serial へ写る経路 (kernel/console.c:261) を通ることを host か NP21/W の入口で確かめる。

## S7a の実装で設計者が決めたこと
- version.chip は既存二重読取ROM内の BIT `i` (version 1) のデータ先頭+2から読む（MIT envytools f102b82381f3f11cee113d16374c87091db039d9、nvbios/bios.c:198,230・bit.c:75–78,124–128・info.c:49–52）；期待値44は REALHW_RECOVER_ADDENDUM のS4取り込み事項に揃え、欠落・範囲外・不一致はinspect失敗とする。
- 二重読取とROM長の検査を通った時点でrom_lenと全ROMのIEEE CRC32を取得済みとし、PCIR/checksum/decode失敗でも表示する；それ以前はunknown、expectは常に44、matchはchip取得時だけ1/0とする。
- inspectの呼出し失敗はinspect-endのrcだけで示し、abort失敗は既存recover先例の `gfinit: rejected rc=`、abort成功は追加行なしとする；API不足/NULLは呼ばずunavailableとする。
- KDEBUGはビルド時の分岐ではなく起動設定なので同じ成果物を使い、hostは実物kernel/kdebug_call.cの未公開opsによるNOSYSと公開opsの両経路を試験する。
- gfinit_wire.hはgfinit.h経由でDWARF収集に含まれるため、generatorのglobから直接includeだけを除き、冗長な1行追加を防いで生成物・inventoryの差0を維持する。

## S8 用の受け方（S7c では実行しない）
ユーザー承認・立会いの下で、PM が各結果を確認してから次のコマンドを選ぶ。
`<現在sid>` は status または直前の応答で得た値、`<取得sid>` は A2 の最初の REC で採用された値。
受信者は常に1個とし、PARTIAL・override は失敗終了のままログ・state を保存する。
以下は別々に実行する形であり、一括実行する台本ではない。

- A1: `python3 tools/rshell_recover.py --port <port> --session 0 --state <新規A1.json> --image-crc <CRC> --log <A1.log> --command 'gfinit size-bars <現在sid> --allow-write'`
- A1 の証拠を保存して確認後、A2: `python3 tools/rshell_recover.py --port <port> --session 0 --state <別の新規A2.json> --image-crc <CRC> --log <A2.log> --command 'gfinit prepare <現在sid> --allow-write'`
- A2 の PAUSED 継続: `python3 tools/rshell_recover.py --port <port> --session <取得sid> --state <A2.json> --image-crc <CRC> --log <A2.log> --command 'gfinit run-one <取得sid> <main> --allow-write'`（main は0〜4、1本ずつ承認・結果確認）。
- 全 main 後は同じ session/state/log で command を `gfinit verify <取得sid> --allow-write` にする。PAUSED の中止は同じ受け方で `gfinit abort <取得sid> --allow-write`。補助命令や ver を挟まない。

## S7c の実装で設計者が決めたこと
- main 範囲は既存 GF_INIT_MAINS=5 を gfinit_decode.h から private gfinit_wire.h に移し、shell と部品で共用する（wire 配置・op・KAPI は不変）。
- size-bars/prepare/run-one/verify は abort と同じく非ゼロ rc を `gfinit: rejected rc=`、ゼロ rc は追加行なしとし、REC の結果判定は既存 monitor に任せる。
