## r1→r2 の変更

- P1　bind と packed の初回・再 init の成功時に、確定面と表示を同じ初期色に揃える。
- P2-1　GDC の走査順、PPC の空の表示フック、OF の提示元を明記した。
- P2-2　矩形転送の呼出し元ごとに原点を維持するか、両方をゼロにするかを決めた。
- P2-3　CLUT の再送を含め、操作ごとの二つのカウンタの差分を定めた。
- P2-4　所有者の画面が有効な間に子アプリから呼び、検査の削除を画素で検出する。
- P2-5　独立した不在集合と生成ラッパーの呼出し先を別々に検証する。
- P2-6　両 CPU で正常版と変異版を同じ待機段まで進め、同じ座標の RGB を比較する。

完了の基準の案　PowerMac の公開 KAPI に正負の行数を渡すと、最後に表示した像が論理行単位で循環し、影への未提示の書込み、保護帯、端末復帰を含む host 試験とゲスト画面照合が通る。

## 1. 実物の確認

基点は origin/main。調査 ppc-kapi-next/OUT.md:11 のとおり、共通本体は既に像にある。前提を実装に落とす際の食い違いは次の 3 点。
1. 「hardware」は内容の移動を指さない。platform/pc98/gfx_scroll.c:25–38 は表示ページ先頭と y 行目から二つの表示区間を作り、y..H−1、0..y−1 の順に走査する。200 行モードの倍化は表示ライン数だけに掛かる。
2. platform/powermac/display.c:15–22 のコメントは端末の影でのスクロールを指すが、この関数は全引数を捨てる。5c 設計 PPC_KAPI_GFX_REST_DESIGN.md:66 が公開を保留したのは、原点だけ更新されるため。
3. 「影から矩形を転送」だけでは足りない。drivers/video/of_fb/backend_of_fb.c:37–39,101–106 の影はアプリが編集する面。これを scroll 時に丸ごと送ると present 前の画素まで見える。

統合の判断は次の 3 点。
1. gfx/gfx_scroll.c:12–20 の加算・正規化と display_scroll の口を共有する。PC-98 は GDC、PowerMac はこの口から OF の GfxBackend 実装へ委譲する。
2. [HW1] 表示への書込みは OF 後端に集約する。端末の文字生成・フォント・改行は kernel/console のまま。of_fb_v2.c:59–83 の端末と gfx2 の提示にも既存の矩形転送を使う。
3. 公開は sdk/kapi.json:698–703 の既存 slot。sdk/kapi_target_ppc.py:22–25 の gfx 不在集合は現在 2 本。5c 当時の集合を復元せず scroll だけを外す。

## 2. 振る舞いと境目

void gfx_hardware_scroll(int lines)。正は画面を上へ、負は下へ動かし、外れた行を反対端に出す。空白の補充はない。
H は有効な論理画面高。+1 の後に -1 で元の像、±H と 0 は同じ原点。INT_MIN に対しても絶対値を取らない。
戻り値はなく、初期化前・所有権失効後は状態も画面も変更しない。公開入口では共通の提示許可判定を原点の加算より先に置く。
初期化の内部経路は原点を直接 0 に設定できるよう、公開入口の検査と既存の正規化本体を分ける。
OF では固定 INDEX8 面の有効高を H とする。bind／モード設定で共通側の高との一致を保証し、PC-98 の高さ・ページ倍率を持ち込まない。

```c
/* 共通本体。bb_b と正の H が確立した後。 */
int y = (vram_scroll_y + lines % H) % H;
if (y < 0) y += H;
vram_scroll_y = y;
display_scroll(y, H, gfx_flip_enabled, gfx_display_page);
```

OF に最後に提示した論理画素を保持するカーネル専用 INDEX8 面を 1 枚置く。CLIENT の pitch と番地、dirty 座標は変えない。
専用面の pitch は幅 W。bind 時に W×H の積と確保を検査し、失敗なら公開前に bind を失敗させる。unbind 時に解放する。
起動 bind は検証・確保・CLUT 初期化後、live を公開する前に次の reset_frame を実行する。初期色は論理 index 0。物理保護帯は保存する。
packed の初回 init と再 init は gfx_core_init.inc の成功分岐で高さ確定後、既存の GfxBackend.enter を使う。OF の noop を専用 enter に替える。擬似コードを示す。

```c
reset_origins() { origin = 0; gfx_scroll_reset_common(); } /* 共通側は vram_scroll_y = 0 のみ */
reset_frame() {
    reset_origins();
    memset(saved, 0, W * H);
    emit_saved_visible();              /* 現 CLUT、padding・保護行を除く */
}
of_fb_enter() { if (live && gfx_started) reset_frame(); }
```

再 bind も新規確保後に同じ reset_frame を通す。CLIENT は初期画面の入力にしない。初回 present 前に回しても全行が 0、最初が部分 present なら確定した語以外は 0 のまま。
通常の v1 present は論理矩形を面内に切り、従来どおり左右を 4 画素語に丸め、該当画素だけを専用面へ確定する。
その後、同じ語幅だけを移動先へ送る。負の座標や巨大な端点の処理は of_fb_copy の既存の広い整数での切詰めを流用する。
次はクリップ後の半開区間 [x0,x1)、[s0,s1) の行境界。origin は正規化済みで、保護帯は物理表示の上端に固定する。

```c
for (int s = s0; s < s1; ++s) {
    int d = s - origin;
    if (d < 0) d += H;
    if (commit) copy_indices(saved + s * W + x0, src + s * pitch + x0, x1 - x0);
    if (d < protected_rows) continue;
    emit_words(display + d * display_pitch + x0, saved + s * W + x0, x1 - x0);
}
```

copy_indices は論理 INDEX8 のコピー。emit_words は backend_of_fb.c:59–85 の CLUT 変換・整列語ストアを切り出し、実転送バイトだけを加算する。
保護帯に隠れた論理行も確定するので、後で可視部へ回った時の内容が決まる。padding は描かない。
scroll は原点変更時だけ commit=false で全面を送る。未変更時の 0 行は転送不要。コミット回数はアプリの present ごとに従来どおり一度。
パレット再描画（backend_of_fb.c:140）も確定済み面を新 CLUT と現在原点で再送する。CLIENT の再提示を兼ねさせない。
v2 の mode_set／flip は面全体を表示する操作なので原点 0 で置換する。端末も同じ経路で先頭行から再描画する。
v1 の present 群 → present_rect → of_fb_present → of_fb_copy は全面指定でも両原点を保持する。CLUT 変更はこの経路から確定面再送へ移す。
v2 mode_set → shim_mode_set、page flip → shim_flip、端末の画面設定・再提示 → 同じ shim の各入口 → surface_copy → of_fb_copy が全面置換の経路。
surface_copy は引数・転送範囲の検証をすべて済ませ、失敗し得ないコピー区間の先頭で reset_origins() を呼ぶ。矩形コピー本体は原点を変更しない。失敗時は旧状態を保つ。公開の提示許可を端末の内部 flip に要求しない。
協調切替下で検証・面確定・転送・原点同期の途中に yield を入れない。保存するのは画素であり、提示元の一時ポインタではない。
追加 RAM は 800×600 で 480,000 B、追加 VRAM は 0。既存 display の上限は pitch×H（backend_of_fb.c:26–31）。
1 回の移動に最大 W×(H−保護行数) B の表示書込み。1 行移動でも全面が変わる。OF に vblank はなく（display.c:34–35）、走査との競合で途中像が見え得る。
消去を挟まず各語を一度だけ書く。受入は呼出し完了後の像を比較し、速度は転送量とゲストでの連続呼出し時間を記録する。

## 3. ファイルごとの変更と大きさ

以下は実装差分の概算行数。試験・契約の更新を含め約 2 人日。

- gfx/gfx_scroll.c（約 15 行）で公開入口の所有者・世代検査と内部初期化を分離する。既存の提示許可の口を使い、内部ヘッダに共通原点だけをゼロにする関数を宣言する。
- platform/powermac/display.c（約 5 行）で正規化済み原点を OF 後端へ渡す。表示ページは固定面のため加算しない。
- drivers/video/of_fb/backend_of_fb.h（約 2 行）に後端内部の原点設定の宣言を足す。
- drivers/video/of_fb/backend_of_fb.c（約 85 行）に確定済み面の寿命、共通の語転送、矩形の座標変換、再送を実装する。
- drivers/video/of_fb/of_fb_v2.c（約 6 行）で検証済みの全面置換を原点 0 で転送し、成功時に共通原点を同期する。
- sdk/kapi_target_ppc.py（1 行減）で gfx_hardware_scroll を除外から外す。
- tools/ppc/test_kapi_binding.py（約 4 行）で独立した不在期待と通常ラッパーの実ターゲットを更新する。
- of_fb_host.c（約 65 行）に異なる pitch、保護行、色変換を持つ小画面の期待配列を置く。
- test_of_fb.py（約 20 行）に下記の後端変異を加える。
- dispprb の既存ゲストソース（約 45 行）に行模様・矩形更新・段階別の待機を加える。
- tools/accept/ppc_display.py（約 35 行）に待機マーカーと画面照合を加える。
- PPC の KAPI 契約書 §9（約 4 行）に行の単位、循環、保護帯、vblank 非保証を記す。

## 4. 受入 → 試験と変異

計数の期待は (Δcommits, Δpresent_bytes)。F=W×(H−protected_rows)、R=丸めた矩形語幅のバイト数×移動先が可視の行数。
v1 矩形成功は (1,R)、空矩形成功は (1,0)。変更のある scroll は (0,F)、同一原点は (0,0)。
CLUT の scan_index が変われば従来の成功コミットを保ち (1,F)、写像不変・拒否は (0,0)。変更時も未提示 CLIENT は読まない。
bind・packed enter・v2/端末の全面置換は各 (0,F)、検証失敗は (0,0)。host で各操作の前後差を個別に比較する。

各行は独立した受入。変異は指定の比較が失敗した時に検出とする。

- 初期化 → 起動 bind、初回 init、非零原点・非零色からの再 init の各々で、部分提示より先の scroll と部分提示を先にしたケースを独立に試す。全可視画素と保護帯番兵を比較する。変異は enter の reset_frame を削除。
- 正負と折返し → host で行番号を色にし、+1、−2、±H、INT_MIN、INT_MAX の各回を広い整数の期待像と比較。変異は d の減算を加算へ。
- 確定済み像 → host で CLIENT の一部を未提示のまま回し、旧色を確認。変異は再送元を CLIENT に変更。
- 矩形 → host で原点をまたぐ 2 行と非整列 x を提示し、両端語・padding・未指定行を比較。変異は表示側にも source pitch を使用。
- 保護帯 → host で物理先頭の番兵と、保護帯から出た論理行の更新色を確認。変異は保護判定を s に変更。
- 色と計数 → host で CLUT 変更後の非零原点の色と実転送バイト差を比較。変異は再描画の原点を 0 に固定。
- 原点失効 → host で v2 mode_set、v2 flip、端末再提示、unbind／再 bind の後に +1 し初期原点からの像を比較。変異は原点同期を削除。
- ゲスト → 7450・7455 で dispprb が非対称の行模様を提示し、+1、部分更新、−1 の各段で停止。s.wait で段階名を待ち、画面取得後に次へ進める。上下端・中央・保護帯を RGB で照合。変異は display_scroll の委譲だけを削除。7450・7455 各々で同一プローブと入力を使い、正常版・変異版とも +1 完了の同じ s.wait マーカーに到達させる。物理最初の可視行の標本は正常版で次の論理行の RGB、変異版で元の行の RGB。後者の画素不一致で RED とし、起動失敗・待機 timeout は RED に数えない。
- 復帰 → 同じゲストで shutdown 後に scroll を呼び、端末文字の画面と再 init 後の原点を照合。別段で親所有者が非対称模様を提示し、所有権を保持したまま既存の子アプリ経路から +1 を呼ぶ。子の復帰マーカーで親の再提示前に撮影する。正常版は元の行色、検査だけを削った版は隣の行色となることを照合する。
- 公開表 → ppc-kapi-binding の固定 EXPECTED_ABSENT_SLOTS から scroll だけを削り、生成元集合との完全一致を維持する。gfx_osri_show は残す。公開期待に scroll を足し、生成ラッパー内の gfx_hardware_scroll(lines) 呼出しを検査する。ppc-kapi-table は配置とラッパー定義を受け持つ。変異は scroll を不在へ戻す。
- 指紋 → 同一 sdk/kapi.json から両機種の表を再生成し、slot 順・型・ABI 指紋が基準値と一致、PPC の束縛差分が当該 1 本だけと確認。束縛を含む期待指紋は生成結果に追従。変異は生成前に lines の型を変更。

## 5. 未決

0 件。循環表示と未提示画素の分離は既存の提示契約から決まり、ゲスト試験に合格した段階で当該 slot を公開する。

## 確定 (2026-11-23、sol 第 2 回の条件付き承認、PM が反映)
本文と食い違う所はこの節を正とする。
- **R2-P1 起動の enter**: PowerMac の起動のコードは GfxBackend.enter を display_text_hide で上書きしているので、後端の noop を差し替えるだけでは packed の初回・再 init が画面を初期化しない。platform/powermac/display_provider.c の enter に既存の display_text_hide() と OF の初期化 (原点と確定の面) を組み込み、後者は gfx_is_started() で成功を確かめてから行う。
- **R2-P2 原点のリセットの順**: v2 の全面の置き換えでは、転送元・pitch・bytes の検証を共通にし、検証が通った直後に両方の原点 (OF の後端と共通の vram_scroll_y) を 0 にしてから、失敗しない共通の転送へ進む (§2 と §3 の書き方の違いはこの順に揃える)。of_fb_copy の転送元の拒否の条件も同じ検証に入れる。
- **R2-P2 空の矩形**: 正の幅・高さでクリップの後に空なら (1,0)、幅か高さが正でなければ OS32_ERR_INVAL と (0,0) (of_fb_present の拒否と揃える)。
