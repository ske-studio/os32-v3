.text
.globl module_exports
module_exports:
.globl module_ref_acquire
module_ref_acquire:
.globl module_ref_release
module_ref_release:
.globl vfs_write
vfs_write:
.globl vfs_sync
vfs_sync:
.globl vfs_read
vfs_read:
.globl dma_free
dma_free:
.globl sysconfig_get_int
sysconfig_get_int:
.globl kprintf
kprintf:
.globl vfs_mkdir
vfs_mkdir:
.globl boot_image_info
boot_image_info:
.globl kutoa_dec
kutoa_dec:
.globl kdebug
kdebug:
.globl e82557_io_window
e82557_io_window:
.globl pci_bind_set_reason
pci_bind_set_reason:
.globl vfs_rm
vfs_rm:
.globl e82557_window_name
e82557_window_name:
.globl netcd_register
netcd_register:
ud2
.section .note.GNU-stack,"",@progbits
