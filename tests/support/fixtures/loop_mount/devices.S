.section .rodata
.globl fat_image
fat_image:
.incbin "fat.img"

/* Only the foreground LAN lifecycle and floppy reads are exercised. */
.text
.macro unused name
.globl \name
\name:
    ud2
.endm
unused clock_read_date
unused disk_write
unused diskio_set_hdd_drive
unused diskio_set_hdd_ide_phys_size
unused diskio_set_hdd_partition
unused diskio_set_hdd_sector_size
unused e82557_recv
unused e82557_send
unused fdc_media_gen
unused idle_wait_step
unused serial_gate_get
unused serial_gate_put
unused serial_gate_set
unused serial_get_status
unused serial_hold_dropped
unused serial_hold_take
unused serial_putchar
unused sfs_client_quiesce
unused sfs_ensure_session
unused sfs_persistent_generation
unused sfs_request_deadline
unused sys_time_now
.section .note.GNU-stack,"",@progbits
