# kapi/kapi_sys.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_D2_DESIGN・VER_INFO_DESIGN (D1 で移した)
ソース: `kapi/kapi_sys.c`

- kapi_sys.cのユーザー範囲検査→local snap→一度コピーとidx変換を維持。pci_getの40Bも固定。列挙/config/bind状態はPCI driver、BAR/台帳はD3 pci_resourcesのまま。

- `kapi_serial_diag`のNULL判定と0戻りを保ち `port_diag_get(&out->oe,...)` に変更。SerialDiagの並び・出力検査は既存どおり。

- ビルド日時は既存 kapi_sys.c の __DATE__ / __TIME__ を使う。新 formatter のコンパイル日時に置き換えない。

NULL／不一致は OS32_ERR_INVAL、書込みなし。成功 0、全体ゼロ初期化、NUL 終端。
- 既存生成ラッパの out 検査に従うだけで新しい権限モデルは導入しない。別サイズの将来 ABI は別エントリにする。

- Build 取得は kapi_sys.c 内の日時コピーを内部 helper に分離。旧 sys_get_build_info の report 副作用は互換維持、新経路は印字せず snapshot→formatter に集約。

- kapi_sys_ver_info の配置と処理順:

```c
/* kapi/kapi_sys.c; size/null check precedes this fragment */
VerSnapshot s;
ver_collect(&s);              /* does not collect Build */
build_time_copy(s.build, sizeof(s.build));
ver_format(&s, (OS32_VerInfo *)out);
return 0;
```
