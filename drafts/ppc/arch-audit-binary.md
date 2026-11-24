A 3 件 / B 6 件 / C 3 件

対象は `arch-audit-binary`、main `3497386e7`。以下の「確認」はソースと設計文書で確認した事実を指します。分類は **(a) ソース・意味を揃える価値がある**、**(b) x86 のバイナリ形状を揃える要求に独立した価値が見当たらない**、**(c) 既存の利用や運用を踏まえて判断が要る**、の３つです。

PowerPC で最も具体的な歪みは、**PC-98 由来の固定ページング帯とシェル番地が、PowerPC のリンカ、起動時のマッピング、受入試験を連動させていること**です。KAPI では、共通の API 定義から両機種の表を生成できていますが、**公開 API の意味と、既存バイナリのスロット配置が同じ契約に入っている**ため、PowerPC でも PC-98 専用機能の欄を維持します。シェル起動ループとモジュールのライフサイクルには、すでに共通化を進める土台があります。

### 決定・KAPI

| 重さ・分類 | 確認した事実と証拠 | 直す向き |
|---|---|---|
| **A・(c) 「x86 に揃える」の適用範囲** | `POLICY_DEV.md` §1 は同じ役目を一つの実装にできるか先に判断する。§10 は実行の形、番号、資源の持ち方まで x86 に揃え、相違を残す根拠を物理制約に限定する。`ARCH_COMPAT_CONTRACT.md` §4.2 の AC-KAPI-1 はスロット順も保証に含め、§9 は物理差と既存の未整合を記録する。`PPC_KAPI_FILL_DESIGN.md` §1–2 は共通本体への結線と、既存スロットの名前・意味の維持を求める。スロット順を直接要求するのは AC-KAPI-1 で、固定番地の具体値は `PPC_SHELL_ALIGN_X86_DESIGN.md` §4 にある。 | §10 と互換契約に、**両機種で保つ利用者向けの振る舞い、各ターゲットで保つ既存 ABI、機種が選ぶ配置**を明記する。 |
| **A・(c) KAPI の番号・レイアウトが一体** | `sdk/kapi.json` の API 配列順を `sdk/gen_kapi.py` がスロット番号、ラッパー、初期化表に使う。関数欄の容量は 384、データ欄の先頭は `8 + 4 × 384 = 0x608`。`arch/ppc/exec_kapi.c` の `exec_kapi_table()` はページ整列した `ppc_kapi_table` を返し、`arch/ppc/kapi_table.h` は共通の `KernelAPI` 型を参照する。x86 の `arch_exec_kapi.h` は `KAPI_ADDR` を返す。ここで確認できた結合は **表の内部配置**。PowerPC に別の手書きスロット台帳を維持する構造は、この経路では見当たらなかった。 | 共通の API 定義と各ターゲットの ABI 配置を生成器の入力上で分ける。PowerPC の現行 SDK・バイナリが消費する配置は棚卸しし、変更が必要なら同一ターゲット内の互換方針を決める。 |
| **B・(b) PC-98 固有の不在機能にも同じ番号を割り当てる** | `sdk/kapi_target_ppc.py` の `PROVIDER_GROUPS` と `absent_providers()` には `buz_*`、旧 FM/SSG 6 欄、NP2、V86 などがあり、`gen_kapi.py` が不在ラッパーを生成する。`absent_result()` の NULL・指定照会の 0・`OS32_ERR_NOSYS` と void の無動作は呼出元が観測する意味として残る。IDE は別扱いで、`--ide` 有効時に `absent_providers()` が IDE 群を不在集合から外す。旧 FM/SSG の欄の維持には §9 の明示決定がある。 | 名前と型、不在時の意味は SDK で保つ。PC-98 固有欄に両機種共通の番号を要求する規則を見直し、旧 FM/SSG の既存決定と PPC の既存 ABI を合わせて扱う。 |
| **B・(b) KAPI のターゲット間照合がバイナリ形状まで固定** | `tools/tests/test_arch_compat.py` は生成した `os32_kapi_slots.h` をバイト単位で比較する。このヘッダには順序、容量、データ欄オフセットの刻印が入る。`tools/ppc/test_kapi_table.py` の順序付き初期化表・ラッパー照合と、`tools/ppc/test_kapi_binding.py` の不在集合・型・実結線の検査は PPC 自身の ABI と提供機能を検証している。`sdk/os32x_hdr.py` と `exec/os32x_hdr.c` が使う刻印はデータ欄のオフセットで、ローダの取り違え検出に役立つ。 | ターゲット間では名前・型・戻り値・能力を検査する。番号、刻印、初期化表の一致は同一ターゲットの SDK、像、カーネルの間で検査する。 |

### メモリ・シェル

| 重さ・分類 | 確認した事実と証拠 | 直す向き |
|---|---|---|
| **A・(b) PC-98 の固定ページング帯が PowerMac のカーネル予算を拘束** | `include/memmap.h` の固定帯は `MEM_FIXED_PAGING_GUARD_BASE = 0x3e0000` から始まり、`mm/paging.c` は x86 の PD/PT の並びとページ数を静的検査する。`platform/powermac/platform_kboot_layout.h` は同じ境界を `PM_KERNEL_CHAIN_LIMIT` に取り込み、`arch/ppc/kernel.ld` が常駐領域・ヒープ・KAPI 分の予算・ガード・SHM の上端をそこに制約する。`platform/powermac/kernel_boot.c` の `boot_walk_valid()` は固定ページング帯全体の恒等写像を要求し、切替経路も `MEM_FIXED_PD_BASE` に依存する。現行 PPC には実依存がある。 | PowerMac の起動 root、予約量、guard を PPC の実際の用途から定義し、リンカ・予約・マッピングが同じ機種別定義を参照するよう移す。 |
| **B・(c) シェルの `0x400000` 固定と受入の数値固定** | `sdk/link/app_sys.ld` と `sdk/link/ppc/app_sys.ld` はともに `LAYOUT_MEM_SHELL_LOAD_ADDR` を使い、`exec/exec_launch.inc` は `MEM_SHELL_LOAD_ADDR` にロードする。`sdk/mkos32x.py` は ELF の `__os32_shell_load_addr` を使い、欠ける場合は `sdk/os32x_hdr.py` の `0x400000` に戻る。`tools/ppc/kboot_accept.py:fixed_layout()` と `tools/ppc/kboot_layout.py:report_product()` は番地や帯の連続性まで固定する。現在の PPC 常駐像には番地の一致が必要で、PC-98 と同じ値である根拠は設計上の配置整合にある。 | PPC のリンカ、梱包器、ローダ間の番地一致を保証し、受入は非重複、必要容量、guard、権限を検査する。番地変更の判断には既配布の PPC 常駐像も含める。 |
| **C・(a) シェルの役割と起動順** | `platform/pc98/kernel.c:kernel_shell_loop()` と `platform/powermac/ppc_kernel.c:platform_boot_run()` はともに `init/shell_loop.c:shell_loop_run()` を呼ぶ。外部シェルを `exec_run()` し、終了後に再び起動する処理は共通化済み。§9 にある常駐 CLI を製品シェルの起動文脈に見立てる記述は、監査時点のこの起動経路より古い。 | 共通ループ、シェル切替、コマンドの公開名・意味を維持する。§9 の製品シェル行を現行コードに合わせる。 |

### 実行形式・部品

| 重さ・分類 | 確認した事実と証拠 | 直す向き |
|---|---|---|
| **C・(a) 共通の像検査とローダのライフサイクル** | `sdk/mkos32x.py` は PPC の像に `OS32X_FLAG_PPC32` を付け、`arch/ppc/arch_exec_image.h` はそのフラグで対象 CPU を判定する。`exec/os32x_hdr.c:os32x_layout_check()` は共通のヘッダと世代を検査する。部品では `kernel/module/module_image.c` が共通の検証処理を使いつつ、`MODULE_PPC_FORMAT_VERSION` では `module_fixup_ppc_apply()`、x86 では別の再配置処理を使う。PPC のネイティブコードと LE の梱包メタデータも区別されている。 | ヘッダ検証、ロード、開始、公開、後始末の手順を共有し、対象 CPU の判定と再配置をターゲット側に置く。世代は各ターゲット内の像とカーネルの組で検証する。 |
| **B・(c) PPC の positional imports は KDEBUG 固有** | `kernel/module/module_schema.h:module_positional_imports()` は `MODULE_ID_KDEBUG` だけを対象にする。`kernel/module/module_image.c` は名前ハッシュ・型・版・位置の範囲・重複に加え、この場合に限り `data_off + 4 * i` を要求する。`tools/mkmod.py` にも data 先頭の厳密な配列を要求する経路がある。FEP にも適用される制約ではない。実行側がその位置を前提にしている範囲は未確認。 | KDEBUG の import 呼出側まで追い、位置に依存する利用者を特定する。必要な制約は KDEBUG のターゲット別 ABI として明記する。 |
| **B・(c) `root_pair` は版の混在検出に媒体全体を結合** | `PPC_MODULE_ALIGN_X86_DESIGN.md` は x86 の VK32 由来の pair 検査を PPC に引き継ぐ。`tools/ppc/root_pair.py:bind_module()` / `bind_iso()` は ISO 内の部品の pair と CRC を更新する。`platform/powermac/module_pair.c:pm_verify_entry_pair()` / `platform_module_pair()` は正規化した ELF と root の検証結果から pair を公開する。版の混在を検出する効用があり、その実装コストとして二段生成と ELF・ISO・部品間の結合が生じている。 | 「別ビルドの部品を拒む」要件を明文化し、ターゲット・部品 ABI・ビルド識別子で同じ受入条件を満たせるか判断する。媒体自体の整合性検査は維持する。 |
| **B・(c) FEP の起動条件が PowerMac の製品媒体に結合** | `platform/powermac/ppc_kernel.c:platform_boot_run()` は静的 SQLite の成功に加え、HDD root、Unicode ファイル確認、`platform_module_pair()` 成功を条件に `fep_boot_static()` を呼ぶ。`kernel/fep_boot.c:fep_boot_start()` には、部品ロード後の検査から公開までの共通処理がある。設計では SQLite のモジュール化は任意で、FEP の辞書パスと起動・公開・失敗時の条件を揃える。 | 辞書と部品の利用可否を共通の起動処理に渡し、HDD・媒体検証は PowerMac の入力として扱う。静的 SQLite と部品版 SQLite は同じ準備完了条件に接続する。 |
| **C・(a) HWDEV はターゲット別レイアウトを扱う先例** | `tools/gen_hwdev_contract.py:target_gate()` は PPC を `--layout-only` に限定する。ELF のターゲットとバイト順を検証して対象の構造体配置を取り出す。x86 の生成物には i386 cdecl のジャンプ列がある。この経路で PPC に x86 の実行用ジャンプ列を要求する規則は見当たらなかった。 | 宣言や部品の意味は共有し、レイアウトの照合と呼出コードの生成は対象 CPU ごとに行う方針を KAPI にも適用できる。 |

### 上位 5 件

1. **`POLICY_DEV` §10 と AC-KAPI-1 の適用範囲を決める。** 共通の振る舞いとターゲット内 ABI を明記すると、後続の配置変更や試験変更の判断基準ができる。
2. **PowerMac の固定ページング帯を実用途から定義する。** 現在は PC-98 由来の境界が PPC の起動 root、カーネル容量、予約・保護に波及している。
3. **KAPI の意味定義と ABI 配置を生成器で分ける。** PC-98 の既存番号を守りつつ、PowerPC の不在機能と既存バイナリを個別に判断できる。
4. **シェル番地の受入を、ターゲット内の整合と配置の性質で検査する。** 共通の起動順を保ったまま PPC の配置を検討できる。
5. **FEP の公開条件と `root_pair` の配布条件を見直す。** 辞書・SQL・部品の準備条件を明示し、媒体をまたいだ版混在検出に必要な結合を判断する。

### 調べきれなかった範囲

- 既配布の PPC バイナリが依存する KAPI のスロット・刻印・世代、常駐シェル番地の利用実績。KDEBUG の positional imports を消費する全呼出箇所。
- 固定ページング帯のうち PowerMac が実際に使うページ数、領域を動かした場合の空き容量、HDD・ISO・TFTP ごとの FEP と pair の失敗時挙動。表の優先度は静的に確認した結合の範囲に基づく。
- 読み取り専用の調査として、ファイル変更、修正付き preflight、ビルド、lint、変異試験、QEMU・実機試験は未実施。
