from pathlib import Path
import sys, subprocess
root=Path.cwd()
sys.path.insert(0,str(root))
from tools.ops.kernel_includes import include_flags
d=Path(__file__).resolve().parent
sources='sys/dev/net/e82557/e82557_desc.c sys/fs/serialfs/sfs_proto.c sys/fs/serialfs/sfs_client.c sys/fs/serialfs/sfs_lan.c sys/net/nic.c sys/net/nic_rx.c sys/net/rx_work.c lib/crc32.c sys/kern/kstring_c.c sys/kern/io_work.c sys/kern/io_policy.c sys/kern/idle_work.c sys/kern/irq/tick.c sys/kern/irq/tick_budget.c sys/hal/time/tick.c'.split()
subprocess.run(['gcc','-m32','-static','-std=gnu11','-ffreestanding','-ffunction-sections','-fdata-sections','-fno-pie','-no-pie','-Wl,--gc-sections','-O2','-D__cdecl=','-I'+str(d),'-I.','-Ibuild/out',*include_flags(root),d/'model.c',d/'boot.c',*sources,'-o',d/'model'],check=True)
