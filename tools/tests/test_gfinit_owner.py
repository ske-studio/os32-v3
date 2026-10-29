"""S6 owner lifecycle and S7a/S7c shell/ROM output through the real console."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = 'kdebug/gfinit.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffunction-sections', '-fdata-sections', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']
INCLUDES = ['-I' + str(ROOT / d) for d in
            ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
             'sdk/include', 'sdk/include/os32', 'exec', 'gfx', 'drivers', 'kapi', 'kdebug')]
MUTATIONS = [
    ('cli_mode_c', 'q.flags |= GF_MODE_C;', 'q.flags |= 0;', 'userland/shell/cmd_gfinit.c'),
    ('modeset_flag', 'op == GF_OP_MODESET && (owner->prepare_flags & GF_MODE_C)', 'op == GF_OP_MODESET'),
    ('modeset_order', 'owner->next_main == GF_INIT_MAINS && r->state == REC_PAUSED', 'true'),
    ('cache_flags', ' && owner->last.flags == q.flags', ''),
    ('cache_flags_subset', 'owner->last.flags == q.flags',
     '(owner->last.flags & q.flags) == owner->last.flags'),
    ('vpll_budget_rows', 'rows += RECOVER_VPLL_ROWS;', 'rows += 0;'),
    ('read_plan_identity', 'recover_owner()->plan == &owner->compiled->plan', 'true'),
    ('begin_expand', 'if (!g->compiled || recover_gf_profile_expand(&g->profile, g->compiled)) return -1;',
     'if (!g->compiled) return -1;'),
    # Ignoring the refusal is equivalent: hwdev_claim rejects the retained lease.
    ('inspect_detach_refusal', 'if (gf_release_rom(g)) return -1;', 'if (gf_release_rom(g)) return 0;'),
    ('cli_write_gate', 'argc != expected || !str_eq(argv[argc - 1], "--allow-write")',
     'argc != expected && !str_eq(argv[argc - 1], "--allow-write")', 'userland/shell/cmd_gfinit.c'),
    ('cli_main_range', 'q.script_id >= GF_INIT_MAINS', '0', 'userland/shell/cmd_gfinit.c'),
    ('cli_op_swap', 'op = GF_OP_ABORT;', 'op = GF_OP_INSPECT;', 'userland/shell/cmd_gfinit.c'),
    ('cli_nosys_success', 'return rc ? 1 : 0;', 'return rc && rc != -10 ? 1 : 0;', 'userland/shell/cmd_gfinit.c'),
    ('chip_fixed', 'return rom[data + GF_BIT_CHIP_OFFSET];', 'return GF_VERSION_CHIP;', 'hwdev/gf/rom.inc'),
    ('lease_generation', 'owner->generation, r->token.session', 'gf_lease.generation, r->token.session'),
    ('lease_entry', 'if (owner && owner->inspected && !gf_owned(r)) {', 'if (false) {'),
    ('idle_unowned_release', 'if (r->state == REC_IDLE || r->state == REC_VERIFIED) gf_release_rom(owner);', ''),
    ('detach_before_free', 'recover_owner_configure(0, 0, 0, false)', '0'),
    ('paused_work_retained', 'if (owner && !stage.initialized && (r->state == REC_VERIFIED || r->state == REC_FAILED))', 'if (owner && ((void)r, 1))'),
    ('max_nodes', 'if (!plan->node_count || plan->node_count > RECOVER_NODE_MAX) return -1;', ''),
    ('size_failure_retained', 'op != GF_OP_SIZE_BARS || r->state == REC_FAILED', 'op != GF_OP_SIZE_BARS'),
    ('inspect_replaces_s0_ops', 'g->s0 = s0;', 'g->s0 = s0; (void)recover_owner_configure(&g->ops, &g->bus, s0, false);'),
    ('begin_leaves_s0_ops', 'recover_owner_configure(&g->ops, &g->bus, g->s0, false) ||', ''),
    ('idle_abort', 'owner && owner->inspected && r->state == REC_IDLE', 'false'),
    ('external_recover_release', 'if (owner && !stage.initialized && (r->state == REC_VERIFIED || r->state == REC_FAILED))', 'if (owner && (r->state == REC_VERIFIED || r->state == REC_FAILED) && false)'),
    ('cache_invalidate', 'g->last_op = RECOVER_NONE;', ''),
    ('flow_size', 'plan->node_count * RECOVER_BITS_WORDS * sizeof(u32)', 'RECOVER_NODE_MAX * RECOVER_BITS_WORDS * sizeof(u32)'),
    ('decode_release', 'gf_free(g->decoded, sizeof(*g->decoded));', ''),
    ('plan_release', 'gf_free(g->compiled, sizeof(*g->compiled));', ''),
    ('page_round', 'pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, PAGE_ALIGN_UP(bytes) / PAGE_SIZE)', 'pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, bytes / PAGE_SIZE)'),
    ('inspect_release', 'op != GF_OP_INSPECT && (op != GF_OP_SIZE_BARS || r->state == REC_FAILED)', '(op != GF_OP_SIZE_BARS || r->state == REC_FAILED)'),
    ('write_flag', '(op != GF_OP_INSPECT && !(q.flags & GF_ALLOW_WRITE))', 'false'),
    ('session', 'if (op != GF_OP_C_ONLY && q.session != r->token.session) return -1;', ''),
    ('main_order', 'q.script_id == owner->next_main &&', ''),
    ('rom_release', 'gf_free(g->rom, GF_ROM_MAX);', ''),
    ('size_end', 'if (size) return trial_end(r, &r->token, REC_NORMAL);', 'if (size) return 0;'),
]

def command(exe, probe=False):
    return ['gcc', *FLAGS, *(['-DGF_VPLL_PROBE=1'] if probe else []), *INCLUDES, '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
            str(ROOT/'tools/tests/gfinit_owner_host.c'), str(ROOT/'lib/kprintf.c'),
            str(ROOT/'platform/pc98/console_attr.c'), str(ROOT/'platform/pc98/console.c'), '-o', str(exe)]

def mutation(item):
    name, old, new = item[:3]
    path = item[3] if len(item) == 4 else SOURCE
    source = (ROOT/path).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-owner-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {path: source.replace(old, new, 1) if old else source},
                                       [command(exe, name == "vpll_budget_rows")], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return f'{name}: compile failed {exc.stderr!r}', 1
        try:
            result = host32.run([str(exe)], cwd=tree, timeout=30, capture_output=True)
        except subprocess.TimeoutExpired:
            print(f'{name}: TIMEOUT', flush=True)
            raise
        return f'{name}: {"GREEN" if result.returncode == 0 else "RED"}', int(result.returncode == 0)

def main():
    with tempfile.TemporaryDirectory(prefix='gf-owner-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(['gcc', '-std=gnu89', '-pedantic-errors', '-I'+str(ROOT),
                        '-x', 'c', '-c', '-o', str(pathlib.Path(td)/'wire.o'), '-'],
                       input='typedef unsigned int u32;\n#include "kdebug/gfinit_wire.h"\n'
                             'typedef char wire_size[sizeof(struct gfinit_request)==40 ? 1 : -1];\n',
                       text=True, check=True)
        assert '"gfinit", shell_cmd_gfinit,' in (ROOT/'userland/shell/cmd_sys.c').read_text()
        assert 'kdebug/gfinit_wire.h' in (ROOT/'build/programs.mk').read_text()
        subprocess.run(command(exe), check=True, cwd=ROOT)
        result = host32.run([str(exe)], cwd=ROOT, timeout=30)
        if result.returncode:
            return result.returncode
        subprocess.run(command(exe, True), check=True, cwd=ROOT)
        result = host32.run([str(exe)], cwd=ROOT, timeout=30)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS, ('control', '', ''))))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
