# kdebug/recover_plan/profile.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・HWDEV_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_ADDENDUM_R2.md (D1 で移した)
ソース: `kdebug/recover_plan/profile.inc`

## GF6200_4A_DESIGN

| 対象 | 実装・根拠 |
| --- | --- |
| VGA I/O | 3xxhへin/outしない。3C2/3CC・3C3・3C4/5・3CE/FはBAR0+0C0000h+head×2000h+port、3D4/5はBAR0+601000h+head×2000h+port。`/home/hight/os32-tmp/refs/geforce6200/envytools/rnndb/display/nv3_prmvio.xml:11–15`、`nv3_pcrtc.xml:23–30`、`nv_vga.xml:336` |
| 代替可否 | 上記別名の存在は資料根拠あり。I/O BAR有効化による代替はしない |
| indexed処理 | indexへの書きもW権限が必要。条件評価・選択読取も書きop内だけ。CR44はhead0に書き、値3なら以後head1（`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:240–260`）。PRMVIOにもhead×2000hを加える（`/home/hight/os32-tmp/refs/geforce6200/envytools/docs/hw/mmio.rst:95`） |
| 除外ポート | 3C9・3DA/3BA等の副作用読取、未知port・indexは拒否。IO命令でread側が未定義のportは勝手に0を返さず、資料に基づく別名/意味が確定するまで止める |

## HWDEV_DESIGN

| 関数・データ単位の配置 | 判断 |
| --- | --- |
| recover.c:472–490 の GFX port 検査、zero_target (:534) と検証/復帰側の全呼出し | 核に残す。GF の zero 保持と共通 accessor/復帰の安全条件 |

## REALHW_RECOVER_ADDENDUM

- profile_okは表の形式検査と、測定値を受ける範囲検査の二段とする。INSPECT/S2は形式と全経路を先に調べ、未測定は「範囲検査待ち」であり検証完了ではない。通常のBAR利用planのarm前にはsnapshot・guard・cleanupを含む全到達targetについて範囲検査を完了させる。

- BAR相対実効offset（VGA別名/head strideを含む）をchecked演算で求め、`width <= length && offset <= length-width`、整列、base加算を検査する。VRAM範囲は全長で検査し予約範囲条件も併用する。BAR長はVRAM実容量の証明ではない。

- 未測定A1の固定planはPCI_CFGのprobe/復元と独立②だけで開始できる。測定自身だけは段階検証とし、A1内のBAR利用は候補長の検証・BAR復元・Mem有効の確認後のみ、必要targetの範囲検査を済ませる。未成立ならMMIO quiesce（①）を省いて安全な②へ進む。測定前abortを含めA1でGPUへの書込みが0件なら①の省略だけではFAILEDにしない（②等の失敗は既存どおり）。書込み済みで①が必要なのに実行できない場合は既存契約どおりFAILEDとする。全GF profileの未使用BAR行を理由にPCIだけの測定を循環依存にしない。

- A1は未測定BAR targetを含む表をbindできるが、snapshotで必要な範囲は開始前、plan内BARアクセスは候補長・生BAR復元・Command=0102hを確認してから段階検査する。

- S1追補完了時の別名禁止は BAR0 の PRMVIO/PRMCIO 各 head の実 port byte とアクセス幅の重なりだけを対象とする（既参照 envytools f102b82 `display/nv_vga.xml` 334–358 の vio/cio 群）

実読取り直前は r->selected と照合し

## REALHW_RECOVER_ADDENDUM_R2

- VGAのportは論理3xx番号、headは0/1。3C2/3CC・3C3・3C4/5・3CE/FはPRMVIO=`BAR0+0x0c0000+head*0x2000+port`、3D4/5はPRMCIO=`BAR0+0x601000+head*0x2000+port`（4a §5）。幅は1。

- INDEXEDはportを3C4/3CE/3D4のindex側で正規化しindex=0..255、data側はport+1。

- index 選択はdata書込みと同じtargetを使い、`substep=SELECT`、valueはtargetのindexと一致させる（fixtureのc03c4/c03c5も同じtargetの別substepへ対応付ける）。

- CR44はhead0で操作し値3で以後head1、値0でhead0。共有accessorがhead状態を管理し、不明値を推測しない。3C9・3DA/3BA等は従来どおり拒否する。

- accessorは元indexと分類済みlock/dataを保存し、CR1F/CR11の必要な解錠→data→lock→元indexの順を実装する。内部書込みもsubstep別ruleで許可し、失敗後にCOLD値を再生しない。

- profile_okを拡張し、key範囲、幅/整列/BAR範囲、mask/値/式、read安全性、phase、依存・guard、重複矛盾を検査する。根拠文字列はhost側のrule ID対応表またはコメントで検査し、カーネルに載せずCRCから外す。CRCはポインタ/構造体paddingでなく実行規則の全表の正規化値・参照先列を含める。
