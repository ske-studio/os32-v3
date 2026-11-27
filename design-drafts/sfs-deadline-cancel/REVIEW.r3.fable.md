# fable reviewer の r3 確認 (2026-11-27): 指摘は全部直った。PPC の側に新しく P2 1・P3 3、確定してよい (P2 を確定版に入れれば往復不要)
読んだ: 3936aaa1b の DESIGN.r3.md 全文、bd01609c0 の serialfs_persistent.c 125–165・hostfs_link_io.c・ppc-powermac.mk 252–261・ppc_fixture.py の差し替え。x86 側は見ていない。
P2 §2 片道 BYE の所有: hostfs_link の put_until は受理時点で 0 を返し、送り出しは後の poll (hostfs_link_io.c:139–152)。片道成功の直後・HELLO 前に cancel すると間に poll が無いので OPEN 前に必ず消える → link では seq 上限の BYE は「常に送られない」。今は BYE pending の間 HELLO の put が -1 で、idle の poll が BYE を送り終えてから HELLO が入る。挙動の変化が設計に無い。直し: 「hostfs_link では seq 更新の BYE は送られない (HELLO が古いセッションを置き換える、serialfs_persistent.c:146 の注記どおり)」を §2 に明記、§3 の seq 上限の小試験に通常の道の期待 (BYE の WDATA 0・HELLO は最初の put で受理)、ホストが BYE 無しの HELLO で古い sid を捨てる事を 1 行で確かめる。「期限に達している時だけ cancel」案は HELLO の出口で BYE が残る道が戻るので勧めない。
P3-1 §5: 第 4・5 枠は build/targets/ppc-powermac.mk の変更 (今は KB_MUT・KB_MUT_EXTRA・KB_MUT_THIRD の 3 つ) → 表に足し、ビルド規則の変更で全件が選ばれる事を段 3 の所要に見込む。既存 3 枠を使う fixture (ATA は 3 枠とも) と同じ像で重ならない事も 1 行。
P3-2 §3 ppc_hfs_cancel の変異: 「表の cancel を 0」の old は定義と表の両方に名前が出て一意にならない恐れ → 表の末尾の並び (例「pm_hfs_put_until, pm_hfs_cancel」) を old に。
P3-3 §7: 製品の保証の言い方 (期限を過ぎて最初の前景の観測で閉じる) は PM 決定になった → 票の決定の一覧に 1 行 (何を保証とし、jiffy 側の失効はいつ諮るか)。
問題なし: cancel の照合、POLL が先に handle を放しても受付の記録から rid を引ける作り、幅 50 の判定の位置、段 2・3 の分け方。
