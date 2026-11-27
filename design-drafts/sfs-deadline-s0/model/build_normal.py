from pathlib import Path
import subprocess,sys
root=Path.cwd()
sys.path.insert(0,str(root))
from tools.ops.kernel_includes import include_flags
d=Path(__file__).resolve().parent
flags=['gcc','-m32','-static','-std=gnu11','-ffreestanding','-ffunction-sections','-fdata-sections','-fno-pie','-no-pie','-O2','-D__cdecl=','-I'+str(d),'-I.','-Ibuild/out','-Ibuild/out/lan',*include_flags(root)]
def run(cmd):
    subprocess.run(cmd,check=True)
obj=d/'facade.o'
run([*flags,'-c',d/'facade.c','-o',obj])
symbols=subprocess.check_output(['nm','--defined-only','-g',obj],text=True).splitlines()
renames=['--redefine-sym='+line.split()[-1]+'=facade_'+line.split()[-1] for line in symbols if line.split()[-1].startswith(('e82557_','sfs_lan_'))]
run(['objcopy',*renames,obj])
sources='sys/dev/net/e82557/lan_body.c sys/dev/net/e82557/e82557_desc.c sys/fs/serialfs/sfs_proto.c sys/fs/serialfs/sfs_client.c sys/fs/serialfs/sfs_lan.c sys/net/nic.c sys/net/nic_rx.c sys/net/rx_work.c lib/crc32.c sys/kern/kstring_c.c sys/kern/io_work.c sys/kern/io_policy.c sys/kern/idle_work.c sys/kern/irq/tick.c sys/kern/irq/tick_budget.c sys/hal/time/tick.c'.split()
run([*flags,'-Wl,--gc-sections',d/'normal.c',d/'boot.c',obj,*sources,d/'normal_unused.S','-o',d/'normal'])
