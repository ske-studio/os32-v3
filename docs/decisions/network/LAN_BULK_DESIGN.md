# LAN_BULK_DESIGN — LAN の一括転送 (link の新しい op、/host の LAN 経路を link へ一本化)

> 状態: **現行 (2026-11-14)** — 確定 (astra 起草 r0〜r2、Opus 5.5 の独立レビュー 2 回、ユーザー参加の往復、残りを PM 決定で反映)。段の状態は票 TASK_LAN_BULK が持つ。
> 出所: 往復の版は `~/os32-tmp/run/lan-bulk/` (ISSUES.r0) と `~/os32-tmp/run/lan-bulk-r1/` (DESIGN.r1・r2・final、REVIEW.r1)。Tier との口は IO_TIER_DESIGN。

- 2026-11-14。設計のみ。実装・配備・実測は行わない。
- 照合基点: origin/main `9e39176a`（/home/hight/os32-v3 の git show）。
- PACK.r1.md、ISSUES.r0.md と指定された現行ファイルの該当箇所を照合。保存先はユーザー指定により lan-bulk-r1 に変更。 r1の関係箇所だけを改訂。REVIEW.r1 / R2_REQの決定を優先。新しい口・opは実装済みを意味しない。
- 読取り限定の設計依頼につき preflight --fix-fonts、ビルド、lint、変異試験は未実行。作業木は変更しない。
## r1→r2 の変更
- F1: x86のSW RX/NICをC=16〜22＋制御1へ拡張する段とRAM増分、GEMの制約・調査根拠を追加。
- F2: OPENをVFS呼出し単位に固定、累積closed_ridで台帳を回収。
- F3/R4: linkディスパッチのFG移行をLAN独立段とし、KAPIとの排他を明記。
- F4: VFS待ちからRX/lan_bulk_stepを直接駆動し、PPCのEEを限定復元、HLTしない。
- F5: grantを転送全体の通し番号にし、burst境界の全ACK待ちを撤去。
- F6: ゲスト保存は通し番号順、穴の後ろはRAM保持。
- F7: ゲストディスクへの一括コピーを初版の遅延合否対象から除外。
- F8: io_slice/IO_DONE/MORE/WAIT/io_clock_usへ統一、bulk_work.cをLAN所有、io-tier D6は削除する契約。
- F9: 1枚37〜104µsの推定から初期予算440µs/4枚をLAN側で定義。
- F10: Bを送受信2ファイルへ分割、状態・遷移・C実体のhost試験を追加。
- F11: NIC取り込みは既存FG、IRQ0に残るlink dispatch/82557救済を区別。
- F12: 照合基点をorigin/main 9e39176aへ変更。
- F13: 1フレーム以内のreadはOPENEDに本文とclose結果を同梱、CLOSEを省略。
- U1/U2: 共通遅延合否を確定、同じ道具で非転送基準を先に測定。Tier初版は案B（FGのみ）。

## 1. 決定と r0 の閉じ方
|決まったこと|r0|
|---|---|
|本文領域1024B固定、末尾はlenで有効長を示す|Q4 本文長を閉じる|
|輻輳制御なしで実装し問題を測る、受け手creditは必須|Q5 方針を閉じる|
|同一セグメント内のみ、外へはホストサービス経由|Q5 適用範囲を閉じる|
|/host の全LAN操作をHost Services、link 0x88B5の新opへ統合|Q1 を閉じる|
|旧LAN SFS形式は互換もfallbackも作らず、HostDrvの機能は残す|Q7 LAN移行を閉じる|
|serial SFSは現形式のまま、リンク未確立時の逃げ道、一括転送外|Q7 serialを閉じる|
|OPENでパスを一度送り、以後ID。フレームACKとclose完了応答を分離|Q10 操作単位を閉じる|
|書込み完了はホストOS受理＋close、fsync不要|Q10 完了意味を閉じる|
|offsetはu32、バーストACKに書込み失敗を載せる。TRUNC・作成・closeは一度|Q6/Q10 を閉じる|
|creditはホストpwrite／ゲスト保存先への書込み完了で返す|Q5/Q10 返却時点を閉じる|
|速度と入力・表示遅延を受入、100Mbpsで8.75MB/s。共通合否はp95≤50ms・max≤100ms、非転送との差も記録|Q9 数値も閉じる|
|Tier初版は案B（FG協調点のみ、IRQ出口なし）。一括転送はTier2、µs予算でフレーム境界復帰|Q8/Q9 境界を閉じる|
- Q2/Q3、Q4のN、Q8の換算は以下の推奨で具体化。測定後の調整は実装判断とする。
- セキュリティ、9801互換、固定CPU配分率は扱わない。整数演算のみ。

## 2. 線上形式と操作の寿命
- Ethernetの型だけBE、既存link 20Bと以下の整数はLE。byte accessorで直列化する。
- sess/epoch/peer検証を共有。新opは既存DATA/WDATAのGBN・8KiBリング・256B creditと別窓。
- 提案op: 12 OPEN、13 OPENED、14 BULK_DATA、15 BULK_ACK、16 CLOSE、17 CLOSED、18 PROBE、19 FS_META、20 FS_RESULT。
- 新opのflags/seqは0。ackは要求側が受領済み終端結果（CLOSED、FINAL/失敗OPENED、FS_RESULT）の累積closed_rid、応答側は0。ridはOPEN/OPENEDで要求ID、他はホスト採番の非0転送ID。
- OPEN.ver=1。LISTのos32.fsにbulk-v1を広告。未対応は明示エラー、旧LAN SFSへ戻さない。
- 以下は線上配置を示す記述用構造体。byte配列なのでpaddingなし。実装ではパケットを構造体castしない。
```c
struct BulkOpen { u8 ver, direction, flags, nmax; /* 16B＋path、@0: dir 0=host→guest,1=guest→host */
    u8 offset_le[4], size_le[4];    /* @4,@8: 開始位置・要求長 */
    u8 path_len_le[2], rx_slots_le[2]; /* @12,@14: path 1..255B(NULなし)、枠1..32 */
}; /* pathが続く。flags bit0=CREATE, bit1=TRUNC。rid=要求ID */
struct BulkOpened { u8 status_le[4], id_le[4], size_le[4]; /* 20B＋本文、@0,@4,@8: status/ID/実長 */
    u8 nmax, mode, rx_slots_le[2];  /* @12,@13,@14: mode bit0=FINAL_INLINE */
    u8 data_len_le[2], reserved[2]; /* @16,@18: 0..1024、予約0。本文が続く */
};
struct BulkData { u8 burst_le[4], offset_le[4], len_le[2], k, n; /* 1036B、@0,@4,@8,@10,@11 */
    u8 bytes[1024]; /* @12: len=1..1024、末尾余白0、k=0..n-1 */
};
struct BulkAck { u8 base_frame_le[4], received_le[4], written_le[4]; /* 24B、@0,@4,@8 */
    u8 grant_le[4], status_le[4], failed_offset_le[4];  /* @12,@16,@20 */
}; /* base未満は累積written、bitmapはbase相対、grantは排他的右端、status負で失敗 */
struct BulkClose { u8 abort_le[4]; }; /* 4B: 0=正常、1=中断 */
struct BulkClosed { u8 status_le[4], written_bytes_le[4]; }; /* 8B: @0,@4 */
struct BulkProbe { u8 base_frame_le[4]; }; /* rid!=0は4BでACK再提示、rid=0は本文0B */
```
- burstは0始まり。stride=nmax×1024、offset=OPEN.offset+burst×stride+k×1024。
- n=min(nmax,残りフレーム数)、lenは最後だけ短い。加算前にsize≤UINT32_MAX-offsetを検査。
- 最大ファイル長0xffffffff B、u32を周回させない。VFSのint戻り値を超える単一呼出しは分割する。
- READは開始時長で範囲を固定、途中短縮・読出し失敗はエラー。スナップショット保証はしない。
- OPENはVFS read_stream/write_stream等の呼出し1回が単位（ファイルopen寿命ではない）。同時1転送、完了後に次を許可。cpは1回read完了→writeで2本常駐を要さない。
- OPENの鍵=(sess,epoch,要求ID)、本文も照合。重複は同じOPENEDを返しTRUNC/CREATEを再実行しない。
- 同一世代で転送IDを再利用しない。失敗OPENも記録、ID枯渇時は新規受付を止めて世代更新。
- receivedはRAM受理、writtenはpwrite_all成功／ゲスト保存完了。receivedだけで送り手の再送本文を解放しない。
- ホストは1KB単位でpwrite_all、短いwriteは残りを継続。途中失敗は即ACKに載せ、次burstへ進まない。
- ゲスト保存はnext_writeから順に実行し、穴の後ろはRAM保持してwrittenにしない。一括コピーはディスク保存、通常/host readは呼出し側bufferへの順次引渡しでcredit返却。
- 全written後にCLOSE。ホストはclose結果をCLOSEDに載せ、closeエラーを成功にせず再closeもしない。
- 中断は未完ファイルを残し部分バイト数を返す。自動再OPEN/TRUNC・自動serial切替はしない。
- 要求ridはbulk/META共通の非周回連番。CLOSED受領を次の要求のack=closed_ridで累積通知し、ホストはwatermark以下の台帳を回収（飛行中＋直前の1〜2件）。古い要求はwatermarkでSTALE、再実行しない。 次要求がない時もrid=0・本文長0のPROBEを「closed_rid通知だけ」として送れる。転送ID付きPROBEは従来どおり本文4B、rid/長さの不一致は拒否する。累積値は連続完了済みまで、未発行/未完了ridを越す通知は拒否。300KBを1KBずつ読む300呼出しもBUSYにしない。
- READ実長≤1024BはFINAL_INLINE OPENEDに本文とclose後statusを載せ、CLOSE不要。失敗なら本文なし、0Bも対応。再送OPENへ同じ本文/結果を返すため最大1KBを回収まで保持。WRITEは通常手順。
- 再起動・世代変更はSTALE。永続的exactly-onceは保証せず、完了不明を返す。旧IDで新規操作を起こさない。
- /hostのstat/list/mkdir/rmdir/unlink/rename/syncもFS_META/RESULTへ移す。METAはver u8, kind u8, reserved u16, count u32, cursor u32, bodyの順、整数LE、pathはu16長＋bytes。
- RESULTはstatus i32＋next_cursor u32＋件数u16＋予約u16＋長さ付きレコード、全体≤1400B。listはページ化。METAは既存rid台帳で重複抑止、200ms再送。
- HostDrv利用側機能・常駐/host・hsyncを新transportへ接続。serial SFSとネットCDを退行させず、旧LAN SFS利用者を列挙してから旧経路を撤去する。
## 3. 選択再送・credit・優先順位
|項目|推奨|
|---|---|
|方式|最大32枠のスライド窓＋bitmap選択再送。burstは番号付けだけでACK待ち境界にしない|
|N/C|N=1..32、標準8。C=1..32は独立、Ra266は16/22比較。N=8でも複数burstを飛行させる|
|RAM|送信/受信ともC×(1024+約16)B、C=3/16/22で各3.05/16.25/22.34KiB。1転送の逆方向用領域は共用可|
|追加RAM|base/grant/bitmap等≤128B、台帳2×約64B＋短read再提示1024B。NIC/SW queueは別|
|再送|NIC受理から200msで未receivedだけ、5回無進捗で停止。TX未受理は1秒、新着雑音で延長しない|
|保存待ち|received済みは200msごとPROBE、5秒written無進捗で失敗。ゼロcreditも同じ|
|ACK|保存2枚ごと／grant使切り／穴／失敗／PROBE／転送末尾で即時、他は最大1ms|
- 通し番号f=burst×nmax+k。total=ceil(size/1024)、baseは先頭未written、grant=min(total,base+C)。初期grant=min(total,C)、送信はf<grantのみ。N境界でも止まらない。
- ACKはbase未満を累積保存済み、baseから32bitを選択状態として表す。旧baseのACKは絶対番号へ戻してマージ、bitmap/累積状態を後退させない。grantは最大値で統合し加算しない。
- base≤grant≤total、written⊆received、ACK対象は送信済みであることを検査。転送ID/世代違いは拒否。消費前に枠を解放せず、再送で権利を二重消費しない。
- 受信重複は再保存せず現ACKを再提示。ゲストはnext_write順、ホストも穴がある間grantを先送りしないためC枠以内。最後のACK消失はPROBE/重複DATA、CLOSED消失は同じCLOSEで回復。全written後にCLOSE、closed_ridまで完了応答を保持し回収後もID水位を残す。
- 制御とrshell/gshell通知を先に選択、bulkは1フレームごと再選択。TX FIFOへbulkを大量予約しない。
- NIC/SW RXとも制御用最低1枠（NE2Kは最大フレーム6ページ）確保。既存DATAとbulkは共通allocatorから容量予約し二重広告しない。
- 初版はbulk中の非制御DATA窓を絞る。通知ACKはbulk creditから独立（現link.c:256と整合）。
- 制御連続時も8フレーム選択ごとにbulk1枚を候補に戻す。Tierでの最低機会は将来scheduler側が保証する。
```c
struct bulk_tx { u32 total, base, next, grant, recv_bits, done_bits, due_us; u8 state, retries; /* C個の再送slot */ };
struct bulk_rx { u32 total, base, next_write, grant, recv_bits, done_bits, due_us; u8 state; /* C個の保持slot */ };
/* bitmapは各base相対、期限はio_clock_us、相対期限<2^31µs。本文はwrittenまで保持。 */
```
|状態→事象|遷移・所有者|
|---|---|
|OPEN_WAIT→OPENED|TX/RX初期化、INLINEならDONE。重複OPENはホスト台帳で結果再提示|
|ACTIVE→DATA/ACK|RXは検証/保持/順次保存、TXはbase/grant更新・未送信補充、双方ACTIVE|
|ACTIVE→RTO/保存失敗|欠落だけ再送、上限/IO失敗はFAILED。全writtenでCLOSE_WAIT|
|CLOSE_WAIT→CLOSED|DONE、次要求でclosed_ridを通知。期限なら同CLOSE再送、世代変更はSTALE|


## 4. CPU と線上の予算（推定、実測値ではない）
- 8,750,000B/s=8545 DATA/s。266,000,000/8,750,000=30.4 clk/BがCPU全量の上限。
- DATA線上長=14+20+12+1024+4+8+12=1094B、本文効率93.6%。ACKは最小84B、2枚に1ACKなら合算約96.4Mbps。
- 1枚に1ACKでは合算約100.5Mbps、半二重の目標を超える。全二重では方向別帯域も記録する。
- LANの追加CRC32は省く。Ethernet FCSをNICが検査する前提で媒体破損を検出する。RAM/ソフト破損まで保証するものではない。serial CRCは維持。
|処理|仮予算clk/B|検証するもの|
|---|---:|---|
|NIC/DMA buffer→SW frame→bulk slot→保存先buffer|3コピー×1..3=3..9|実経路のコピー数、cache冷温、借用buffer寿命|
|PIO/DMA管理|DMA 1..3、PIOは別実測|NE2KのI/O待ちをmemcpyから推測しない|
|ヘッダ検査・bitmap・ACK生成|1500..3000clk/frame≒1.5..2.9|異常経路も測る|
|IRQ入口・回収|1000..4000clk/IRQ≒1..4|1IRQ/frame仮定、RX/TX別IRQなら増加|
|保存先FS処理・協調切替|3..8|同期ディスク待ちとCPU busyを区別|
|DMA合計|約9.5..26.9|残り3.5..20.9clk/Bを入力・表示等へ、保証値ではない|
- TXは読出しbuffer→再送slot→送信frame→NICの2..3コピーを計上。両方向を別測定。
- 1DATA/IRQなら約8545 IRQ/s、ACK/2もRX IRQなら約12817/s、別のTX完了IRQがあればさらに増える。
- 100Hzだけの駆動でC=3なら最大約307kB/s。短周期化後はコピー・PIO・ディスク・Python送受信が律速候補。
- C=3で目標達成に必要な補充周期≤351µs、C=8でも≤936µs。旧2.2ms周期が残れば8枚で約3.72MB/s。
- 合計9.5..26.9clk/B×1024/266≒37..104µs/枚（概算を丸め約40..110µs）。初期budget=440µs、max_units=4。io-tierはこの値を引用し独自値を置かない。
- 旧往復2.2msの目安なら必要C=ceil(8.75MB/s×2.2ms/1024)=19、余裕を含め16〜22を比較。GEMの4枠経路/C=3のままではこの条件で目標未達、PPCはC内の実測値を記録。
- CPU不足で未達なら内訳を報告する。入力・表示遅延を犠牲にして速度だけを合格にしない。
## 5. Tier との口・前景駆動・排他
```c
#include "kernel/io_work.h" /* io-tier A1所有。LANはio_sliceの内部を複製・参照しない */
int io_slice_begin(struct io_slice *s, u32 budget_us, u32 max_units); /* A1の口 */
int lan_bulk_step(void *transfer, struct io_slice *s); /* IO_DONE/IO_MORE/IO_WAIT/負値 */
/* bulk_work.c（LAN所有）。io_slice_beginの失敗は転送エラーへ。時計の取得はA1に任せる。 */
static void bulk_idle_work(void) { struct io_slice s; int rc=io_slice_begin(&s,440,4); if(rc<0) fail_clock(active); else lan_bulk_step(active,&s); }
int bulk_init(void) { return idle_work_register(bulk_idle_work); }
int link_fg_enter(void) { irq_state f=irq_save(); int ok=!link_busy; if(ok) link_busy=1; irq_restore(f); return ok; }
void link_fg_leave(void) { irq_state f=irq_save(); link_busy=0; irq_restore(f); }
int kapi_link_update(void) { if(!link_fg_enter()) return AGAIN; update_handle_no_wait(); link_fg_leave(); return 0; }
int link_dispatch_fg(struct io_slice *s) { if(!link_fg_enter()) return IO_MORE; int r=dispatch_rx_timers_tx(s); last_link_fg_jiffy=tick_count; link_fg_leave(); return r; }
void link_irq0_rescue(void) { /* IRQ0内、共有busyでFG/KAPIと排他。既存の限定処理のみ */
    if ((u32)(tick_count-last_link_fg_jiffy)>=10 && link_fg_enter()) { link_tick(); link_fg_leave(); }
}
int host_wait_round(void *t) { /* 擬似コード。archの安全なFG待ち口からだけ呼ぶ */
    struct io_slice s; int rc=io_slice_begin(&s,440,4); if(rc<0) return rc;
    if(nic->fg_service && io_slice_take(&s)) { rc=nic->fg_service(); if(rc<0) return rc; }
    net_rx_ingest_fg(&s); link_dispatch_fg(&s); rc=lan_bulk_step(t,&s);
    fg_service_input_display_once(); /* キーを入力queueへ格納するだけ。描画/アプリcallbackなし */
    return rc; /* IO_WAITでも次周のNIC/期限を確認。HLT/idle_work再帰なし */
}
int host_vfs_wait(void *t) { return platform_io_pump_wait(t,host_wait_round); }
/* platform_io_pump_waitはT2の待ち口を示す仮名。terminal/負値までroundを繰返し、元IF/EEを復元。 */
int lan_bulk_step(void *t, struct io_slice *s) { /* 擬似コード、所有者単位の印 */
    irq_state f=irq_save(); if(bulk_busy(t)) { irq_restore(f); return IO_MORE; }
    set_bulk_busy(t,1); irq_restore(f); int rc=bulk_advance(t,s);
    f=irq_save(); set_bulk_busy(t,0); irq_restore(f); return rc; /* 全終了経路で解除 */
}
```
- 口はio-tierと同一、時計はio_clock_us（AC-TIME-4）、x86 TSC/PPC TBはそのprovider内に隠す。io-tier D6は削除する合意で、本稿から別設計ファイルは書き換えない。
- io_slice_takeは時間・総unitsを開始前に検査、RX取り込み/link処理/bulkで同じsliceを使い途中で予算を作り直さない。frameを再カウントしても安全側。最後の1単位超過を測り、次回は実測上限で開始可否を判断。
- 現NIC取り込みはnet_rx_idle/net/rx_work.cで既にFG。link dispatchの通常経路をFGへ移すが、FGが10 jiffy（100ms）linkを回していなければIRQ0で既存link_tickを1回実行する救済を残す。busyなら実行せず次回に持ち越す。nic.c:298の既存e82557限定回収も維持。
- KAPIは所有中にhandle世代を固定、busyのまま待ち・アプリcallback禁止。E0の不変条件は「通常FG＋既存IRQ0救済、共有busyによる単一所有」。IRQ0は救済条件成立時だけlink状態を進め、他はpending/時刻を記録する。短いirq_saveはbusy/pending受渡し用、NICのticking guardと相互待ちしない。救済は新bulk/保存処理やTier dispatcherを実行せず、新opはFGへ保留する。
- PPCのsyscall待ちでEEを許可するのはarch/ppc/io_pump.c（io-tier T2）の1つの口だけ。Jはその口を呼び、独自にEEを操作しない。同口がIRQ深さ0・未保持lock・再入防止を検査して許可/元状態復元、条件不成立はエラー。x86も対応する安全な待ち口を使う。IRQ出口dispatcherは作らない。
- Tier前の登録はidle_workの1本、4枠満杯は初期化失敗。VFS待ちは直接駆動で進め、IO_WAITも再受信/期限を確認しHLTしない。lan_bulk_stepは自身のbusyを持ち、入れ子では状態・sliceを変更せずIO_MORE。Tier導入後も同じstepを使う。
- 段GでNetNicOpsにint (*fg_service)(void)を追加、NE2K adapterはne2k_bh（リングPIO読出し・tx_dequeue・reinit）の予算付き1単位を接続する。各adapterは不要時の無処理を定義。待ちloopはこの口→net_rx_ingest_fgを毎周回し、下半分の時間もslice経過に含め、無制限走査にしない。
- 通常/host readのメモリ引渡しを予算化する。ゲストディスクへの一括コピーは同期保存が超過し得るため初版の入力・表示遅延合否から除外し、時間を別記する。ホストpwrite待ちは通知loopを止めないworker/完了キュー方式とする。
- RTOは割込禁止中も経過するio_clock_usで判定、clock失敗は継続せずエラー。時計換算にlibgcc依存64bit除算を導入しない。

## 6. W2 へ渡す容量契約
- ARCH_COMPAT_CONTRACT §5: AC-NET-1はadmission/消費、AC-NET-2は総走査＋時間＋単一RX所有、AC-NET-3はRTO/close ACKの予約。W2担当へ本案を渡して確定する。
- C=min(32,NIC予約可能枚数,SW予約可能枚数,保存待ちslot数)。段の容量を足さず、許可済み未着分も予約に含める。
|NIC/段|固定DATAの換算|Nとcredit|
|---|---|---|
|NE2K 256Bページ|管理4+Ethernet14+link20+bulk12+本文1024+FCS4=1078B→5ページ|C≤floor((空きページ−制御6ページ−他予約)/5)|
|e82557 slot|短い末尾も1slot、制御1slotを確保|C≤空きslot−1−他予約、byte換算で枠を増やさない|
|GEM条件4×1514B|4枠から制御1を除き最大3枚|C≤3、N=8でも初期grant=3、保存に応じ増加|
|x86共通SW RX|現RX_SLOTS=4→link用23枠（C22＋制御1）|SFS用queueは4を維持、上流NICも拡張|
- 4×1514BはPACKの経路容量条件で、GEMハードdescriptor総数と同一視しない。P1gのSHA照合はW2/F5で行う。
- 1枠しかない段ではbulk停止。旧byte/256−12・1枚6pageと新枠予約を二重課金しない。
- H1でx86 link SW queueを4→23へ（+19×(1514+4)=28,842B）、受信bulk slotはC3→22で+19×1040=19,760B。送信slotも増やすなら別に同量、両方向同時確保しない初版は共用する。
- H2で82557 RFDを8→23、stride1552なので+23,280B。現DMA16KiBに収まらずRFD_OFF1616+23×1552=37,312B、64KiB境界を跨がない領域へ（確保単位64KiBなら増分48KiB）。登録/再開/周回試験も変更。
- NE2KのC16/22には5C+6=86/116ページ（22,016/29,696B）のRX領域にTX・管理分も要る。16KiB機は不可、32KiB機だけ実配置を照合して増枠、未達機は実Cを記録。10Mbpsに100Mbps目標を課さない。
- GEMは現gem.h:176–177が32descriptor/28公開、4枠は経路制限。まずSW/adapterを23枠へ広げればハードring増設なしでC22を狙える。さらに32→64ならdescriptor+512B、buffer+32×2048=64KiB、pool配置変更・DMA同期の再検証が必要。
- HW3資料: [Sun Platform Notes 806-3985-10](https://docs.oracle.com/cd/E19455-01/806-3985-10/806-3985-10.pdf)はGEM driverを説明するがRX ring最大値は確認できない。[OpenBSD gemreg.h:164–172](https://github.com/openbsd/src/blob/master/sys/dev/ic/gemreg.h#L164)（BSD許諾）の符号化は32〜8192。これは手本上限でApple派生の保証ではなく、メーカーの該当register資料・実機照合を増設のゲートとする。GPL実装は根拠に使わない。 H3でGEM経路容量を確認・拡張、上限資料が得られない/増設不可の間は「Cの範囲で出る値を記録」。W2担当と照合してから容量保証を確定する。

## 7. 先に測るものと実機計画
|新規道具|測定|
|---|---|
|userland/commands/lanbench.c|memcpy/CRC32、1KiB/4MiB、整列/非整列、cache冷温、空ループ差引き|
|net/bulk_probe.c|RX入口→dispatch→保存完了、コピー別cycle、IRQ数、CPU busy、最長非yield|
|tools/lan_bulk_bench.py|4MiB両方向、N=1/2/4/8/16/32、欠落注入、通知往復、CSV回収|
- Ra266でまず現行経路の受信1枚費用と保存先単体速度を測る。その後bulkの各条件を5回、中央値と最悪値を残す。
- CPU使用率=busy cycle/経過cycle。idle/IRQ/foregroundを排他的集計し、I/O待ちと二重計上しない。
- 遅延試験の1回のVFS read/writeは≤32KiB（合計4MiB）、各呼出しの間にアプリを進めて表示する。fg_service_input_display_onceはキーの入力queue格納だけで表示しない。同じlan_bulk_bench/lanbenchで非転送時の入力→応答/表示を先に測定。次に4MiB転送中にrshell入力・gshell更新を20回/s発生、p95≤50ms・max≤100msを唯一の共通合否とし非転送との差も併記。io-tierの増分p99≤2ms/max≤5msは参考記録へ下げる。
- payload速度、closeまでの総時間、cache条件、NIC、SHA、budget、N/C、drop/再送数を記録。異なる時計の片道差を計算しない。
- Ra266の8.75MB/sなら4MiB本文約0.479秒。NP21/W 10Mbpsの70%は0.875MB/s・約4.79秒、DATAのみ媒体下限約3.59秒。
- NP21/Wはエミュレーション費用を分け、速度だけでRa266を否定しない。正確性・遅延・容量試験の先行環境とする。
- 実装後にNP21/W停止→配備→再起動。Ra266の物理操作は担当へ依頼。本ラリーで実行しない。

## 8. ファイル割当と段（各ファイル1段・1実装者）
|段|ファイル（新/変更、概算行）|機能・依存／並列|
|---|---|---|
|A|net/bulk_wire.h 新180|形式・定数・内部口、最初に固定|
|B1|net/bulk_tx.c 新280|送り手・再送timer、A後|
|B2|net/bulk_rx.c 新280|受け手・順次保存・ACK、A後B1と並列可|
|C|tools/host_bulk.py 新350|ホスト台帳・fd worker接続、A後B1/B2と並列可|
|Cw|tools/host_bulk_io.py 新160|pwrite/close workerと完了キュー、Cの口確定後|
|D|net/link_rx.inc 変更60|新op長さ/世代検証・dispatch、B後|
|E0a|net/link.h 変更50|通常FG＋IRQ0救済の単一所有口/不変条件、A後|
|E0b|net/link.c 変更180|通常dispatchをFG化・共有busy/最終FG時刻・救済、E0a後|
|E0k|net/link_host_api.inc 変更100|KAPIをFG所有guardへ置換、E0b後|
|E0c|net/nic.c 変更100|IRQ0の10jiffy/busy条件付きlink救済、既存RX接続、E0k/G後|
|E|net/link.c 変更100|失効・送信優先・bulk接続、D/E0c後|
|F|tools/host_agent.py 変更100|bulk dispatch・LIST・送出、C後Eと並列可|
|G|net/nic.h 変更70|W2容量予約・予算口とNetNicOps.fg_service、Bに先行可|
|G1|net/nic_lgy98.c 変更60|NE2Kのfg_serviceをne2k_bhへ接続、G後（adapter所在を実装時照合）|
|H/H1|net/nic_rx.c 変更180|既存FG取り込み維持、link23枠/共有予約、G後|
|H2a|drivers/net/e82557/e82557_regs.h 変更30|RFD23・DMAサイズ、G後|
|H2b|drivers/net/e82557/e82557_desc.c 変更100|ring初期化/周回/回収、H2a後|
|H2c|drivers/net/e82557/e82557.c 変更60|DMA領域確保・境界検証、H2b後|
|H3|platform/powermac/nic_gem.c 変更120|4枠経路のadmission拡大、G/W2後|
|I|net/bulk_work.c 新180|io_slice_begin/idle_work・bulk自身の再入guard、B1/B2/H/E0cとio-tier A1・A3・T1・T2後、LAN専有|
|J|fs/hostsvc/hostsvc.c 新380|VFS OPEN・fg_service/RXを回す待ち・PPC pump口使用、I/G1とio-tier A1・A3・T1・T2後|
|K|tools/host_fs_meta.py 新250|stat/list等、A後並列可|
|L|fs/serialfs/serialfs.c 変更80|serialを残す経路整理、J後|
|M|tools/lanfs_host.py 変更60|旧LAN起動撤去・移行エラー、F/K後|
|N|net/bulk_probe.c 新150|測定hook、B/H/I後|
|O|userland/commands/lanbench.c 新180|ゲスト測定、N後|
|P|tools/lan_bulk_bench.py 新220|ホスト測定、C後並列可|
|Q0|tools/tests/lan_bulk_host.c 新350|実bulk_tx/rx.cを偽NIC/時計/保存先で駆動、A後B1/B2と並列可|
|Q|tools/tests/test_lan_bulk.py 新300|Q0をhost compilerで組み実行・Python peerと共通vector・変異、Q0/C後|
|R|tools/tests/test_lan_bulk_capacity.py 新250|容量・予算・変異、G後Hと並列可|
|S|userland/tests/lan_bulk_test.c 新220|実guestのVFS・通知・再起動、J後|
- 表のBはB1/B2両完了を指す。同一ファイルは直列、B1/B2/Cは共通ベクトルで突合。QのCHECK inputsにC実体・ヘッダ・harnessを列挙、source文字列検査で代用しない。
- mount/HostDrv/hsync/netcd/起動CLI/ビルド登録の呼出し元は統合前にrgで棚卸しし、実ファイル1個につき補助段1つ（20..100行）を追加。今回の読取指定範囲外なので所在を確定扱いにしない。
- /hostと旧LAN SFSの全利用者移行までMを完了にしない。NIC adapter変更はW2担当が同様に分割。Tier本体は発注しない。

## 9. 受入→試験→変異
|受入|host/ゲスト試験|変異（runtime RED）|
|---|---|---|
|LE/長さ/末尾/上限|共通byte vectorをx86/PPC、0/1/1023/1024/1025B、4MiB、u32境界|LE反転、len検査除去、offset周回|
|OPEN/CLOSE/回収|300回1KB read、cpのread→write、CLOSED/INLINE欠落と遅延旧OPEN|watermark照合除去、未完了rid回収、再TRUNC|
|選択再送|N=8/C=22で境界を跨ぐ、穴/最終ACK欠落、旧base ACK|burst待ち復活、grant加算、bitmap基準誤り|
|保存後credit|順不同受信でもguestの書込みoffsetは昇順、部分write/ENOSPC/close失敗|穴を越して保存、receivedで返却、status無視|
|制御枠・単一RX|4枠満杯と通知併走、NE2K境界、他窓同時予約|制御予約除去、二重広告、予算reset|
|FG排他・予算|link/bulkのbusy・KAPI/IRQ、PPC pump経由のEE許可/復元、440µs/4単位|busy解除漏れ、bulk入れ子実行、HLT、EE復元除去|
|IRQ0救済|syscallなし30秒の計算中もセッションがSTALEにならない、10jiffy境界・busyなら不実行|救済削除、busy無視、閾値誤り|
|待ち内進行・通知|NE2KのPIO/TX/reinit進捗、≤32KiBごとのアプリ表示、rid=0/本文0B通知|fg_service欠落、待ち内描画、PROBE長さ検査除去|
|/host機能・経路|全VFS操作、常駐＋hsync、HostDrv、serial、CD退行|LAN fallback復活、経路選択誤り|
|内容と性能|4MiB両方向照合・hash、同一道具の非転送基準→転送p95/max、短read1往復|INLINE close前成功、再提示本文欠落、max記録欠落|
- 共通遅延合否は通常/host転送と入力・表示の併走。ゲストディスクへの一括コピーは初版の遅延合否対象外（内容/順序/credit試験は対象）。性能は実機、変異は偽時計/NIC/保存先のC実体でruntime RED、anchorは一意。
- 実装段は変更試験だけ--mutate、対応ビルドとlint。全mutation/check-changedは実行せずCIの担当とする。

## 10. 未決と実物との照合
- ユーザー判断の未決 **0件**。p95≤50ms・max≤100ms、非転送時との差も併記を採用済み。GEMのメーカー上限資料照合は実装ゲートでありユーザー判断ではない。 N/予算調整、W2確定、棚卸し、実測は担当工程の作業とする。
- 実物との食い違い **2件**（設計による変更予定は数えない）。
  1. r0「ARCH_COMPAT_CONTRACTが作業木になく未確認」→今回のorigin/mainには存在。§5はW2の予約で、確定容量保証ではない。
  2. PACK「今はpwriteしてclose」→serialfs_host.py:543–572はsecure側がpwrite_all、他方はseek/write＋withのclose。両方fsyncなし、完了意味は一致。
- 基点9e39176aではlink.c:543、nic.c:213/310がlink駆動、net_rx_idle:257は既存FG取り込み。E0で旧不変条件と新op受付を更新。TASK_NET_HOSTSVC §0の旧共存方針より今回の決定を優先する。
- 反論: 0件。F1〜F13、R4とユーザー/PM決定を採用。

## 確定 (2026-11-14、Opus 第 2 回、PM 決定を反映)
- N1: io_sliceの内部は複製せずio-tier A1のio_slice_beginを使用、I/JはA1・A3・T1・T2に依存。
- N2: GでNetNicOps.fg_serviceを追加、VFS待ちでNE2K下半分とRX取り込みを直接進める。
- N3: FG不実行10jiffyかつ共有busyなしでIRQ0が既存link_tickを1回救済し、30秒syscallなしでもSTALEなしを受入。
- Tier側N3: lan_bulk_step自身がbusyを持ち、入れ子は無変更でIO_MORE、全終了経路で解除。
- N4: 待ち内はキーの入力queue格納だけ、表示せず、遅延試験のread/writeは1回≤32KiB。
- Tier側N4: PPCの待ち中EE許可/復元はarch/ppc/io_pump.cの単一口へ集約、Jは呼ぶだけ。
- N5: rid=0・本文長0のPROBEはclosed_rid通知だけ、転送ID付きの4B PROBEとは区別。
