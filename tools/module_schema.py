"""KMOD v1 wire order and component policies (TASK_T4_T6_MODULES §2-1).

gen_module_schema.py emits the C view; mkmod consumes this source directly.
SQLite prototype limits are measured by tools/sqlite_module_proto.py.
"""

HEADER_FIELDS = (
    'MAGIC', 'FORMAT', 'HEADER_SIZE', 'IMAGE_SIZE',
    'KAPI', 'LAYOUT', 'ABI', 'ID', 'PHASE', 'FLAGS',
    'MEM_SIZE', 'PAYLOAD_OFF', 'PAYLOAD_SIZE',
    'TEXT_OFF', 'TEXT_SIZE', 'RO_OFF', 'RO_SIZE',
    'DATA_OFF', 'DATA_SIZE', 'BSS_OFF', 'BSS_SIZE',
    'RELOC_OFF', 'RELOC_COUNT', 'IMPORT_OFF', 'IMPORT_COUNT',
    'EXPORT_OFF', 'EXPORT_COUNT', 'NAMES_OFF', 'NAMES_SIZE',
    'INIT', 'STOP', 'PAIR_CRC', 'IMAGE_CRC', 'OPS_VERSION',
    'RESERVED0', 'RESERVED1',
)
CONSTANTS = dict(
    HEADER_SIZE=144, PAGE_SIZE=4096, NAME_MAX=63, RECORD_SIZE=12,
    MAGIC=0x444f4d4b, FORMAT_VERSION=1, ID_KDEBUG=1, ID_SQLITE=2,
    PHASE_POST_ROOT=2, FLAG_PAIR=1,
)
KDEBUG_OPS_NAMES = (
    'selftest_run', 'selftest_post_exec', 'selftest_post_unicode',
    'selftest_post_probe', 'crt_diag_bootlog', 'gcap_capture',
    'gcap_active', 'gcap_keep_if', 'gcap_insn_abort', 'gcap_port_passed',
    'gcap_pass_in', 'gcap_pass_out', 'gcap_note_in', 'gcap_note_out',
    'gcap_detach', 'gcap_release', 'nic_selftest', 'nic_trace',
)
# These fields also define struct module_component_policy's generated row order.
POLICY_FIELDS = (
    'module_id', 'phase', 'required_flags', 'allowed_flags', 'stop_allowed',
    'ops_version', 'ops_slots', 'image_max', 'mem_max',
    'reloc_max', 'import_max', 'export_max', 'load_span_max',
)
POLICIES = {
    CONSTANTS['ID_KDEBUG']: dict(
        module_id=CONSTANTS['ID_KDEBUG'], phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=2, ops_slots=len(KDEBUG_OPS_NAMES),
        image_max=262144, mem_max=262144,
        reloc_max=16384, import_max=1024, export_max=64, load_span_max=524288,
        init_symbol='kdebug_init', imports_symbol='kdebug_import_slots',
        ops_symbol='kdebug_ops', ops_names=KDEBUG_OPS_NAMES,
    ),
}

# T4a-2 VA0 measurement gate; no product SQLite loading until T4c.
POLICIES[CONSTANTS['ID_SQLITE']] = dict(
    module_id=CONSTANTS['ID_SQLITE'], phase=CONSTANTS['PHASE_POST_ROOT'],
    required_flags=0, allowed_flags=0, stop_allowed=0,
    ops_version=1, ops_slots=1, image_max=524288, mem_max=524288,
    reloc_max=4096, import_max=32, export_max=1, load_span_max=544768,
    init_symbol='sqlite_module_init', imports_symbol='sqlite_import_slots',
    ops_symbol='sqlite_module_ops', ops_names=('initialize',),
)
