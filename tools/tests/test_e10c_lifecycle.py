"""e10c real gcap ownership/release, polled stop marker, and int80 VM branch."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
from test_shm_user import function, ROOT

PATHS = ('kernel/v86_gcap.c','exec/exec.c','kernel/ring3_entry.asm','kernel/kernel.c','kernel/kselftest.c')
MUTANTS = [
 ('post-exec-body',PATHS[4],'    check(kselftest_run_audit("post-exec") == 0, "post-exec: lifecycle audit");','', 'FAIL post-exec body audit'),
 ('selftest-rebuild',PATHS[0],'    if (g->mode == V86G_MODE_ROM || v86_session.aborting)','    if (1)', 'FAIL restored == 0 && cursor == 0'),
 ('gcap-free',PATHS[0],'    kfree(tv);\n    kfree(g);\n    v86_gcap_free_count', '    v86_gcap_free_count','FAIL freed == 2'),
 ('gcap-ops',PATHS[0],'    gcap_ops = 0;','    (void)0;','FAIL !gcap_owned'),
 ('gcap-TV',PATHS[0],'    tv_restore(tv);','    (void)tv;','FAIL freed == 2 && restored == 1 && cursor == 1'),
 ('stop-mark',PATHS[1],'    exec_stop_count++;','    (void)0;','FAIL exec_stop_count == 1 && polled == 1'),
 ('stop-serial',PATHS[1],'    serial_puts_polled("OS32: exec teardown stopped\\r\\n");','    (void)0;','FAIL exec_stop_count == 1 && polled == 1'),
 ('VM-gate',PATHS[2],'        jnz     .dispatch','        nop','FAIL host_vm_gate(0x20000) == 0'),
 ('probe-before-only',PATHS[3],'    kselftest_run_post_probe();','','FAIL audit after probe'),
]

def run(runner, mutant=None):
    texts={p:(ROOT/p).read_text() for p in PATHS}
    if mutant:
        _,path,old,new,_=mutant
        assert old in texts[path]
        if mutant[0] == 'gcap-ops':
            body = function(texts[path], 'v86_gcap_release')
            assert body.count(old) == 1
            texts[path] = texts[path].replace(body, body.replace(old, new))
        else:
            texts[path]=texts[path].replace(old,new)
    k=texts[PATHS[3]]
    try:
        assert 'kselftest_run_post_probe();' in k and k.index('gfx_prepare_backend();') < k.index('kselftest_run_post_probe();'), 'FAIL audit after probe'
        body = function(texts[PATHS[4]],'kselftest_run_post_exec')
        assert 'check(kselftest_run_audit("post-exec") == 0,' in body, 'FAIL post-exec body audit'
        assert 'kselftest_run_audit("AS-resume")' not in texts[PATHS[1]], 'FAIL resume audit'
        for name in ('exec_exit','exec_pending_transfer','exec_pending_finish'):
            b=function(texts[PATHS[1]],name)
            assert 'exec_stop_mark(); for (;;) { _stop(); }' in b
    except AssertionError as e:
        return 1,str(e)
    with tempfile.TemporaryDirectory(prefix='e10c-life-') as d:
        tmp=Path(d)
        (tmp/'gcap_slice.inc').write_text(''.join(function(texts[PATHS[0]],n) for n in ('gcap_cui_rebuild','v86_gcap_release','v86_gdc_capture')))
        (tmp/'stop_slice.inc').write_text(function(texts[PATHS[1]],'exec_stop_mark'))
        asm=texts[PATHS[2]]
        branch=asm[asm.index('        test    dword [esp + 40]'):asm.index('        mov     eax, esp')]
        # Execute the actual saved-EFLAGS offset/branch; replace STI's privileged
        # effect with its observable IF-set result. Segment loads are outside it.
        branch=branch.replace('        sti','        inc dword [host_if]')
        (tmp/'vm.asm').write_text('cpu 386\nsection .bss\nhost_if: resd 1\nsection .text\nglobal host_vm_gate\nhost_vm_gate:\n mov eax,[esp+4]\n mov dword [host_if],0\n push eax\n sub esp,40\n'+branch+' add esp,44\n mov eax,[host_if]\n ret\nsection .note.GNU-stack noalloc noexec nowrite progbits\n')
        subprocess.run(['nasm','-f','elf32',str(tmp/'vm.asm'),'-o',str(tmp/'vm.o')],check=True)
        source=(ROOT/'tools/tests/e10c_gcap_host.c').read_text().replace('static void run(void) {','extern int host_vm_gate(u32 flags);\nstatic void run(void) {\n    CHECK(host_vm_gate(0x20000) == 0);\n    CHECK(host_vm_gate(0x202) == 1);')
        (tmp/'fixture.c').write_text(source)
        cmd=['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-ffreestanding','-fno-builtin','-fno-pie','-fno-stack-protector','-nostdlib','-static','-no-pie']
        cmd+=['-I'+str(tmp)]+['-I'+str(ROOT/p) for p in ('include','kernel','sdk/include/os32')]
        r=host32.build(cmd+[str(tmp/'fixture.c'),str(tmp/'vm.o'),'-o',str(tmp/'test')],capture_output=True,text=True)
        assert r.returncode == 0,r.stderr
        r=host32.run([str(tmp/'test')],runner=runner,capture_output=True,text=True,timeout=60)
        return r.returncode,r.stdout+r.stderr

@host32.control_session
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner',choices=['native','qemu'],default='native')
    p.add_argument('--mutate',action='store_true')
    a=p.parse_args();host32.begin_control(a.mutate,a.runner,ROOT)
    with host32.control(a.mutate,a.runner,ROOT) as normal:
        if normal:
            rc,out=run(a.runner);print(out,end='');assert rc == 0,rc
    if a.mutate:
        for m in MUTANTS:
            rc,out=run(a.runner,m);assert rc == 1 and m[4] in out,(m[0],rc,out)
            print('RED',m[0])
if __name__ == '__main__': main()
