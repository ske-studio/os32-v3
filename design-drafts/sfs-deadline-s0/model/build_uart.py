from pathlib import Path
import sys, subprocess
root=Path.cwd()
sys.path.insert(0,str(root))
from tools.ops.kernel_includes import include_flags
d=Path(__file__).resolve().parent
sources='sys/fs/serialfs/sfs_proto.c sys/fs/serialfs/sfs_client.c sys/fs/serialfs/sfs_lan.c sys/dev/char/serial_plan.c lib/crc32.c sys/kern/kstring_c.c'.split()
subprocess.run(['gcc','-m32','-static','-std=gnu11','-ffreestanding','-ffunction-sections','-fdata-sections','-fno-pie','-no-pie','-Wl,--gc-sections','-O2','-D__cdecl=','-I'+str(d),'-I.','-Ibuild/out',*include_flags(root),d/'uart.c',d/'uart_unused.S',*sources,'-o',d/'uart'],check=True)
