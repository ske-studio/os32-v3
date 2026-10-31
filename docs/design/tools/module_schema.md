# tools/module_schema.py — 設計
> 状態: **現行 (2026-10-31)** — 出所: HWDEV_CORE_API・HWDEV_DESIGN (D1 で移した)
ソース: `tools/module_schema.py`

```python
dict(name='HWDEV', path='/boot/hwdev.mod', dependencies=(1,), required=False,
     module_id=11, phase=CONSTANTS['PHASE_POST_ROOT'],
     required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
     stop_allowed=0, ops_version=1, ops_slots=1,
     image_max=196608, mem_max=196608, load_span_max=262144,
     reloc_max=16384, import_max=128, export_max=1,
     init_symbol='hwdev_init', imports_symbol='hwdev_import_slots',
     ops_symbol='hwdev_ops', ops_names=('initialize',))
```

> [要確認] tools/module_schema.py の現行 MODULE_RECORD_CAP は GEFORCE 追加により 12。HWDEV の module_id=11 は維持される。

KDEBUG/SQLITE/FEP/LAN は既存 KMOD。image/mem 上限は各 256/256、512/512、64/64、64/64 KiB。FEP は SQLITE 依存の先例
