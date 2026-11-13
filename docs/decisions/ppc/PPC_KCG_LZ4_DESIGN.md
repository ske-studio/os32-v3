# PPC_KCG_LZ4_DESIGN — KCG1 のフォントを LZ4 で圧縮して RAM root を減らす (L0 生成・L1 読み込み)

> 状態: **現行 (2026-11-13)** — KCG1 の flags=1 を単一の raw LZ4 block とし、生成は tools/gen_font16.py、読み込みは drivers/video/kcg/kcg_file.c が起動時に 1 回だけ伸長する。初段は PowerPC の RAM root。x86 は KCG1 を収録しないので生成物は変わらない。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/fb-route/DESIGN.final.md (astra 起草 r1→r2、本線 PM 経由の Opus 第 2 回 条件付き確定、PM 反映)。中身は変えずに写す。前提の FB 文字端末は [PPC_FBCON_DESIGN](PPC_FBCON_DESIGN.md)。§5 の console_route は段 I へ送った将来案で、確定した契約ではない。
2026-11-13。設計のみ。作業木は読み取り専用、実装・生成・ビルド・実機受入は未実施。

## r1→r2 の変更
- F1: console_route の更新呼び手の検討を段 I へ送る。
- F2: 最早期の静的既定行を段 I へ送る。
- F3: route 関連 host 試験の所有を段 I へ送る。
- F4: con_sink_render_gate への影響を段 I へ送る。
- F5: PPC の stream 直呼び移行を段 I へ送る。
- F6: L1 の R6 後という前提を削除し、build 編集順は PowerPC PM が管理する。
- F7: PPC 専用 object に -O0 を指定し、EBDT の argv 分岐を下記に具体化する。
- F8: 二度目の load 拒否を削除し、検証成功後に旧 resident を解放する既存動作を維持する。
- F9: キュー・新設定・viewport を今回の対象から外す。
- F10: 受入・変異の各行に未変異の正常対照を明記する。
- D1: CONSOLE_DUAL は .inc で結線済みと訂正し、結線の追加を行わない。
- D3: CONSOLE_DUAL_DESIGN:50 の「PPC は実体なし」を訂正対象にする。

## 1. 今回の契約
対象は L0（生成）、L1（loader と収録）、文書訂正2点。受け手は PowerPC、共通差分は本線と照合する。
console_route・段 I・PLANAR4・FD フォント収録・実行中切替コマンドは今回実装しない。
以下の既存仕様・容量値は r1 とレビューを引き継ぐ。再確認は依頼で指定された関係行に限った。

| 欄 | 契約 |
|---|---|
| header 16B | magic=KCG1、展開長 P=295,940（LE u32）、flags、予約0 |
| flags=0 | body=P、全長295,956B。従来形式を引き続き受理 |
| flags=1 | body全体が単一 raw LZ4 block。frame・サイズprefix・独立分割blockなし |
| 拒否 | 未知flags、予約非0、P不一致、短header、その他の不正データ |
| payload順 | 漢字282,752B、fetched8,836B、ANK4,096B、fetched256B。fetchedは全て1 |

- file_size>=16 を先に確認し、C=file_size−16 を求める。flags=1 は 0<C<=P+P/255+16=297,116。
- 新しい伸長器は作らず、lib/lz4.c の lz4_decode(src,C,dst,P) を使う。戻り値は厳密にPを要求する。
- font_guard と resident lease を維持する。描画中の確保・I/O・遅延伸長は導入しない。
- header検査→出力P確保→圧縮時のみ入力C確保→全量read→lease validate→decode→fetched検査→close成功→差替え。
- flags=0 は出力に直読。入力Cは公開前に解放する。失敗時は新入力・新出力・leaseを回収し、旧residentを保持する。
- 成功時だけ既存の kcg_file.c:48–50 と同じ順で旧resident解放→新resident代入→counter reset。再ロードを拒否しない。
- bootの呼び手は init/ppc_kernel.c:345 の1か所。font成功後のunicode/target/FB準備順を維持する。
- 初回loadの字形用最大同時確保はP+C=593,056B、再ロードは旧P込み888,996B＋管理分。他用途を含む1MiB arenaの高水位を測る。
- 確保不能時はエラーで戻り、既存streamを継続する。公開kcg_load_fontのNOSYS、x86のROM/module選択は不変。

## 2. L0 — 生成（受け手: PowerPC）
触るファイル: tools/gen_font16.py、build/font-fetch.mk、生成試験（tools/tests配下、CHECK入力宣言を含む）。
TTF/EBDTのpayloadを共通 pack_kcg(payload, compression) に渡す。明示LZ4は lz4.block の store_size=False。
EBDTは現行の3引数形式を維持し、末尾に --lz4 / --no-lz4 を任意で1個許す。argvの受け方は次の形にする。
```python
args = sys.argv[1:]
if args and args[0] == '--ebdt':
    from gen_g16 import G16Error
    if len(args) not in (3, 4) or (len(args) == 4 and
            args[3] not in ('--lz4', '--no-lz4')):
        print('usage: gen_font16.py --ebdt INPUT OUTPUT [--lz4|--no-lz4]',
              file=sys.stderr)
        return 2
    compression = 'lz4' if len(args) == 4 and args[3] == '--lz4' else 'none'
    try:
        data = pack_kcg(build_ebdt_payload(args[1]), compression)
        with open(args[2], 'wb') as output:
            output.write(data)
    except (G16Error, OSError, ValueError, ImportError) as exc:
        print('gen_font16: %s' % exc, file=sys.stderr)
        return getattr(exc, 'rc', 2)
    return 0
# この後は既存TTF引数処理。入口は sys.exit(main()) として失敗を伝える。
```
build_ebdt_payload は現行build_ebdtのpayload構築部を分離した内部関数。`build_ebdt(path)` は `pack_kcg(build_ebdt_payload(path), 'none')` を返す形で残す（tools/tests/test_kcg_ebdt.py:30 が header 付き全バイトを期待するため、G1）。TTFの既定fallbackと --no-lz4 は維持する。
明示 --lz4 の依存不足は失敗。圧縮bodyがP以上ならflags=0に戻すが、依存不足を無圧縮成功で隠さない。
生成に使うlz4版を固定して再現性を確認し、形式・サイズ・SHAを記録する。
build編集順: ①生成器と試験を先行 ②L1のloader受入完了 ③font-fetch.mkの製品EBDT呼出しへ --lz4 を追加。
L0単独では非圧縮製品を保つ。圧縮製品への切替とL1収録を同じ配布単位にする。

| 受入 | 正常対照（未変異でPASS） | 変異（実行時RED） |
|---|---|---|
| argv互換・失敗伝播 | 旧EBDT、両option、TTFが成功。不正個数/optionは非0終了 | EBDT分岐を旧len条件へ戻し、--lz4実行が失敗 |
| 圧縮生成 | 同一EBDTの0/1を実物C decoderで展開し全payload一致。圧縮可能fixtureはflags=1 | EBDTの圧縮指定を無視しflags期待が不一致 |
| 依存・膨張 | lz4ありで成功、明示指定で依存なしは失敗、膨張fixtureはflags=0 | 依存不足を成功扱い／膨張時の形式選択を反転し個別RED |
| 再現性 | 固定版・同一入力の2回生成でbytesとSHA一致 | 予約欄を非0にして期待header検査がRED |

## 3. L1 — loader と収録（受け手: PowerPC）
触るファイル: drivers/video/kcg/kcg_file.c、lib/lz4.cの冒頭注記、build/ppc-kboot.mk、loader/統合試験。
-O0は維持する。理由: lib/lz4.c:13–15の最適化不具合の注意を解除する根拠がないため。
ppc-kboot.mkのKB_Cへlib/lz4.cを追加し、専用objectに次の1行を置く（program用objectを流用しない）。
```make
$(KB_OUT)/lib/lz4.o: KB_TU_FLAGS += -O0
```
既存compile recipeは $(KB_FLAGS) $(KB_TU_FLAGS) の順なので、この指定を最後の最適化指定として有効にする。
lib/lz4.c:9–13の注記を「programとPPC kernelの専用objectへリンク、両者の最適化設定は各build規則を参照」に訂正する。
実物programs.mk:207–208はPROGRAM_FLAGSを使用し、専用-O0は直接書かれていない。注記の「設定済み」は転記しない。
build編集順: ①loaderと試験 ②ppc-kboot.mkのソース追加・上記TU指定を一緒に編集 ③PPCビルドと0/1受入 ④L0製品切替。
完了条件に追加: KB_FLAGS（-Wall -Wextra -Werror）と -O0 で lz4.c に警告が出たら lz4.c の側を直す（G3）。
R系の完了は前提にしない。ppc-kboot.mkの他作業との順序・所有はPowerPC PMが調整する。
既存build/ppc-ram-root.jsonのフォント収録パスとライセンスは維持し、生成済み圧縮ファイルを既存経路で収録する。

| 受入 | 正常対照（未変異でPASS） | 変異（実行時RED） |
|---|---|---|
| 0/1互換 | 両形式のpayload全量一致、合法な重複マッチと膨張blockも成功 | 圧縮分岐を無効化／マッチコピーを壊して個別RED |
| header・長さ | 正しい0/1は成功。短header、P不一致、未知flags、予約、C境界違反は拒否 | 対応する検査を1個ずつ外し、不正fixtureの誤受理でRED |
| decode・fetched | 正常字形は成功。offset0/範囲外、出力過不足、fetched不正は拒否 | 戻り長検査／fetched検査／decoder境界検査を個別に外してRED |
| 回収・再ロード | 初回と2回目が成功。alloc/read/validate/close失敗時は旧resident保持、リーク0 | free省略／close前公開／再load拒否を個別に入れRED |
| PPC統合 | 非圧縮・圧縮の日本語/全角端/scroll/cursor画素が一致し、予約上端とLAN/serial維持 | font成功経路を失敗にして圧縮像の画素期待がRED |
| 容量・配布 | 実物buildのroot/ISO差分、heap高水位を記録し上限内。超過fixtureは検査で拒否 | 上限検査を削除して超過fixtureが誤通過しRED |

- r1参考実測: 295,956→187,980B、削減107,976B。ISOファイル占有の2048B丸めでは106KiB減。正式生成で再計測する。
- resident Pは不変、初回入力Cは一時増。root＋resident参考値は591,896→483,920B。コード増はリンク/mapで測る。
- x86は現行KCG1をFD/MINIMALに収録しないためフォント由来の空き増分0。route由来のコード増・module再pack見積りは削除する。
- 各試験は実物実装を使用し、CHECKに入力を宣言。各変異anchorは一意、コンパイル失敗ではなく実行時REDを確認する。
- 実装時は指定baseでpreflight、該当PPC像・共用生成器の影響先build、追加変更試験--mutate、lintを実施する。既存anchor移設も確認する。
- check-changed/full mutationは走らせず指定CIへ。今回の設計作成ではpreflightの修正操作・build・試験は実施しない。

## 4. 文書訂正2点
| ID・対象 | 訂正内容・確認 |
|---|---|
| D3: docs/decisions/gui/CONSOLE_DUAL_DESIGN.md:50 | PPCはplatform/powermac/display_dual.cに実体あり、と古い注記を更新する |
| D1: PM事前確認の記述 | init/kernel_gfx_config.inc:18でconfigure済み、kernel.c:208,309で呼出し/include済み。本設計を訂正記録とし、PM管理の元記録も訂正する |
文書訂正はPowerPCから本線PMへ渡す。D1を製品の結線欠落と扱わず、cfg読込みの追加実装はしない。

## 5. 段 I へ送る
| ID | 中身 |
|---|---|
| F1 | x86のconsole_route_update呼び手の表。今はcon_dual_activeがkey変化ごとにdisplay_dual_outputsをやり直す |
| F2 | 最早期の静的な既定の行 |
| F3 | 約15本のhost試験の持ち主 |
| F4 | kselftestのcon_sink_render_gateへの影響 |
| F5 | PPCのconsole_stream_write直呼び（cli_boot・shell_boot・shell_putchar・kboot_console） |
| F9 | 今の要件に無い物（256Bキュー・新設定・viewport） |
r1 §2の要旨（将来案であり今回の確定契約ではない）:
- DUAL内部表を単一route表へ移し、GRID/STREAMと物理画面の役割を扱う。
- con_outの8B互換を保ち、backendの選択・世代・安全性をdispatchで確認する。
- primaryのreadと共通論理寸法を用い、準備成功後に選択を切り替える。
- sink/bootlogは1回、streamは画面の門の前、GRIDはGUI/V86の既存安全条件を守る。
- TVRAMの早期出力、PPCのFBとLAN/ESCC、失敗時のstream継続を保つ。
- 段 IでTVRAMとFBへ同時出力するかをユーザーに尋ね、上記案を必要な範囲で起こし直す。
- 実行中切替コマンドの未決は撤回する。PLANAR4は別票とする。

## 確定 (2026-11-13、本線 PM 経由の Opus 第 2 回 Approve 条件付き、PM が反映)
- G1 (P2): §2 に build_ebdt の互換の形を追記。
- G2: 確認のみ、直しなし。
- G3 (P3): §3 の L1 の完了条件に lz4.c の警告の扱いを追記。
