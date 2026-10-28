"""S2 checked decoder, real layouts and synthetic inputs; no external ROM."""
import pathlib
import subprocess
import sys
import tempfile
import zlib
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def command(exe):
    return ['gcc', *FLAGS, '-O2', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'include'), str(ROOT/'tools/tests/gfinit_host.c'), '-o', str(exe)]


MUTATIONS = [
    ('static_read_cost', 'if (d->args[n->arg_first+j].kind == REC_ARG_READ) cost++;', 'if (false && d->args[n->arg_first+j].kind == REC_ARG_READ) cost++;'),
    ('skip_cr44_decode', 'if (op != GF_INIT_ZM_CR && op != GF_INIT_ZM_CR_GROUP && v->port', 'if (false && op != GF_INIT_ZM_CR && op != GF_INIT_ZM_CR_GROUP && v->port'),
    ('repeat_zero', 'if (!o->count || o->count >= 255)', 'if (o->count >= 255)'),
    ('repeat_c_overflow', 'if (!o->count || o->count >= 255)', 'if (!o->count)'),
    ('unknown_opcode', 'default: return -1;\n    }\n    if (span', 'default: fixed = 1; break;\n    }\n    if (span'),
    ('rom_crc', 'if (crc32_core_final(crc) != s->crc)', 'if (false && crc32_core_final(crc) != s->crc)'),
    ('copy_shift', 'if (!shift_ok(o->shift) || o->ref & 3U)', 'if ((!shift_ok(o->shift) && false) || o->ref & 3U)'),
    ('table_index', 'if (entry >= count)', 'if (false && entry >= count)'),
    ('call_depth', 'calls > GF_INIT_CALL_DEPTH', 'false'),
    ('repeat_depth', 'repeats > GF_INIT_REPEAT_DEPTH', 'false'),
    ('selection_coverage', '|| (o->mask >> o->shift) >= o->count', ''),
    ('sequence_overflow', 'o->count - 1 > (0xffffffffU - o->target) / 4', 'false'),
]

SHARED_MUTATIONS = [
    ('skip_head_flag', '(l->flags & REC_LAYOUT_SKIP_HEAD);', 'true;'),
    ('capture_ordinal', 'c->read_ordinal)', '0)'),
    ('capture_dominance', '!bit_get(in, plan->captures[j].producer_node)', 'false'),
    ('capture_consumed', 'r->capture_valid &= ~(1U << i);', 'r->capture_valid |= 1U << i;'),
    ('unresolved_bind', '!plan->entries || plan->unresolved_count ||', '!plan->entries ||'),
]


def mutation(item):
    file, name, old, new = item
    source = (ROOT/file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gfinit-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return f'{name}: compile failed {exc.stderr!r}', 1
        result = host32.run([str(exe)], cwd=tree, timeout=60, capture_output=True)
        return f'{name}: {"GREEN" if result.returncode == 0 else "RED"} rc={result.returncode}', int(result.returncode == 0)


def diagnostic_mutation(item):
    name, old, new = item
    file = 'tools/tests/gfinit_romfile_host.c'
    source = (ROOT / file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gfdiag-mut-') as td:
        result = mutpar.run_script_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                           'tools/tests/test_gfinit.py', capture_output=True, timeout=180,
                                           real=['tools/tests/gfinit_host.c'])
        return f'{name}: {"GREEN" if result.returncode == 0 else "RED"} rc={result.returncode}', int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='gfinit-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    with tempfile.TemporaryDirectory(prefix='gf-rom-fixture-') as td:
        temp = pathlib.Path(td)
        dump = temp / 'dump.c'
        dump.write_text('#define GF_S2_ENTRY unused_entry\n'
                        f'#include "{ROOT / "tools/tests/gfinit_host.c"}"\n'
                        'void _start(void) { fixture(); u32 at=GF_INIT_PLL_TABLE+4+35*2;\n'
                        'put(at+8,200,2); put(at+10,1000,2); put(at+14,1,2); put(at+18,500,2);\n'
                        'put(at+24,4,1); put(at+25,100,1); put(at+26,1,1); put(at+27,10,1); u32 result;\n'
                        '__asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), '
                        '"c"(rom), "d"(sizeof(rom)) : "memory");\n'
                        '__asm__ volatile("int $0x80" : : "a"(1), "b"(0)); '
                        '__builtin_unreachable(); }\n')
        exe = temp / 'dump'
        cmd = command(exe)
        cmd[cmd.index(str(ROOT / 'tools/tests/gfinit_host.c'))] = str(dump)
        subprocess.run(cmd, cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        if result.returncode:
            return result.returncode
        result_rom = result.stdout
        header = temp / 'synthetic_rom.h'
        header.write_text('static const u8 gf_private_rom[] = {' +
                          ','.join(str(b) for b in result.stdout) + '};\n')
        oracle_c = temp / 'oracle.c'
        oracle_c.write_text('#define GF_S2_ENTRY unused_entry\n'
                            f'#include "{ROOT / "tools/tests/gfinit_host.c"}"\n'
                            'void _start(void) { fixture(); u32 at=GF_INIT_PLL_TABLE+4+35*2;'
                            'put(at+8,200,2);put(at+10,1000,2);put(at+14,1,2);put(at+18,500,2);'
                            'put(at+24,4,1);put(at+25,100,1);put(at+26,1,1);put(at+27,10,1);'
                            'struct gfinit_pll_limits l; struct gfinit_pll_solution s; struct gfinit_pll_sequence q;'
                            'if(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&l)) goto fail;'
                            'for(u32 m=0;m<2;m++){if(m)l.vco2=(struct gfinit_pll_vco){0};'
                            'if(gfinit_pll_solve(&l,36000,&s)||gfinit_pll_sequence(GF_PLL_VIDEO,&l,&s,&q))goto fail;'
                            'const char *p=m?"single":"two";\n'
                            '#define X(name,val) print(p);print(name "=");number(val);print("\\n");\n'
                            'X(".n1",s.n1) X(".m1",s.m1) X(".n2",s.n2) X(".m2",s.m2) X(".p",s.p) X(".actual",s.actual) X(".error",s.error)'
                            'for(u32 i=0;i<q.count;i++){print(p);print(".write");number(i);print(".reg=");number(q.writes[i].reg);print("\\n");'
                            'print(p);print(".write");number(i);print(".source=");number(q.writes[i].source);print("\\n");'
                            'print(p);print(".write");number(i);print(".keep=");number(q.writes[i].keep);print("\\n");'
                            'print(p);print(".write");number(i);print(".value=");number(q.writes[i].value);print("\\n");}}'
                            '__asm__ volatile("int $0x80" : : "a"(1), "b"(0)); fail: '
                            '__asm__ volatile("int $0x80" : : "a"(1), "b"(1)); __builtin_unreachable();}\n')
        cmd = command(exe)
        cmd[cmd.index(str(ROOT / 'tools/tests/gfinit_host.c'))] = str(oracle_c)
        subprocess.run(cmd, cwd=ROOT, check=True)
        oracle_result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        if oracle_result.returncode:
            return oracle_result.returncode
        oracle = dict(line.split('=', 1) for line in oracle_result.stdout.decode().splitlines())
        cmd = command(exe)
        cmd[cmd.index(str(ROOT / 'tools/tests/gfinit_host.c'))] = str(ROOT / 'tools/tests/gfinit_romfile_host.c')
        cmd.insert(1, '-DGF_ROM_HEADER="' + str(header) + '"')
        subprocess.run(cmd, cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        if result.returncode:
            print(result.stdout.decode(), result.stderr.decode())
            return result.returncode
        output = result.stdout.decode()
        print(output, end='')
        fields = dict(line.split('=', 1) for line in output.splitlines())
        assert fields['video.row'] == '2' and fields['video.matches'] == '1'
        assert fields['video.fallback'] == '0' and fields['video.request_khz'] == '36000'
        assert fields['limits.vco2.max_freq'] == '1000000'
        assert int(fields['decode.rom_crc']) == zlib.crc32(result_rom)
        assert fields['later.count'] == '0'
        subprocess.run(cmd[:1] + ['-DGF_TEST_LATER'] + cmd[1:], cwd=ROOT, check=True)
        later_result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        assert later_result.returncode == 0
        later_fields = dict(line.split('=', 1) for line in later_result.stdout.decode().splitlines())
        assert later_fields['later.count'] == '10'
        for i in range(10):
            assert later_fields[f'later{i}.main'] == '3'
            assert later_fields[f'later{i}.pc'] == str(0xe7e0)
            assert later_fields[f'later{i}.substep'] == '489'
            assert later_fields[f'later{i}.ctx'] == str(7 + i)
            assert later_fields[f'later{i}.reg'] == str(0x680508)
        print('synthetic later: 10 writes in repeat')
        for mode in ('two', 'single'):
            for name in ('n1', 'm1', 'n2', 'm2', 'p', 'actual', 'error'):
                assert fields[mode + '.' + name] == oracle[mode + '.' + name]
            for i in range(5):
                for name in ('reg', 'source', 'keep', 'value'):
                    assert fields[f'{mode}.write{i}.{name}'] == oracle[f'{mode}.write{i}.{name}']
        # Remove the VIDEO key; the first synthetic row is the documented fallback.
        fallback = bytearray(result_rom)
        fallback[0x5bc8 + 4 + 35 * 2:0x5bc8 + 4 + 35 * 2 + 4] = (0x680524).to_bytes(4, 'little')
        header.write_text('static const u8 gf_private_rom[] = {' +
                          ','.join(str(b) for b in fallback) + '};\n')
        subprocess.run(cmd, cwd=ROOT, check=True)
        fallback_result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        assert fallback_result.returncode == 0
        fallback_fields = dict(line.split('=', 1) for line in fallback_result.stdout.decode().splitlines())
        assert fallback_fields['video.matches'] == '0' and fallback_fields['video.row'] == '0'
        assert fallback_fields['video.fallback'] == '1' and fallback_fields['video.offset'] == str(0x5bc8 + 4)
        print('fallback: selected row 0')
    if '--mutate' in sys.argv:
        diagnostic_bad = mutpar.run_with_control(diagnostic_mutation,
            [('diag_single', 'l.vco2=(struct gfinit_pll_vco){0};', '(void)l;'),
             ('diag_request', 'khz=v->value; found++;', 'khz=v->value+1; found++;'),
             ('diag_later_empty', 'i=boundary+1; i<decoded.plan.node_count; i++', 'i=boundary+1; i<boundary+1; i++')],
            ('diag_control', '', ''))
        return int(bool(diagnostic_bad | mutpar.run_with_control(mutation,
            [('kdebug/gfinit_decode.c', *item) for item in MUTATIONS] +
            [('kdebug/recover.c', *item) for item in SHARED_MUTATIONS],
            ('kdebug/gfinit_decode.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
