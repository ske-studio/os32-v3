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
    MAGIC=0x444f4d4b, FORMAT_VERSION=1,
    PHASE_POST_ROOT=2, FLAG_PAIR=1,
)
# Fixed resident record budget; adding an eleventh component needs a new decision.
MODULE_RECORD_CAP = 10
KDEBUG_OPS_NAMES = (
    'selftest_run', 'selftest_post_exec', 'selftest_post_unicode',
    'selftest_post_probe', 'crt_diag_bootlog', 'gcap_capture',
    'gcap_active', 'gcap_keep_if', 'gcap_insn_abort', 'gcap_port_passed',
    'gcap_pass_in', 'gcap_pass_out', 'gcap_note_in', 'gcap_note_out',
    'gcap_detach', 'gcap_release', 'nic_selftest', 'nic_trace', 'dbg_call',
)
# These fields also define struct module_component_policy's generated row order.
POLICY_FIELDS = (
    'module_id', 'phase', 'required_flags', 'allowed_flags', 'stop_allowed',
    'ops_version', 'ops_slots', 'image_max', 'mem_max',
    'reloc_max', 'import_max', 'export_max', 'load_span_max',
)
COMPONENTS = (
    dict(
        name='KDEBUG', path='/boot/kdebug.bin',
        dependencies=(), required=False,
        module_id=1, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=3, ops_slots=len(KDEBUG_OPS_NAMES),
        image_max=262144, mem_max=262144,
        reloc_max=16384, import_max=1024, export_max=64, load_span_max=524288,
        init_symbol='kdebug_init', imports_symbol='kdebug_import_slots',
        ops_symbol='kdebug_ops', ops_names=KDEBUG_OPS_NAMES,
    ),

    # T4c-1 paired POST_ROOT SQLite; initialize returns the typed facade.
    dict(
        name='SQLITE', path='/boot/sqlite.mod',
        dependencies=(), required=True,
        module_id=2, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS["FLAG_PAIR"], allowed_flags=CONSTANTS["FLAG_PAIR"], stop_allowed=0,
        ops_version=2, ops_slots=1, image_max=524288, mem_max=524288,
        reloc_max=4096, import_max=32, export_max=1, load_span_max=544768,
        init_symbol='sqlite_module_init', imports_symbol='sqlite_import_slots',
        ops_symbol='sqlite_module_ops', ops_names=('initialize',),
    ),

    # T5a-2 paired FEP depends on the private SQLite engine at POST_ROOT.
    dict(
        name='FEP', path='/boot/fep.mod',
        dependencies=(2,), required=True,
        module_id=3, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=96, export_max=1, load_span_max=81920,
        init_symbol='fep_module_init', imports_symbol='fep_import_slots',
        ops_symbol='fep_module_ops', ops_names=('initialize',),
    ),

    # T5b-5: ATAPI and ISO9660 share one POST_ROOT body and registration lifetime.
    dict(
        name='ATAPI_ISO', path='/boot/cd.mod',
        dependencies=(), required=False,
        module_id=4, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=32, export_max=1, load_span_max=81920,
        init_symbol='cd_module_init', imports_symbol='cd_import_slots',
        ops_symbol='cd_module_ops', ops_names=('initialize',),
    ),

    # T5c-1: typed ROM glyph reader; Unicode and old font path stay core.
    dict(
        name='KCG', path='/boot/kcg.mod',
        dependencies=(), required=False,
        module_id=5, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=16384, mem_max=81920,
        reloc_max=256, import_max=2, export_max=1, load_span_max=86016,
        init_symbol='kcg_module_init', imports_symbol='kcg_import_slots',
        ops_symbol='kcg_module_ops', ops_names=('initialize',),
    ),

    # T5c-2a: FM driver and snd share a tick and Key-OFF lifetime.
    dict(
        name='FM', path='/boot/fm.mod',
        dependencies=(), required=False,
        module_id=6, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=16, export_max=1, load_span_max=81920,
        init_symbol='fm_module_init', imports_symbol='fm_import_slots',
        ops_symbol='fm_module_ops', ops_names=('initialize',),
    ),

    # T5c-2b: PCM keeps IRQ/tick and DMA stop evidence in one lifetime.
    dict(
        name='PCM', path='/boot/pcm.mod',
        dependencies=(), required=False,
        module_id=7, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=32, export_max=1, load_span_max=81920,
        init_symbol='pcm_module_init', imports_symbol='pcm_import_slots',
        ops_symbol='pcm_module_ops', ops_names=('initialize',),
    ),

    # T5c-3: BIOS/I/O body; mandatory PTE and exception returns remain core.
    dict(
        name='V86', path='/boot/v86.mod',
        dependencies=(), required=False,
        module_id=8, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=48, export_max=1, load_span_max=81920,
        init_symbol='v86_module_init', imports_symbol='v86_import_slots',
        ops_symbol='v86_module_ops', ops_names=('initialize',),
    ),

    # T5c-4: NIC and SerialFS LAN transport share IRQ/tick/session lifetime.
    dict(
        name='LAN', path='/boot/lan.mod',
        dependencies=(), required=False,
        module_id=9, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=48, export_max=1, load_span_max=81920,
        init_symbol='lan_module_init', imports_symbol='lan_import_slots',
        ops_symbol='lan_module_ops', ops_names=('initialize',),
    ),

    # T5c-5b: HAL/board/chip body; permanent windows and identify stay core.
    dict(
        name='CIRRUS', path='/boot/cirrus.mod',
        dependencies=(), required=False,
        module_id=10, phase=CONSTANTS['PHASE_POST_ROOT'],
        required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
        stop_allowed=0, ops_version=1, ops_slots=1,
        image_max=65536, mem_max=65536,
        reloc_max=2048, import_max=16, export_max=1, load_span_max=81920,
        init_symbol='cirrus_module_init', imports_symbol='cirrus_import_slots',
        ops_symbol='cirrus_module_ops', ops_names=('initialize',),
    ),
)


def validate_components(rows=COMPONENTS):
    """Reject incomplete policies before either mkmod or C generation uses them."""
    def require(ok, message):
        if not ok:
            raise ValueError(message)
    require(len(rows) <= MODULE_RECORD_CAP, 'module record capacity exceeded')
    ids = [row['module_id'] for row in rows]
    require(len(set(ids)) == len(ids), 'duplicate module ID')
    require(set(ids) == set(range(1, len(rows) + 1)), 'missing or unknown module ID')
    require(set(range(1, MODULE_RECORD_CAP + 1)) <= set(ids), 'missing component policy')
    for field in ('name', 'path'):
        require(len({row[field] for row in rows}) == len(rows), 'duplicate ' + field)
    by_id = {row['module_id']: row for row in rows}
    visiting, done = set(), set()
    def visit(mid):
        require(mid in by_id, 'unknown dependency ID')
        require(mid not in visiting, 'dependency cycle')
        if mid in done:
            return
        visiting.add(mid)
        row = by_id[mid]
        require(row['phase'] == CONSTANTS['PHASE_POST_ROOT'], 'unknown module phase')
        require(len(set(row['dependencies'])) == len(row['dependencies']), 'duplicate dependency')
        for dep in row['dependencies']:
            visit(dep)
            require(by_id[dep]['phase'] <= row['phase'], 'dependency phase')
        visiting.remove(mid)
        done.add(mid)
    for mid in ids:
        visit(mid)


validate_components()
ALLOWED_MODULE_IDS = {row['name']: row['module_id'] for row in COMPONENTS}
CONSTANTS.update(('ID_' + name, value) for name, value in ALLOWED_MODULE_IDS.items())
POLICIES = {row['module_id']: row for row in COMPONENTS}
