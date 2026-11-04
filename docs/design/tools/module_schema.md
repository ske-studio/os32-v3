# tools/module_schema.py — 設計
> 状態: **現行 (2026-10-31)** — 出所: HWDEV_CORE_API・HWDEV_DESIGN (D1 で移した)
ソース: `tools/module_schema.py`

```python
dict(name='HWDEV', path='/boot/hwdev.mod', dependencies=(1,), required=False,
     module_id=11, phase=CONSTANTS['PHASE_POST_ROOT'],
     required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
     stop_allowed=0, ops_version=1, ops_slots=1,
     image_max=262144, mem_max=262144, load_span_max=327680,
     reloc_max=16384, export_max=1,
     init_symbol='hwdev_init', imports_symbol='hwdev_import_slots',
     ops_symbol='hwdev_ops', ops_names=('initialize',))
```

部品の ID・ops_version・容量の値は `tools/module_schema.py` の `COMPONENTS` を参照する。部品の容量と由来は [08_build の上限表](../../08_build.md#起動時に読む部品の大きさの上限と由来) に集約する。GEFORCE の現行 image/mem は 131,072 B、load_span は 163,840 B。層 3〜5 で広げる予定は [GF_FEAT3 の合算予算](../../decisions/realhw/GF_FEAT3_DESIGN.md#合算予算)。

部品ごとの import 数の上限は設けない。import はリンク時の未解決参照と、読み込み時の契約との件数・各シンボルの完全一致で検査する。テーブルが像に収まる照合は維持する。
