"""S6 real owner lifecycle with explicit platform/trial contract doubles."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = 'kdebug/gfinit.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']
INCLUDES = ['-I' + str(ROOT / d) for d in
            ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
             'sdk/include', 'sdk/include/os32', 'exec', 'gfx', 'drivers', 'kapi', 'kdebug')]
MUTATIONS = [
    ('max_nodes', 'if (!plan->node_count || plan->node_count > RECOVER_NODE_MAX) return -1;', ''),
    ('size_failure_retained', 'op != GF_OP_SIZE_BARS || r->state == REC_FAILED', 'op != GF_OP_SIZE_BARS'),
    ('inspect_replaces_s0_ops', 'g->s0 = s0;', 'g->s0 = s0; (void)recover_owner_configure(&g->ops, &g->bus, s0, false);'),
    ('begin_leaves_s0_ops', 'recover_owner_configure(&g->ops, &g->bus, g->s0, false) ||', ''),
    ('idle_abort', 'owner && owner->inspected && r->state == REC_IDLE', 'false'),
    ('external_recover_release', 'if (owner && (r->state == REC_VERIFIED || r->state == REC_FAILED))', 'if (owner && (r->state == REC_VERIFIED || r->state == REC_FAILED) && false)'),
    ('cache_invalidate', 'g->last_op = RECOVER_NONE;', ''),
    ('flow_size', 'plan->node_count * RECOVER_BITS_WORDS * sizeof(u32)', 'RECOVER_NODE_MAX * RECOVER_BITS_WORDS * sizeof(u32)'),
    ('decode_release', 'gf_free(g->decoded, sizeof(*g->decoded));', ''),
    ('plan_release', 'gf_free(g->compiled, sizeof(*g->compiled));', ''),
    ('page_round', 'pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, PAGE_ALIGN_UP(bytes) / PAGE_SIZE)', 'pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, bytes / PAGE_SIZE)'),
    ('inspect_release', 'op != GF_OP_INSPECT && (op != GF_OP_SIZE_BARS || r->state == REC_FAILED)', '(op != GF_OP_SIZE_BARS || r->state == REC_FAILED)'),
    ('write_flag', '(op != GF_OP_INSPECT && !(q.flags & GF_ALLOW_WRITE))', 'false'),
    ('session', 'if (q.session != r->token.session) return -1;', ''),
    ('main_order', 'q.script_id == owner->next_main &&', ''),
    ('rom_release', 'gf_free(g->rom, GF_ROM_MAX);', ''),
    ('size_end', 'if (size) return trial_end(r, &r->token, REC_NORMAL);', 'if (size) return 0;'),
]

def command(exe):
    return ['gcc', *FLAGS, *INCLUDES, '-nostdlib', '-static', '-no-pie',
            str(ROOT/'tools/tests/gfinit_owner_host.c'), '-o', str(exe)]

def mutation(item):
    name, old, new = item
    source = (ROOT/SOURCE).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-owner-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {SOURCE: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
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
        subprocess.run(command(exe), check=True, cwd=ROOT)
        result = host32.run([str(exe)], cwd=ROOT, timeout=30)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS, ('control', '', ''))))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
