# T2e e4: kernel framebuffer / backend geometry

設計票: [TASK_T2D_T2H §2](../../docs/tasks/v3/TASK_T2D_T2H.md)。
PM 判断: 作業依頼の `ref_e4.md` §8 Q1〜Q10。独立レビューP2-1を受け、PMがQ3を訂正 (公開query/互換fbは選択backend)。

`test_gfx_kernel_fb.py --runner qemu --mutate` は **正常496チェック、10/10 runtime RED**。
実物の gfx_core、PC98/PEGC/Cirrus backend、gfx_vram/scroll、pgalloc/sys/paging、
surface_query/lease、B1 redir_access/access_walk を連結する。
識別/probe、port I/O、Cirrus 下位装置 I/O、IRQ/MMU はホスト足場。
PEGC probe の代役は台帳の CLIENT からドライバ私有の s_bb_phys を設定する。
init/prepare/shutdown/query と選択ロジック・SURFACE 登録/取得は実物。
通常のアプリ状態/所有者は fixture、B1 の検証と copy は実物。

- reserve 前の NULL と present/dirty/raster/scroll の安全性。
- boot reserve 直後の BB と32000B stride。
- 実3 backend の各選択→prepare→init→init_200→shutdown。
  PC98 は400→200、packed は480、未起動の内部fbはPC98 400、公開query/互換fbは選択CLIENTとbackend query。
  pegcchk/hal_testのinit前format判定とgfx_bb_phys_rangeのUSER写像範囲を照合。
  内部fb・互換fb・query・台帳の backing/format/pitch/plane offset を照合。
  200行の台帳heightは400のまま (Q2)。
- 選択 CLIENT publisher、planar DISPLAYのB/R/G/I順、PEGC DISPLAYの
  実surface_leaseでRW/UC PTE・release、Cirrus DISPLAYのnot-ready。
- AUTOのCirrus→PEGC→PC98 fallback、強制候補の失敗→PC98、CLIENT欠落。init後probe失敗直後のbb全4面とfb、prepareなしPC98→Cirrusも照合。
- 正常対照はmakeのHOST32_RUNNERS全員、変異は先頭だけ。
  今回はsandbox指定のqemu。64bit既存splash試験は別。

| 変異 | 期待する最初のFAILの対象 |
|---|---|
| fixed-bb | reserve前のBBがNULL |
| rounded-stride | reserve後の32000B stride |
| display-wb | UC不一致でPEGC DISPLAYが登録されない |
| display-absent | PEGC DISPLAY登録の存在 |
| preinit-backend | prepare後の未起動fbが400行 |
| init200-rebind | 再設定欠落で起動成功せずfbが400行のまま |
| preinit-screen-pc98 | init前queryが選択backendのformat |
| preinit-public-pc98 | init前互換fbが選択backendのgeometry |
| fallback-bind | init後probe失敗直後のbbがPC98 CLIENT |
| packed-bind | prepareなし切替後のbbが選択CLIENT |

変異は一時ソースだけを編集。置換数1を要求し、exit=1かつ上記に対応する
固有FAIL文言を照合する。コンパイル失敗・signal・timeoutはREDに数えない。
初回足場の存在しないos32x.h/余分な_memcpy_w宣言、caller未保存、
別々のIRQ変数による終了rc=2は修正し、正常対照成立後に6変異を確認した。P2/P3修正で4変異を追加、合計10本。
追加試験の初回はvoidのgfx_bb_phys_rangeをCHECKへ渡してコンパイル失敗、修正後rc=0。

既存対照: gfx_bootは18試験/17変異、display_cleanupは13条件/16変異、
boot_splash_nativeは2 unittest/8構成。faultはGDC START後にplanesを2面へ減らし、
planes[2..3]欠落の枝で描画せずshutdownすることを確認。PEGC DISPLAYがCLIENT確保失敗時も
登録されること、予約/写像失敗時は登録されないことはgfx_bootで確認。
既存display_cleanupの変異用コピーにexecの新依存ヘッダ2本を追加した。
ゲスト/実装置のprobe・画面表示・同期性能はこの試験の保証外。
