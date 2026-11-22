# 11-21 の本線のレビュー (本線の sol、B05 = PPC_ROOT_MOUNT_ALIGN_X86 と 7a = PPC_FEP_MODULE を本線の目で) — 本線 PM から 11-22 に受け取った本文
| 重さ | 設計・節 | 問題 | 直し案 |
| P1 | B05 §2–4、確定追記 | begin／rescan の公開 ABI と担当を記すよう求めているが、呼出規約が未定義。既存のディスク KAPI 7 本だけでは承認済み raw unit の保持・再認識・解放を表せない。 | 段3の前に公開口、計画の受渡し、既存 I/O への排他の効き方、終了・中断時の解放を確定し、KAPI 追加が必要なら本線の先行段にする。 |
| P2 | B05 §1・§3–4 | 固定名 platform_install_boot.h はあるが、既に install／cdinst が共有する inst_hdd／inst_disk と、hdprep の CHS 計画の分担がない。hdprep の空ディスク検査・書込み前確認・最終セクタの試し書きも受入から漏れている。 | 既存の共有経路を起点に、幾何・区画表・起動配備を機種側へ置き、hdprep の扱いを明記して PC-98 の確認・拒否・書込み順を回帰で固定する。 |
| P2 | B05 §2–4 | 共通範囲 API 案は、PC-98 の短い区画・宣言容量超過時の EXT2_ERR_NOPART を変える。VFS の OS32_ERR_IO が同じでも、直接の呼出しと既存試験には差が出る。 | 本線の ext2 分離段を明記し、既存 ext2_mount の失敗値・診断・キャッシュ初期化順を保ちながら platform_ext2_resolve に区画解決を移す。 |
| P1 | 7a §2・§3、確定追記／M7[3] §2–3 | 学習パスと取得結果の load 前設定、hooks.mk／ppc-fep-module.mk、私有 ops 5 は一致する。一方、本文は静的 SQL の起動成功だけを起点にしており、load と start の各直前の ready 照合、facade の RUNNING 条件、失敗時の pending 失効順が明記されていない。 | M7[3] の契約を段3の必須条件にし、公開を閉じる → inactive → 全ゼロ snapshot で take → handle を一度だけ回収、の順を境界試験で確認する。 |
| P2 | 7a §2–4／M7[3] §3–4 | origin/main の x86 FEP は私有 ops 4。7a の共通化と ops 5 への移行を一緒に比較すると、B-P 前後で x86 成果物の差ゼロを求める M7[3] の受入と基準が合わない。 | M7[3] 本線段で私有 ABI を更新し、7a の共通化を別段で比較・承認してから、B-P 単独の x86 差ゼロを確認する。公開 KAPI の番号・署名・版は維持する。 |
本線の目で可か: 現状は差し戻し。B05 は installer と ext2 の本線分担および管理要求の ABI、7a は B-P の公開・失効契約と比較基点を確定すれば可。
本線担当の段: B05 段2の PC-98 子 mount 共通化、ext2 resolver／範囲 API 分離の先行段、段3に先行する hdprep／install／cdinst の境界整理と管理要求の ABI 決定。7a は M7[3] の B1〜B8 と ime.c、段1の共有生成器、段3の FEP／SQLite 起動・公開・rollback 共通化、段4の共有 KAPI 生成器と描画権限口に差が及ぶ場合の変更。B-P の製品結線は PowerPC 担当。
(補足: M7[3] は 11-21 に本線で main に入り、私有 ops は 5 — 2 つ目の P2 の「x86 は ops 4」は今は解消済みのはず。r4 では今の main で確かめる。)
