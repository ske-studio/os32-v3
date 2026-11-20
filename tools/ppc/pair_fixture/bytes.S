.section .pair_low,"a"
.globl __kernel_image_start,ppc_kernel_entry,__root_record_start,__root_record_end
.globl __kernel_file_end,__kernel_load_mem_end
__kernel_image_start:
.long 0x11223344
ppc_kernel_entry:
__root_record_start:
.byte 0x0c,0x04,0xaa,0x66
.byte 1,2,3,4,5,6,7,8,9,10,11,12
__root_record_end:
.long 0x55667788
__kernel_file_end:
.set __kernel_load_mem_end,__kernel_file_end+8
.section .pair_high,"a"
.globl __root_image_start,__root_file_end,__root_load_end
__root_image_start:
.ascii "highLOAD"
__root_file_end:
__root_load_end:
