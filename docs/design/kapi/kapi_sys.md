# sys/kapi/kapi_sys.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_D2_DESIGN・VER_INFO_DESIGN (D1 で移した)
ソース: `sys/kapi/kapi_sys.c`、`sys/kapi/kapi_sysinfo.c`

- kapi_sys.cのユーザー範囲検査→local snap→一度コピーとidx変換を維持。pci_getの40Bも固定。列挙/config/bind状態はPCI driver、BAR/台帳はD3 pci_resourcesのまま。

- `kapi_serial_diag`のNULL判定と0戻りを保ち `port_diag_get(&out->oe,...)` に変更。SerialDiagの並び・出力検査は既存どおり。

- 共通の `kapi_sysinfo.c` が `kapi_mem_stat` を実装する。PowerMac では診断欄は 0 で、`kstack_high_water = 0` は未計測を含む。

- ビルド日時は共通 TU の `sys/kapi/kapi_sysinfo.c` の `__DATE__` / `__TIME__` を使う。

NULL／不一致は OS32_ERR_INVAL、書込みなし。成功 0、全体ゼロ初期化、NUL 終端。
- 既存生成ラッパの out 検査に従うだけで新しい権限モデルは導入しない。別サイズの将来 ABI は別エントリにする。

- Build 取得は 共通の `kapi_sys_build_time_copy` を使用。旧 sys_get_build_info の report 副作用は互換維持、新経路は印字せず snapshot→formatter に集約。

- kapi_sys_ver_info の配置と処理順:

```c
/* sys/kapi/kapi_sys.c; size/null check precedes this fragment */
VerSnapshot s;
ver_collect(&s);              /* does not collect Build */
kapi_sys_build_time_copy(s.build, sizeof(s.build));
ver_format(&s, (OS32_VerInfo *)out);
return 0;
```
