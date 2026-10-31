# exec/exec_stdin.inc — GUI stdin の syscall 再実行

> 状態: **現行 (2026-10-31)**

GUI・CPL3 の現在のアプリが、WM 内部ではなく sys_read 自身の syscall に入っている時だけ
`exec_stdin_state()` が AppSlot 末尾の `stdin_read` を返す。ほかは NULL。
行読取の本文と状態型は [vfs_fd_stdin](../fs/vfs_fd_stdin.md) に集約する。

## park と resume

kbd 待ちの park は従来どおり int80 フレームを保存して longjmp する。
`APP_RESUME_SRC_KBD` かつ active の resume は、検査後に `kbd_inject_peek_for` で配送先を指定して
入力の存在だけを確認する。既存 inj_read と同じ lock・配送規則を使い、文字を消費しない。
空なら AGAIN でフレーム・状態・進捗を変えない。

既存の caller_context、setjmp、resume_commit、mark_scheduled、abort_check、context 復元を経て、
ring3_resume の直前にだけ保存 EIP を2バイト戻す。EAX は sys_read のスロット値を保つ。
スタブの int 0x80 が再実行され、通常 dispatch の caller/guard、引数とポインタ検査、
入口・出口の abort と完了時 STOP の処理を通る。子の CR3 と owner の下で done から続ける。
入力が再び空なら新しい int80 フレームを保存して park するため、EIP の巻戻しは累積しない。

active でない getchar/getkey は従来どおり1文字を EAX に返す。POLL/YIELD/KEEP は従来どおり。
完了した sys_read の STOP は active=0 と確定件数を保存し、KEEP 再開で EIP と EAX を保持する。
待機中 kill と実行中 fault/abort は既存の回収経路へ進み、slot_zero が状態も消す。
追加の解放資源や公開 ABI の変更はない。

## 試験とゲスト受入

`test_exec_r1.py` は実 exec_resume と setjmp/longjmp の着地で EIP・EAX・非消費・空入力・
再 park と既存の着地を確認する。`test_kstop.py` は STOP と slot 初期化を確認する。

PM は NP21/W 停止→NHD 配備→再起動後、GUI 端末で `kout_test --stdin-byte` (A) と
`kout_test --stdin-line` (A、待機、B、待機、Enter) を実行する。buf と戻り値、読取完了まで
呼出しが戻らないこと、待機中の WM 動作、CTRL+STOP 後の別読取、完了時 STOP/KEEP、
再開時 fault と slot 再利用を観測する。同じモードを CUI と stdin リダイレクトでも実行する。
GUI の FEP ON でも両モードを実行し、確定済み日本語入力は `--stdin-utf8` で「あ」3バイトを確認する。
`sys_read` は input service の kbd provider を使い、GUI の FEP 確定入力は端末の注入リングへ来る。
通常 dispatch の再実行、ポインタ再検査、実 CR3 と例外帰属の通しの確認はゲストに残る。
