# kapi/ — カーネル API の入口と共有契約

> 状態: **現行 (2026-10-31)**

KAPI のラッパーと個別機能の入口を置く。
呼出し元のメモリと各 target の引数をそれぞれ検査する。

## 落とし穴

- CPL=3 から呼んでも target はカーネル権限で実行されるため、座標や添字から
  作った番地はポインタの出力検査だけでは守れない。target 側で範囲を検査し、
  漢字は右側のセルも確保する (`platform/pc98/console.c:35`, `platform/pc98/console.c:51`)。
  加算は先に始点を検査し `count <= limit - first` の形で溢れを避ける。
  共有領域の列数を容量の正典にせず (`kapi/kapi_db.c:797`)、NULL と長さも
  入出力ごとに検査する。`make check-kapi-bounds-host` が境界を検査する。
