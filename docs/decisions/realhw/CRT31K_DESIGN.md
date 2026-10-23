r1からの変更: 追加処理はCRT31K設定値で限定し、falseのソフトリセットも従来どおりとする。
INT18h AH=30h/31h/42hの修正を除外し、AH=31hの既知不良は別票へ送る。
0597h下位2bitとPRXDUPDの追加書換えを削除する。
bios18.cは変更せず、trueの前提をDIP 1-1高解像とする。
host試験は小ヘッダの純関数・定数表に限定し、実機能の結線はゲスト受入で確認する。

# CRT31K 詳細設計 r2 — 2026-10-23 PM

目的: NP21/Wを31kHz・400ラインで起動し、RECOVER-T4BのS0を試験可能にする。REVIEW.r1.mdの5件をすべて採用。
N=`/home/hight/np21w-src`、O=`/home/hight/os32-v3`、D=`/mnt/c/WATCOM/docs`。実装対象はPC-9821ビルド。

## 1 実物の確認 (file:line)

| 実物 | 設計への拘束 |
|---|---|
| O/docs/tasks/realhw/TASK_REALHW_RECOVER.md:38、docs/tasks/DEFERRED_TESTS.md:54 | T4b未受入。31kHzとHWDEV M5配備が前提。本件だけでT4b完了にしない。 |
| O/kernel/v86.c:1038–1043 | S0は09A8h D0=1、sink無効、高さ400、flip無効が必要。 |
| O/gfx/backend_pegc.c:465–505、1086–1103 | 起動時ポートとBIOS記録を比較。400への戻しは自前pegc_text_sync_400で、INT18h修正は不要。 |
| N/src/io/gdc.c:47–53、1072–1173 | 31k SYNC定数は#if 0。gdc_resetだけでなくgdc_biosresetにも適用が必要。 |
| N/src/io/gdc.c:403、1029–1043、1177–1185 | GDCDISP_31がクロック選択と09A8h読値を決める。gdc_bindでクロック再計算。 |
| N/src/bios/bios.c:158、208–236、692–700、989–1001 | BIOS再初期化でもGDC設定とBIOSメモリ再構築を通る。PRXDUPDのb2/b7は元から0。 |
| N/src/io/cpuio.c:22、src/pccore.c:1413 | レビューのF0h→CPU_SHUT→ITF経路では、falseでもゲストが立てたGDCDISP_31が残り得る。 |
| N/src/bios/bios18.c:146、1024 | DIP高解像なら文字ラスタは既に400側。0597h下位2bitの変更はAH=4Dh経路にも影響するため不要。 |
| N/src/win9x/ini.cpp:593、703、src/pccore.h:363 | DspClockは別用途。USEPEGCP同様、NP2CFGのBOOL鍵を追加。 |
| N/Makefile:57 | 既存test-hostは小ヘッダを使うgcc試験。gdc.c/bios.c全体をhostへリンクする設計は採らない。 |

- D/undocumented/io_disp.md:1865–1893: 09A8h D0=1は31.47kHz。SYNC再設定が必要。
- D/undocumented/memsys.md:3994–4030、1030–1043: 054Ch b5=31k、b6=高解像、0459h b0=480ライン。400/31kは70.10Hz。
- D/PC9800Bible/3-2_グラフィック256色表示.md:71–106の09A8h=1/HFを参照。「垂直」「31k=480限定」の記述はUNDOCUMENTEDを優先する。
- 別票へ送る既知不良: N/src/bios/bios18.c:567の`>>(7-5)&4`は31kでもrate=08hとなる。本件では修正しない。

## 2 鍵とリセット時の状態 (コード断片)

| 項目 | 決定 |
|---|---|
| 鍵・型・既定 | `[NekoProject21] CRT31K=false`、PFTYPE_BOOL。NP2CFG末尾にUINT8 crt31kを追加し、静的末尾ゼロ初期化で未指定false。 |
| 適用条件 | SUPPORT_PC9821かつSUPPORT_CRT31KHZ。true運用はDIP 1-1高解像が前提。DIPの自動変更・互換対応は追加しない。 |
| true | 31.47kHz・640x400・標準グラフィック・GDC 2.5MHz。ゲストによる後続の周波数変更は従来どおり。 |
| false | 既存経路をそのまま通す。通常DIPで新規起動は24k。ゲスト変更後のソフトリセットも従来との差分を作らない。 |
| BIOS | 054Ch b5はCRT31K設定値から付加。b6は前提DIPで既に1。0459h b0はtrue時のみ0。0597h・PRXDUPDへの追加書換えなし。 |

```c
/* 新規src/io/crt31k_reset.h: OS・CPU・I/O依存なし、hostと製品で共有 */
static const unsigned char crt31k_m[8] = {0x10,0x4e,0x47,0x0c,0x07,0x0d,0x90,0x89};
static const unsigned char crt31k_s[8] = {0x06,0x26,0x41,0x0c,0x83,0x0d,0x90,0x89};
static const unsigned char crt31k_mode1 = 0x88;
static const unsigned char crt31k_mcsr[3] = {0x0f,0xc0,0x7b};
static const unsigned char crt31k_scsr = 0;
static unsigned char crt31k_prxcrt(unsigned char base, int enabled) {
    return enabled ? (unsigned char)(base | 0x20) : base;
}
/* win9x/ini.cpp: s_IniItems[]に追加 */
PFVAL("CRT31K", PFTYPE_BOOL, &np2cfg.crt31k),
/* bios.c: 既存PRXCRT算出後、代入前。現在GDCDISP_31は参照しない */
prxcrt = crt31k_prxcrt(prxcrt, np2cfg.crt31k);
```

- gdc_biosresetの既存DIP分岐の前にtrue枝を置く。GDCDISP_31を立て、crt15khz=0、上表のmode1・CSRFORM・両SYNCをコピー。falseは既存分岐へ。
- gdc.cの無効な31k定数は共有ヘッダへ移す。slave先頭06は既存reset流儀。後続のモノクロ指定、clock=0、pitch=80/40、パレット初期化は既存共通処理を利用。
- true時だけ256/256Eを解除し、共通初期化後にgdc_updateclockを呼ぶ。gdc_resetとBIOS ITFの双方で31k状態を作り直す。
- bios_reinitbyswitchで上のPRXCRT算出と、true時だけ`mem[0x459] &= ~1`を行う。0597h b7/b2、045Ch b6など既存能力設定は維持。
- 追加鍵参照・追加処理は上記ビルド条件で囲む。bios18.cは無変更。INT18hの既定rateも現状のままで、S0受入には使用しない。

## 3 ini_live の操作と試験

| 対象 | 変更・運用 |
|---|---|
| OPERATIONS / DERIVE_OPERATIONS | `crt31k-on: {'CRT31K':'true'}`、`crt31k-off: {'CRT31K':'false'}`。他鍵は変更しない。 |
| 共通変換 | EXTRAにCRT31K、_extra_valueにtrue/false検証を追加。ALLOWEDには入れず既存操作・receiptを維持。 |
| 初回移行 | PMが更新前原本を保存。新フォークの通常ini保存で鍵を生成してから操作。欠落時は通常保存を案内し、自動挿入しない。 |
| 運用・復元 | PMのみpreview→既存exclusive条件で--live-apply→receipt保存→試験→restore。offはfalse指定、restoreは操作前全バイト復元。 |

- test_np21w_ini.py: on/off往復、大小文字・CP932・CRLF・コメント・無関係バイト保存、欠落/重複/不正値拒否、旧fixtureで既存操作成功。
- test_np21w_ini_live.py: preview無変更、apply/readback/receipt/restore一致、off≠restore、既存排他・停止確認・差替え検知の回帰。

## 4 確認の手順と変異

1. 段Aでヘッダのhost試験と製品ビルド、段Bで`python3 -m unittest discover -s tools/tests -p 'test_np21w_ini*.py'`を実行。
2. PMが実行体識別子・ハッシュ、DIP 1-1高解像、原本を記録しcrt31k-onを適用。resumeを使わず新規起動する。
3. 新規起動・通常リセット・F0hソフトリセットのboot.logを保存。`[pegc] hsync=31k`、054Ch b5=1、0459h b0=0、MISMATCHなし、GDCクロックと054Dh b2一致を確認。
4. 09A8h読値とBIOS再通過の整合は上のログ、gdc_updateclockの結線は31k SYNC・内部タイミングのゲスト受入時観測と表示で確認する。host合格だけで結線済みにしない。
5. CUIの文字・スクロール・カーソル、recover statusのS0可を確認。PEGC GUI480→自前同期のCUI400を往復し、乱れなし・復帰後S0可を記録する。
6. HWDEV M5配備後にrecover now 0のPARTIALと98画面復帰を採取。GUI480中のS0不可は正常。他のT4b項目は別途受入する。
7. restore後の新規起動、off、鍵なしで従来の24kとS0不可を確認。falseでゲスト09A8h=1→F0hの結果は旧版と比較し、BIOS記録を新たに変えないことを確認する。

| host試験 (1行1件) | 変異と検出 |
|---|---|
| ヘッダの両SYNCを独立した期待16Bと比較 | 片側の1byteを24k値へ変更すると失敗。 |
| mode1・master/slave CSRFORMを期待値と比較 | mode1=98hまたはslave CSRFORM=1への変異で失敗。 |
| prxcrt(base,false)=base、true=base\|20hを全256値で確認 | 無条件OR、OR削除、他bit変更で失敗。 |
| ini新操作と旧fixtureを実行 | CRT31KをALLOWEDへ移すと旧操作が欠落エラーで失敗。 |
| ini不正値とreceipt復元を実行 | BOOL検証削除、restoreをoffに置換すると失敗。 |

## 5 ファイルごとの変更一覧

| 段 | ファイル | 変更 |
|---|---|---|
| A フォーク | N/src/pccore.h、src/win9x/ini.cpp | 末尾設定とBOOL鍵。既定false。 |
| A フォーク | N/src/io/crt31k_reset.h (新規)、src/io/gdc.c、src/bios/bios.c | 小表・純関数、true reset枝、設定値からのBIOS記録。bios18.cは変更なし。 |
| A フォーク | N/Makefile、host用crt31k_reset_test.c (新規) | 既存test-hostへヘッダのみのgcc試験を追加。 |
| B os32-v3 | tools/np21w_ini.py、tools/np21w_ini_live.py、tools/tests/test_np21w_ini.py、test_np21w_ini_live.py | 定型操作と変換・復元試験。カーネル/GFXは変更なし。 |
| C PM受入 | TASK_REALHW_RECOVER.md、DEFERRED_TESTS.md | 証拠リンクと31k前提完了のみ更新。T4b残件は維持。 |

## 6 未決 (ユーザー判断が要るものだけ)

- 0件。実装・試験・別票起票は未実施。本書のみ作成し、両リポジトリとiniは変更していない。

## 確定 (2026-10-23、Opus r2 確定可、PM が P3 3 件を決めた)
- C1 (0459h b0): `mem[0x459] &= ~1` は書かない。bios_memclear (bios.c:170) が毎回 0 にし、立てる所が無い。受入の boot.log で bios0459.b0=0 を確かめるだけ。
- C2 (ヘッダの形): crt31k_reset.h の関数は `static __inline`、表は gdc.c だけが含める (bios.c は PRXCRT の関数だけを使う。表を `#ifdef CRT31K_TABLES` などで分けるか、表を gdc.c 側に置くかは実装者の小さな判断)。host 試験はヘッダの表と関数を直接読む。
- C3 (§4-7 の F0h の false 比較): ゲストの手順は外す。false の経路でコードの差分が無いこと (追加の参照はすべて `np2cfg.crt31k` の条件の中、bios18.c 無変更) を実装のレビューで確かめる。
- 別票へ送る: bios18.c:567 の `>>(7-5)&4` (GDCDISP_31=7 で AH=31h の rate が 31k でも 08h)。np21w-src の送り項目として記録。
