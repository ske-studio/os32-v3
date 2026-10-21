"""S6 production PREPARE/RUN/VRAM composition and read-group contract."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar
ROOT = pathlib.Path(__file__).resolve().parents[2]

def command(exe):
    return ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-O2', '-nostdlib',
            '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', '-I'+str(ROOT/'tools/tests/recover_hostshim'),
            '-I'+str(ROOT/'include'), '-I'+str(ROOT/'sdk/include/os32'), str(ROOT/'tools/tests/gfinit_plan_host.c'), '-o', str(exe)]
MUTATIONS = [
    ('compare', 'if (args[i] != args[i+1]) *value = 0;', 'if (args[i] != args[i+1] && 0) *value = 0;'),
    ('capacity_branch', '*value=bytes>=4 && args[2]<=bytes-4;', '*value=1; (void)bytes;'),
    ('last_word', 'if(boundary>length)break;', 'if(boundary>=length)break;'),
    ('result_log', 'return recover_gf_result(r,out->saved_count,true,out->capacity_known,out->full_capacity,\n                             out->capacity_bytes,out->tested_bytes);', 'return 0;'),
    ('pattern', '((pass ^ word) & 1U)', '((pass | word) & 1U)'),
]

SHARED_MUTATIONS = [
    ('check_required', '(l->dir_substep != RECOVER_NONE || (l->flags & REC_LAYOUT_CHECK_EXPR))', '(l->dir_substep != RECOVER_NONE)'),
    ('check_reads', 'else if (bus_read(r, a->value, &values[i])) return access_error(r, a->value);', 'else values[i]=0;'),
]

def mutation(item):
    file, name, old, new = item
    source=(ROOT/file).read_text()
    if old and source.count(old)!=1: return name+': ambiguous anchor',1
    with tempfile.TemporaryDirectory(prefix='gf-plan-mut-') as td:
        exe=pathlib.Path(td)/'host'
        try:
            tree=mutpar.build_in_tree(ROOT,td,{file:source.replace(old,new,1) if old else source},[command(exe)],capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name+': compile failed '+str(exc.stderr),1
        result=host32.run([str(exe)],cwd=tree,timeout=60,capture_output=True)
        return name+(': GREEN' if not result.returncode else ': RED'),int(result.returncode==0)

def main():
    with tempfile.TemporaryDirectory(prefix='gf-plan-') as td:
        exe=pathlib.Path(td)/'host'
        subprocess.run(command(exe),check=True,cwd=ROOT)
        result=host32.run([str(exe)],cwd=ROOT,timeout=60)
        if result.returncode:return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,[('kdebug/gfinit_plan.c',*item) for item in MUTATIONS]+[('kdebug/recover.c',*item) for item in SHARED_MUTATIONS],('kdebug/gfinit_plan.c','control','',''))))
    return 0
if __name__=='__main__':sys.exit(main())
