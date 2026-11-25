# IS ゲスト受入（PM 実施）

## PC-98

2026-11-25 の決定に従い、実機では lead 3（引数省略可）、NP21/W では以下の例の lead 4 を使う。Ra266 の CS4231 は lead 3 で 5 回とも欠音 0。NP21/W は DMA 位置が一度に 384 frame 進み、lead 3 では 5 回中 4 回で 1〜2 区間欠けた。

pc98emu は /api/cmd の実行中に /api/sound が応答しない。rshell を閉じ、ゲストのコマンドはキー入力で流し、別端末で採取する。再起動直後は CS4231 を開く前の最初の窓が rate 8000 になる。照合は先頭の 8000 Hz 窓を読み飛ばし、44100 Hz に切り替わった後の連続性を検査する。

通常像と同じビルドの pcm.mod / fm.mod、テスト用ユーザーランドを配備する。CS4231 と FM の両方を有効にした構成で、シェルから順番に実行する。

```text
snd2_test
snd2_test exit
snd2_test
snd2_test wave lead=4
snd2_test stall lead=4
```

最初の 3 回とも装置一覧に CS4231 (kind=1) と FM (kind=2) があり、各回の末尾が `snd2_test PASS rc=0`。exit は動作中の stream と chip を開いたまま終了する。直後の通常実行で両方を再取得し close できることが終了回収の合格条件。装置が欠けた構成の PASS は両装置の検証に数えない。

wave は 44.1 kHz、period 256、上記の lead で 30 秒。左は 32 段の 1 kHz 正弦、右は 256 frame ごとの 1 frame の印（値 10000 + 区間番号 modulo 16000）。written=1323000、underruns=0。stall は 1 秒のところで供給を 200 ms 停止する。underruns が増え、played ≤ written を保ち、再供給後も印が順番どおりなら合格。epoch は下記の診断像で別に実行する。

ホスト側は再生前から採取する。例は別端末で実行し、再生終了後に Ctrl-C で止める。

```sh
mkdir -p capture
while :; do
    curl -fsS "${NP21W_AIDEBUG_URL:-http://127.0.0.1:8025}/api/sound?pcm=1" -o "capture/$(date +%s%N).json" || break
    sleep 0.25
done
```

各 JSON の ok・rate・frames を確認し、pcm を base64 復号して s16le ステレオとして読む。各窓は frames_total を終端にして重複を除く。採取自体に空白があれば再測定する。wave の右の印の欠け・重複が 0、左の周期が 44.1 frame。発音遅延は下記の latency 専用試行で測る。

## Ra266 実機（§13-4）

1. 製品像で CS4231 を確認し、シリアルを記録しながら `snd2_test wave lead=3 secs=60` を一度実行する。44.1 kHz・period 256 の連続した 60 秒で `written=2646000`、`underruns=0`、drain 後に `snd2_test PASS rc=0` が合格条件。30 秒試行を足し合わせない。
2. IF=0 は下記の kernel-tick-probe 診断像と対のモジュールを実機に配備し、KDEBUG=1 で採る。`tickprobe stop` で起動時の採取を閉じる。ホストの同じビルドの `nm -an build/out/tick-probe/kernel.elf` から tsc_clock.o の state・tsc_generation・epoch の番地を求める（同名なら kernel.map の当該 .bss と照合）。ゲストの `peek -k -w4 番地 4` で state と世代を、epoch の番地 + 0x10 から長さ 8 で mult・shift を読む。値はリトルエンディアンで、state=2、mult > 0、shift は 1〜32。世代と係数を十進整数で `{"generation":G,"mult":M,"shift":S}` の形で rate-before.json に保存する。
3. 生シリアルを別ファイル if0.serial に取り、`tickprobe start` → `snd2_test`（pause を含む）→ `snd2_test wave lead=3 secs=60` → `snd2_test_diag epoch lead=3`（下記の b・a 操作）→ `snd2_test exit` → `tickprobe stop`。同じ時計の値を再読し、state・世代・係数が前後で一致することを確認する。60 秒の集計と各試験の PASS を保存する。
4. ホストで `python3 tools/accept/snd2/verify_if0.py rate-before.json if0.serial` を実行する。両 bank の start・pause・resync・advance・teardown・other と、それを囲む if0-resident・irq0・irq10 の各 worst と全体最大を記録する。全経路の count が各々合計 1 以上、observation_lost=0、最長が **1450 µs 以下**で合格。診断試行後も count=0 なら、その経路は未測定で実機の IF=0 判定を保留する。期限超過は未合格として実測値を報告する。
5. L3 の sndl pump 最大間隔は IL で測る。sndl 実装後、同条件で最大間隔を cap − period と比較する。IS の max_writer_us は書込みループの記録として扱う。

## v1 の回帰

ホストで `python3 tools/tests/make_pcm_wav.py test.wav` を実行し、WAV を HostDrv へ置く。ゲストで `pcm_play /host/test.wav` を実行する。再生中に採った JSON を使い `python3 tools/tests/pcm_play_guest.py test.wav capture/<取得時刻>.json --min-frames 22050` が終了コード 0。既存の `pcm_test`、`pcm_test short`、装置なし構成の `pcm_test nodev`、E4 専用像の `pcm_test e4`（serial に `[pcm-e4] 5/5 PASS`）と TASK_PCM_CS4231 の E 系の判定を適用する。

## PowerMac

```sh
make ARCH=ppc PLATFORM=powermac IMAGE=powermac KB_ROOT_BINS=build/out/ppc-powermac/user/snd2test.bin build/out/ppc-powermac/boot.iso
```

この構成の root ISO とカーネルを使って QEMU の PowerMac を起動する。シェルから `/bin/snd2test.bin nodev` を起動し、`snd2 devices=0`、`snd2_test PASS rc=0` の順でシェルに戻れば合格。


### wave の生 frame 照合

wave 専用の空ディレクトリに取得する。再生開始前に少なくとも 1 窓の取得完了を確認し、終了まで採る。停止中は何秒待っても frames_total は進まず、前の試験の印が残る。最初の 44100 Hz 窓の終端を基準に、その後増えた frame だけを照合する。起動時の 8000 Hz 窓から切り替わる場合は、新レートの先頭から照合する。次をホストで実行する。

```sh
python3 tools/accept/snd2/verify_wave.py capture
```

最後の行が `PASS 1323000 consecutive stereo frames`。不一致の位置を調べるには `python3 tools/accept/snd2/diag_insert.py capture` で右の印の間隔と挿入候補の frame 数を表示する。取得側に隙間があった場合はその窓を補間せず採り直す。

E4 専用像は `make kernel-pcm-e4-diag` でビルドする。配備には `build/out/pcm-e4-diag/` の vmkernel.lz4 と同じディレクトリの paired モジュールを使う。実行中に CTRL+STOP で抜けた後も、通常の `pcm_test short` で再 open・短音 close が成功することを確認する。

## P1（§13-3）発音遅延 < 30 ms

製品像、最新の snd2_test、エミュレータ実時間等速で実行する。ホストで以下をセットする。以降のホストコマンドは作業木のルートから。シェルや別の入力源から x を送らない。

```sh
D=$(mktemp -d)
export NP21W_AIDEBUG_URL=${NP21W_AIDEBUG_URL:-http://127.0.0.1:8025}
```

ゲストで `snd2_test latency lead=4`、約 2 秒後にホストで `python3 tools/accept/snd2/latency.py "$D/latency-01"`（未使用の出力先）。再生中にホスト側が x を 1 回送り、ゲストは次に受理される書込みの右先頭だけを −23456 にする。ゲストは終了時に `latency frame=F write_us_hi=H write_us_lo=L` を出す。時刻は受理された write の直前（ゲスト起動から 2³²H + L µs）。

ゲストの単調 µs 時計で、書込み直前から HAL の DMA 由来の played が F を超えたことを観測した直後までを測る。再試行では受理された write の時刻を使う。書込みを続けながら観測するので、割込み・観測の遅れも上限に含む。dma_upper_us は DMA 通過までの上限。CS4231A の再生 FIFO 16 サンプルと 16-bit PCM の DAC 群遅延最大 10 サンプル周期を加える。44.1 kHz では 26 周期を µs に切り上げて 590 µs となる。コーデック出力までの判定は output_upper_us_hi=0、output_upper_us_lo < 30000、underruns=0、epoch=0、PASS を要求する。出典は [Crystal Semiconductor, CS4231A, DS139PP2](https://www.cebix.net/downloads/bebox/cs4231a.pdf) p.6「Digital Filter Characteristics」の Group Delay 最大欄、pp.14–15「FIFOs」。ホストは補助照合に使い、timing.json の frame が F と一致することを確かめる。upper_ns はキー要求の送信直前から、印を含む最初の sound 応答の受信完了までの単調時計の差。書込みは送信より後、採取側の発音は受信完了以前なので、入力配送・ポーリング・転送も含む上限になる。ゲスト位置カウンタに依存しない。開始前から x が入力されていた場合や採取窓の欠けは無効。時刻と before.json・hit.json を保存する。req52 の upper_ns は 337〜365 ms で、API 1 回に約 105〜128 ms かかった。この値は記録のみとし、ホストの終了 0 は印の照合成功を示す。異なる開始時点で繰り返した場合は各回の値と最大を残す。

## P1（§13-3）150 ms 停止の前後と再生継続

NP21/W と実機で kernel-tick-probe 診断像と対のモジュール・snd2_test_diag.bin を使う。`snd2_test_diag epoch lead=4`（実機は lead=3）を開始し、2 秒後に b を入力する。診断 HAL は IRQ と DMA を動かしたまま、advance と書込み時の DMA 位置の観測をゲスト時計で 150 ms 抑止する。次の advance が通常の欠測判定、停止確認、epoch 更新、再始動を行う。シリアルの `epoch armed` を確認し、ゲストが等速で 0.5 秒以上走った後、再生中に a を入力する。NP21/W での入力例を示す。

```sh
curl -fsS -d text=b "$NP21W_AIDEBUG_URL/api/key"
# epoch armed が表示された後
sleep 0.5
curl -fsS -d text=a "$NP21W_AIDEBUG_URL/api/key"
```

a を送った直後、同じホストで再開後の PCM を 2 回採る。最初の窓を基準に、増えた frame の印を調べる。途中の窓も取得した場合は時刻順に引数に渡す。全窓が同じ frames_total なら再生継続の証拠がなく不合格。

```sh
curl -fsS "$NP21W_AIDEBUG_URL/api/sound?pcm=1" > "$D/epoch-a.json"
sleep 0.25
curl -fsS "$NP21W_AIDEBUG_URL/api/sound?pcm=1" > "$D/epoch-continued.json"
python3 tools/accept/snd2/verify_epoch.py "$D/epoch-a.json" "$D/epoch-continued.json"
```

シリアルの `epoch before=E played=P ms=T after=A played=Q ms=U` の前半が armed の値に一致し、A = (E+1) mod 2³²。最後の位置集計行は epoch=A、played > Q、played ≤ written=1323000、drain 後の末尾は PASS。上で出した実音の application frame も Q を超えること。a の後に位置と実際の PCM の両方が進んだことを確かめる。実機ではシリアルの epoch と played の継続を記録する。旧 req52 のホスト pause は PIT と DMA の両方を凍結し得る。当時は a 処理と最終位置検査のいずれも集計表示前に rc=6 を返したため、armed と FAIL の 2 行から失敗の前後は特定できない。

## P1（§13-3）IF=0 の最長区間

`make kernel-tick-probe` の build/out/tick-probe/vmkernel.lz4 と同じ出力ディレクトリの paired modules（kdebug.bin、pcm.mod、fm.mod 等）を診断機に配備する。CPUID の TSC を持つ CPU を選び、等速・KDEBUG=1 で起動する。tickprobe.bin、snd2_test_diag.bin と通常の snd2_test.bin を実行可能にする。シリアル採取を開始し、診断機と手元の kernel.map のビルドが一致することを確認する。

1. ゲストで最初に `tickprobe stop` を実行し、起動時に自動で始まった採取を閉じる。この出力は if0.serial の外に置く。ホストで `python3 tools/accept/snd2/sound_rate.py > "$D/rate-before.json"`。診断像の map と .o から PC-98 の PIT 校正済み TSC 係数と世代を読む。
2. ゲストで順に `tickprobe start`、`snd2_test`、`snd2_test wave lead=4`、`snd2_test_diag epoch lead=4`（上記の b → 位置欠測 → a）、`snd2_test exit`、`tickprobe stop`。各試験の PASS と、最初の started、最後の `TSC=raw cycles observation_lost=0` を確認する。波形や遅延の製品像試行とは別に採る。
3. 両方の bank の snd-start（16 KB 消去・prime・hw_start）、snd-pause（消去）、snd-resync（停止・再構築・再始動）、snd-advance（通常のコピー）、snd-teardown（停止・回収）、snd-other（書込みの publish 等）について count と worst の hex 値を保存する。各種類で両 bank の合計 count > 0 を要求する。foreground / IRQ のどちらでも stream の最外側のロック区間を計時し、途中で resync / teardown に移った場合も最初からの時間を計上する。停止後の集計表示を採取する。foreground の if0-resident と、advance を呼ぶ PIT の irq0・CS4231 の irq10 も両 bank で採る。これらは HAL を囲む時間（他の処理を含み得る）の保守的な上限。区間の一部だけが短くても合格にしない。
4. ホストで `python3 tools/accept/snd2/sound_rate.py > "$D/rate-after.json" && cmp "$D/rate-before.json" "$D/rate-after.json"`。同じ世代・係数であること。下のコマンドでシリアルログから全種類の最大を µs に切り上げる。

```sh
python3 tools/accept/snd2/verify_if0.py "$D/rate-before.json" "$D/if0.serial"
```

if0.serial は行頭にホスト時刻等を挿入しない生シリアルで、今回の start/stop 1 組だけにする。合格は全経路観測済みかつ最長が period の 1/4 以内（設計の 5.8 ms / 4 = 1450 µs 以下、実周期 256 / 44100 秒にも収まる）。生の最大値と換算値を残す。校正失敗、時計の世代変更、イベント欠測、未実行の経路は未測定。閾値超過は未合格として値を報告する。

### IL の SOFT OPNA

録音を先に開始し、ゲストで `sndl_test soft` を実行する。44100 Hz の取得 JSON を
`python3 tools/accept/snd2/verify_soft.py <録音ディレクトリ>` で照合する。
右の 256 frame ごとの 12345 が同期印。左の FM 音と右の印を含む最初の 1 秒を
上流 fmgen の同じ音色・音程から得た PCM と比較する。
