/* Generated i386 cdecl tail-jumps preserve variadic calls. */
.section .text.kdebug_imports,"ax",@progbits
.code32
.globl kprintf
.type kprintf, @function
kprintf:
    jmp *kdebug_import_slots+0
.size kprintf, .-kprintf
.globl appmem_init
.type appmem_init, @function
appmem_init:
    jmp *kdebug_import_slots+4
.size appmem_init, .-appmem_init
.globl appmem_map
.type appmem_map, @function
appmem_map:
    jmp *kdebug_import_slots+8
.size appmem_map, .-appmem_map
.globl appmem_unmap
.type appmem_unmap, @function
appmem_unmap:
    jmp *kdebug_import_slots+12
.size appmem_unmap, .-appmem_unmap
.globl appslot_abort_admit_selftest
.type appslot_abort_admit_selftest, @function
appslot_abort_admit_selftest:
    jmp *kdebug_import_slots+16
.size appslot_abort_admit_selftest, .-appslot_abort_admit_selftest
.globl appslot_alloc_id
.type appslot_alloc_id, @function
appslot_alloc_id:
    jmp *kdebug_import_slots+20
.size appslot_alloc_id, .-appslot_alloc_id
.globl appslot_at
.type appslot_at, @function
appslot_at:
    jmp *kdebug_import_slots+24
.size appslot_at, .-appslot_at
.globl appslot_gfx_owner_selftest
.type appslot_gfx_owner_selftest, @function
appslot_gfx_owner_selftest:
    jmp *kdebug_import_slots+28
.size appslot_gfx_owner_selftest, .-appslot_gfx_owner_selftest
.globl appslot_reclaim
.type appslot_reclaim, @function
appslot_reclaim:
    jmp *kdebug_import_slots+32
.size appslot_reclaim, .-appslot_reclaim
.globl appslot_resume_mark_selftest
.type appslot_resume_mark_selftest, @function
appslot_resume_mark_selftest:
    jmp *kdebug_import_slots+36
.size appslot_resume_mark_selftest, .-appslot_resume_mark_selftest
.globl appslot_trim_done
.type appslot_trim_done, @function
appslot_trim_done:
    jmp *kdebug_import_slots+40
.size appslot_trim_done, .-appslot_trim_done
.globl appslot_trim_request_as
.type appslot_trim_request_as, @function
appslot_trim_request_as:
    jmp *kdebug_import_slots+44
.size appslot_trim_request_as, .-appslot_trim_request_as
.globl as_access_page
.type as_access_page, @function
as_access_page:
    jmp *kdebug_import_slots+48
.size as_access_page, .-as_access_page
.globl bootinfo_get
.type bootinfo_get, @function
bootinfo_get:
    jmp *kdebug_import_slots+52
.size bootinfo_get, .-bootinfo_get
.globl bootinfo_hdd_geom
.type bootinfo_hdd_geom, @function
bootinfo_hdd_geom:
    jmp *kdebug_import_slots+56
.size bootinfo_hdd_geom, .-bootinfo_hdd_geom
.globl bootinfo_parse
.type bootinfo_parse, @function
bootinfo_parse:
    jmp *kdebug_import_slots+60
.size bootinfo_parse, .-bootinfo_parse
.globl bootinfo_seal
.type bootinfo_seal, @function
bootinfo_seal:
    jmp *kdebug_import_slots+64
.size bootinfo_seal, .-bootinfo_seal
.globl bootinfo_seal_image
.type bootinfo_seal_image, @function
bootinfo_seal_image:
    jmp *kdebug_import_slots+68
.size bootinfo_seal_image, .-bootinfo_seal_image
.globl bootlog_compose
.type bootlog_compose, @function
bootlog_compose:
    jmp *kdebug_import_slots+72
.size bootlog_compose, .-bootlog_compose
.globl bootlog_format_header
.type bootlog_format_header, @function
bootlog_format_header:
    jmp *kdebug_import_slots+76
.size bootlog_format_header, .-bootlog_format_header
.globl bootlog_is_active
.type bootlog_is_active, @function
bootlog_is_active:
    jmp *kdebug_import_slots+80
.size bootlog_is_active, .-bootlog_is_active
.globl bootlog_len
.type bootlog_len, @function
bootlog_len:
    jmp *kdebug_import_slots+84
.size bootlog_len, .-bootlog_len
.globl caller_access_enter
.type caller_access_enter, @function
caller_access_enter:
    jmp *kdebug_import_slots+88
.size caller_access_enter, .-caller_access_enter
.globl caller_access_get
.type caller_access_get, @function
caller_access_get:
    jmp *kdebug_import_slots+92
.size caller_access_get, .-caller_access_get
.globl caller_access_leave
.type caller_access_leave, @function
caller_access_leave:
    jmp *kdebug_import_slots+96
.size caller_access_leave, .-caller_access_leave
.globl con_sink_disable
.type con_sink_disable, @function
con_sink_disable:
    jmp *kdebug_import_slots+100
.size con_sink_disable, .-con_sink_disable
.globl con_sink_enable
.type con_sink_enable, @function
con_sink_enable:
    jmp *kdebug_import_slots+104
.size con_sink_enable, .-con_sink_enable
.globl con_sink_selftest
.type con_sink_selftest, @function
con_sink_selftest:
    jmp *kdebug_import_slots+108
.size con_sink_selftest, .-con_sink_selftest
.globl console_get_cursor_x
.type console_get_cursor_x, @function
console_get_cursor_x:
    jmp *kdebug_import_slots+112
.size console_get_cursor_x, .-console_get_cursor_x
.globl console_get_cursor_y
.type console_get_cursor_y, @function
console_get_cursor_y:
    jmp *kdebug_import_slots+116
.size console_get_cursor_y, .-console_get_cursor_y
.globl console_set_cursor
.type console_set_cursor, @function
console_set_cursor:
    jmp *kdebug_import_slots+120
.size console_set_cursor, .-console_set_cursor
.globl copy_caller_cstr
.type copy_caller_cstr, @function
copy_caller_cstr:
    jmp *kdebug_import_slots+124
.size copy_caller_cstr, .-copy_caller_cstr
.globl copy_to_caller
.type copy_to_caller, @function
copy_to_caller:
    jmp *kdebug_import_slots+128
.size copy_to_caller, .-copy_to_caller
.globl cpu_delay_us
.type cpu_delay_us, @function
cpu_delay_us:
    jmp *kdebug_import_slots+132
.size cpu_delay_us, .-cpu_delay_us
.globl cpu_loops_per_tick
.type cpu_loops_per_tick, @function
cpu_loops_per_tick:
    jmp *kdebug_import_slots+136
.size cpu_loops_per_tick, .-cpu_loops_per_tick
.globl db_v50_selftest
.type db_v50_selftest, @function
db_v50_selftest:
    jmp *kdebug_import_slots+140
.size db_v50_selftest, .-db_v50_selftest
.globl dma8237_ready
.type dma8237_ready, @function
dma8237_ready:
    jmp *kdebug_import_slots+144
.size dma8237_ready, .-dma8237_ready
.globl dma_above_1mb_state
.type dma_above_1mb_state, @function
dma_above_1mb_state:
    jmp *kdebug_import_slots+148
.size dma_above_1mb_state, .-dma_above_1mb_state
.globl dma_alloc
.type dma_alloc, @function
dma_alloc:
    jmp *kdebug_import_slots+152
.size dma_alloc, .-dma_alloc
.globl dma_chan_mask
.type dma_chan_mask, @function
dma_chan_mask:
    jmp *kdebug_import_slots+156
.size dma_chan_mask, .-dma_chan_mask
.globl dma_chan_setup
.type dma_chan_setup, @function
dma_chan_setup:
    jmp *kdebug_import_slots+160
.size dma_chan_setup, .-dma_chan_setup
.globl dma_chan_state
.type dma_chan_state, @function
dma_chan_state:
    jmp *kdebug_import_slots+164
.size dma_chan_state, .-dma_chan_state
.globl dma_crosses_64k
.type dma_crosses_64k, @function
dma_crosses_64k:
    jmp *kdebug_import_slots+168
.size dma_crosses_64k, .-dma_crosses_64k
.globl dma_free
.type dma_free, @function
dma_free:
    jmp *kdebug_import_slots+172
.size dma_free, .-dma_free
.globl dma_mark_leaked
.type dma_mark_leaked, @function
dma_mark_leaked:
    jmp *kdebug_import_slots+176
.size dma_mark_leaked, .-dma_mark_leaked
.globl dma_pool_bad_free
.type dma_pool_bad_free, @function
dma_pool_bad_free:
    jmp *kdebug_import_slots+180
.size dma_pool_bad_free, .-dma_pool_bad_free
.globl dma_pool_free_pages
.type dma_pool_free_pages, @function
dma_pool_free_pages:
    jmp *kdebug_import_slots+184
.size dma_pool_free_pages, .-dma_pool_free_pages
.globl dma_pool_init
.type dma_pool_init, @function
dma_pool_init:
    jmp *kdebug_import_slots+188
.size dma_pool_init, .-dma_pool_init
.globl dma_pool_leaked
.type dma_pool_leaked, @function
dma_pool_leaked:
    jmp *kdebug_import_slots+192
.size dma_pool_leaked, .-dma_pool_leaked
.globl exec_heap_used
.type exec_heap_used, @function
exec_heap_used:
    jmp *kdebug_import_slots+196
.size exec_heap_used, .-exec_heap_used
.globl exec_kapi_layout_selftest
.type exec_kapi_layout_selftest, @function
exec_kapi_layout_selftest:
    jmp *kdebug_import_slots+200
.size exec_kapi_layout_selftest, .-exec_kapi_layout_selftest
.globl exec_tramp_page_addr
.type exec_tramp_page_addr, @function
exec_tramp_page_addr:
    jmp *kdebug_import_slots+204
.size exec_tramp_page_addr, .-exec_tramp_page_addr
.globl exec_tramp_user_selftest
.type exec_tramp_user_selftest, @function
exec_tramp_user_selftest:
    jmp *kdebug_import_slots+208
.size exec_tramp_user_selftest, .-exec_tramp_user_selftest
.globl fd_is_redirected
.type fd_is_redirected, @function
fd_is_redirected:
    jmp *kdebug_import_slots+212
.size fd_is_redirected, .-fd_is_redirected
.globl fd_redirect_buf_write_ok
.type fd_redirect_buf_write_ok, @function
fd_redirect_buf_write_ok:
    jmp *kdebug_import_slots+216
.size fd_redirect_buf_write_ok, .-fd_redirect_buf_write_ok
.globl fd_redirect_to_buffer
.type fd_redirect_to_buffer, @function
fd_redirect_to_buffer:
    jmp *kdebug_import_slots+220
.size fd_redirect_to_buffer, .-fd_redirect_to_buffer
.globl gfx_selected_selfcheck
.type gfx_selected_selfcheck, @function
gfx_selected_selfcheck:
    jmp *kdebug_import_slots+224
.size gfx_selected_selfcheck, .-gfx_selected_selfcheck
.globl gui_ime_set_render
.type gui_ime_set_render, @function
gui_ime_set_render:
    jmp *kdebug_import_slots+228
.size gui_ime_set_render, .-gui_ime_set_render
.globl irq_deferred_count
.type irq_deferred_count, @function
irq_deferred_count:
    jmp *kdebug_import_slots+232
.size irq_deferred_count, .-irq_deferred_count
.globl irq_dyn_index
.type irq_dyn_index, @function
irq_dyn_index:
    jmp *kdebug_import_slots+236
.size irq_dyn_index, .-irq_dyn_index
.globl irq_quarantine_line
.type irq_quarantine_line, @function
irq_quarantine_line:
    jmp *kdebug_import_slots+240
.size irq_quarantine_line, .-irq_quarantine_line
.globl irq_register
.type irq_register, @function
irq_register:
    jmp *kdebug_import_slots+244
.size irq_register, .-irq_register
.globl irq_shared_dispatch
.type irq_shared_dispatch, @function
irq_shared_dispatch:
    jmp *kdebug_import_slots+248
.size irq_shared_dispatch, .-irq_shared_dispatch
.globl irq_test_reset_line
.type irq_test_reset_line, @function
irq_test_reset_line:
    jmp *kdebug_import_slots+252
.size irq_test_reset_line, .-irq_test_reset_line
.globl irq_unregister
.type irq_unregister, @function
irq_unregister:
    jmp *kdebug_import_slots+256
.size irq_unregister, .-irq_unregister
.globl kapi_mem_stat
.type kapi_mem_stat, @function
kapi_mem_stat:
    jmp *kdebug_import_slots+260
.size kapi_mem_stat, .-kapi_mem_stat
.globl kbd_diag
.type kbd_diag, @function
kbd_diag:
    jmp *kdebug_import_slots+264
.size kbd_diag, .-kbd_diag
.globl kbd_inject_selftest
.type kbd_inject_selftest, @function
kbd_inject_selftest:
    jmp *kdebug_import_slots+268
.size kbd_inject_selftest, .-kbd_inject_selftest
.globl kheap_alloc
.type kheap_alloc, @function
kheap_alloc:
    jmp *kdebug_import_slots+272
.size kheap_alloc, .-kheap_alloc
.globl kheap_block_size
.type kheap_block_size, @function
kheap_block_size:
    jmp *kdebug_import_slots+276
.size kheap_block_size, .-kheap_block_size
.globl kheap_free
.type kheap_free, @function
kheap_free:
    jmp *kdebug_import_slots+280
.size kheap_free, .-kheap_free
.globl kheap_init
.type kheap_init, @function
kheap_init:
    jmp *kdebug_import_slots+284
.size kheap_init, .-kheap_init
.globl kmemcpy
.type kmemcpy, @function
kmemcpy:
    jmp *kdebug_import_slots+288
.size kmemcpy, .-kmemcpy
.globl kmemset
.type kmemset, @function
kmemset:
    jmp *kdebug_import_slots+292
.size kmemset, .-kmemset
.globl kprintf_attr_to_pc98
.type kprintf_attr_to_pc98, @function
kprintf_attr_to_pc98:
    jmp *kdebug_import_slots+296
.size kprintf_attr_to_pc98, .-kprintf_attr_to_pc98
.globl kstack_high_water
.type kstack_high_water, @function
kstack_high_water:
    jmp *kdebug_import_slots+300
.size kstack_high_water, .-kstack_high_water
.globl kstrcmp
.type kstrcmp, @function
kstrcmp:
    jmp *kdebug_import_slots+304
.size kstrcmp, .-kstrcmp
.globl kstrcpy
.type kstrcpy, @function
kstrcpy:
    jmp *kdebug_import_slots+308
.size kstrcpy, .-kstrcpy
.globl kstrlen
.type kstrlen, @function
kstrlen:
    jmp *kdebug_import_slots+312
.size kstrlen, .-kstrlen
.globl kstrncat
.type kstrncat, @function
kstrncat:
    jmp *kdebug_import_slots+316
.size kstrncat, .-kstrncat
.globl kstrncmp
.type kstrncmp, @function
kstrncmp:
    jmp *kdebug_import_slots+320
.size kstrncmp, .-kstrncmp
.globl kstrncpy
.type kstrncpy, @function
kstrncpy:
    jmp *kdebug_import_slots+324
.size kstrncpy, .-kstrncpy
.globl kutoa_dec
.type kutoa_dec, @function
kutoa_dec:
    jmp *kdebug_import_slots+328
.size kutoa_dec, .-kutoa_dec
.globl kutoa_hex
.type kutoa_hex, @function
kutoa_hex:
    jmp *kdebug_import_slots+332
.size kutoa_hex, .-kutoa_hex
.globl launch_selftest
.type launch_selftest, @function
launch_selftest:
    jmp *kdebug_import_slots+336
.size launch_selftest, .-launch_selftest
.globl lease_audit_all
.type lease_audit_all, @function
lease_audit_all:
    jmp *kdebug_import_slots+340
.size lease_audit_all, .-lease_audit_all
.globl lease_selftest
.type lease_selftest, @function
lease_selftest:
    jmp *kdebug_import_slots+344
.size lease_selftest, .-lease_selftest
.globl ledger_arena_top
.type ledger_arena_top, @function
ledger_arena_top:
    jmp *kdebug_import_slots+348
.size ledger_arena_top, .-ledger_arena_top
.globl ledger_owner_new
.type ledger_owner_new, @function
ledger_owner_new:
    jmp *kdebug_import_slots+352
.size ledger_owner_new, .-ledger_owner_new
.globl ledger_owner_pages
.type ledger_owner_pages, @function
ledger_owner_pages:
    jmp *kdebug_import_slots+356
.size ledger_owner_pages, .-ledger_owner_pages
.globl ledger_owner_retire
.type ledger_owner_retire, @function
ledger_owner_retire:
    jmp *kdebug_import_slots+360
.size ledger_owner_retire, .-ledger_owner_retire
.globl ledger_reclaim_owner
.type ledger_reclaim_owner, @function
ledger_reclaim_owner:
    jmp *kdebug_import_slots+364
.size ledger_reclaim_owner, .-ledger_reclaim_owner
.globl ledger_resource_set_map
.type ledger_resource_set_map, @function
ledger_resource_set_map:
    jmp *kdebug_import_slots+368
.size ledger_resource_set_map, .-ledger_resource_set_map
.globl ledger_selfcheck
.type ledger_selfcheck, @function
ledger_selfcheck:
    jmp *kdebug_import_slots+372
.size ledger_selfcheck, .-ledger_selfcheck
.globl ledger_surface_find
.type ledger_surface_find, @function
ledger_surface_find:
    jmp *kdebug_import_slots+376
.size ledger_surface_find, .-ledger_surface_find
.globl ledger_surface_regen
.type ledger_surface_regen, @function
ledger_surface_regen:
    jmp *kdebug_import_slots+380
.size ledger_surface_regen, .-ledger_surface_regen
.globl ledger_surface_validate
.type ledger_surface_validate, @function
ledger_surface_validate:
    jmp *kdebug_import_slots+384
.size ledger_surface_validate, .-ledger_surface_validate
.globl memmove
.type memmove, @function
memmove:
    jmp *kdebug_import_slots+388
.size memmove, .-memmove
.globl paging_addrspace_create
.type paging_addrspace_create, @function
paging_addrspace_create:
    jmp *kdebug_import_slots+392
.size paging_addrspace_create, .-paging_addrspace_create
.globl paging_addrspace_create_lease
.type paging_addrspace_create_lease, @function
paging_addrspace_create_lease:
    jmp *kdebug_import_slots+396
.size paging_addrspace_create_lease, .-paging_addrspace_create_lease
.globl paging_addrspace_destroy
.type paging_addrspace_destroy, @function
paging_addrspace_destroy:
    jmp *kdebug_import_slots+400
.size paging_addrspace_destroy, .-paging_addrspace_destroy
.globl paging_addrspace_map_user
.type paging_addrspace_map_user, @function
paging_addrspace_map_user:
    jmp *kdebug_import_slots+404
.size paging_addrspace_map_user, .-paging_addrspace_map_user
.globl paging_app_band_selftest
.type paging_app_band_selftest, @function
paging_app_band_selftest:
    jmp *kdebug_import_slots+408
.size paging_app_band_selftest, .-paging_app_band_selftest
.globl paging_current_cr3
.type paging_current_cr3, @function
paging_current_cr3:
    jmp *kdebug_import_slots+412
.size paging_current_cr3, .-paging_current_cr3
.globl paging_kernel_pd_phys
.type paging_kernel_pd_phys, @function
paging_kernel_pd_phys:
    jmp *kdebug_import_slots+416
.size paging_kernel_pd_phys, .-paging_kernel_pd_phys
.globl paging_map_user_keep_selftest
.type paging_map_user_keep_selftest, @function
paging_map_user_keep_selftest:
    jmp *kdebug_import_slots+420
.size paging_map_user_keep_selftest, .-paging_map_user_keep_selftest
.globl paging_master_audit
.type paging_master_audit, @function
paging_master_audit:
    jmp *kdebug_import_slots+424
.size paging_master_audit, .-paging_master_audit
.globl paging_memmap_selftest
.type paging_memmap_selftest, @function
paging_memmap_selftest:
    jmp *kdebug_import_slots+428
.size paging_memmap_selftest, .-paging_memmap_selftest
.globl paging_pd_clone_selftest
.type paging_pd_clone_selftest, @function
paging_pd_clone_selftest:
    jmp *kdebug_import_slots+432
.size paging_pd_clone_selftest, .-paging_pd_clone_selftest
.globl paging_poke_user_bit
.type paging_poke_user_bit, @function
paging_poke_user_bit:
    jmp *kdebug_import_slots+436
.size paging_poke_user_bit, .-paging_poke_user_bit
.globl paging_registered_pt
.type paging_registered_pt, @function
paging_registered_pt:
    jmp *kdebug_import_slots+440
.size paging_registered_pt, .-paging_registered_pt
.globl paging_v86_session_open
.type paging_v86_session_open, @function
paging_v86_session_open:
    jmp *kdebug_import_slots+444
.size paging_v86_session_open, .-paging_v86_session_open
.globl pcm_open
.type pcm_open, @function
pcm_open:
    jmp *kdebug_import_slots+448
.size pcm_open, .-pcm_open
.globl pcm_state
.type pcm_state, @function
pcm_state:
    jmp *kdebug_import_slots+452
.size pcm_state, .-pcm_state
.globl pgalloc_alloc_phys
.type pgalloc_alloc_phys, @function
pgalloc_alloc_phys:
    jmp *kdebug_import_slots+456
.size pgalloc_alloc_phys, .-pgalloc_alloc_phys
.globl pgalloc_model_state
.type pgalloc_model_state, @function
pgalloc_model_state:
    jmp *kdebug_import_slots+460
.size pgalloc_model_state, .-pgalloc_model_state
.globl pit_get_setup
.type pit_get_setup, @function
pit_get_setup:
    jmp *kdebug_import_slots+464
.size pit_get_setup, .-pit_get_setup
.globl res_owner_get
.type res_owner_get, @function
res_owner_get:
    jmp *kdebug_import_slots+468
.size res_owner_get, .-res_owner_get
.globl res_owner_set
.type res_owner_set, @function
res_owner_set:
    jmp *kdebug_import_slots+472
.size res_owner_set, .-res_owner_set
.globl ring3_user_range_ok
.type ring3_user_range_ok, @function
ring3_user_range_ok:
    jmp *kdebug_import_slots+476
.size ring3_user_range_ok, .-ring3_user_range_ok
.globl ring3_user_range_writable
.type ring3_user_range_writable, @function
ring3_user_range_writable:
    jmp *kdebug_import_slots+480
.size ring3_user_range_writable, .-ring3_user_range_writable
.globl ring3_user_ranges_writable
.type ring3_user_ranges_writable, @function
ring3_user_ranges_writable:
    jmp *kdebug_import_slots+484
.size ring3_user_ranges_writable, .-ring3_user_ranges_writable
.globl ring3_wm_enter
.type ring3_wm_enter, @function
ring3_wm_enter:
    jmp *kdebug_import_slots+488
.size ring3_wm_enter, .-ring3_wm_enter
.globl ring3_wm_leave
.type ring3_wm_leave, @function
ring3_wm_leave:
    jmp *kdebug_import_slots+492
.size ring3_wm_leave, .-ring3_wm_leave
.globl serial_gate_active
.type serial_gate_active, @function
serial_gate_active:
    jmp *kdebug_import_slots+496
.size serial_gate_active, .-serial_gate_active
.globl serial_puts_polled
.type serial_puts_polled, @function
serial_puts_polled:
    jmp *kdebug_import_slots+500
.size serial_puts_polled, .-serial_puts_polled
.globl shell_print
.type shell_print, @function
shell_print:
    jmp *kdebug_import_slots+504
.size shell_print, .-shell_print
.globl strchr
.type strchr, @function
strchr:
    jmp *kdebug_import_slots+508
.size strchr, .-strchr
.globl sys_time_now
.type sys_time_now, @function
sys_time_now:
    jmp *kdebug_import_slots+512
.size sys_time_now, .-sys_time_now
.globl sys_usable_mem_end
.type sys_usable_mem_end, @function
sys_usable_mem_end:
    jmp *kdebug_import_slots+516
.size sys_usable_mem_end, .-sys_usable_mem_end
.globl sysclk_detected
.type sysclk_detected, @function
sysclk_detected:
    jmp *kdebug_import_slots+520
.size sysclk_detected, .-sysclk_detected
.globl sysclk_hz
.type sysclk_hz, @function
sysclk_hz:
    jmp *kdebug_import_slots+524
.size sysclk_hz, .-sysclk_hz
.globl time_branch_reset
.type time_branch_reset, @function
time_branch_reset:
    jmp *kdebug_import_slots+528
.size time_branch_reset, .-time_branch_reset
.globl time_test_feed_clear
.type time_test_feed_clear, @function
time_test_feed_clear:
    jmp *kdebug_import_slots+532
.size time_test_feed_clear, .-time_test_feed_clear
.globl tvram_readchar_at
.type tvram_readchar_at, @function
tvram_readchar_at:
    jmp *kdebug_import_slots+536
.size tvram_readchar_at, .-tvram_readchar_at
.globl kdi_get_kctx_irq_depth
.type kdi_get_kctx_irq_depth, @function
kdi_get_kctx_irq_depth:
    jmp *kdebug_import_slots+540
.size kdi_get_kctx_irq_depth, .-kdi_get_kctx_irq_depth
.globl kdi_get_kctx_exc_depth
.type kdi_get_kctx_exc_depth, @function
kdi_get_kctx_exc_depth:
    jmp *kdebug_import_slots+544
.size kdi_get_kctx_exc_depth, .-kdi_get_kctx_exc_depth
.globl kdi_get_tick_count
.type kdi_get_tick_count, @function
kdi_get_tick_count:
    jmp *kdebug_import_slots+548
.size kdi_get_tick_count, .-kdi_get_tick_count
.globl kdi_get_resident_heap_peak
.type kdi_get_resident_heap_peak, @function
kdi_get_resident_heap_peak:
    jmp *kdebug_import_slots+552
.size kdi_get_resident_heap_peak, .-kdi_get_resident_heap_peak
.globl kdi_get_ledger_bad_free
.type kdi_get_ledger_bad_free, @function
kdi_get_ledger_bad_free:
    jmp *kdebug_import_slots+556
.size kdi_get_ledger_bad_free, .-kdi_get_ledger_bad_free
.globl kdi_get_ledger_check_fail
.type kdi_get_ledger_check_fail, @function
kdi_get_ledger_check_fail:
    jmp *kdebug_import_slots+560
.size kdi_get_ledger_check_fail, .-kdi_get_ledger_check_fail
.globl kdi_get_ledger_exc_ops
.type kdi_get_ledger_exc_ops, @function
kdi_get_ledger_exc_ops:
    jmp *kdebug_import_slots+564
.size kdi_get_ledger_exc_ops, .-kdi_get_ledger_exc_ops
.globl kdi_get_ledger_irq_ops
.type kdi_get_ledger_irq_ops, @function
kdi_get_ledger_irq_ops:
    jmp *kdebug_import_slots+568
.size kdi_get_ledger_irq_ops, .-kdi_get_ledger_irq_ops
.globl kdi_get_ledger_region_count
.type kdi_get_ledger_region_count, @function
kdi_get_ledger_region_count:
    jmp *kdebug_import_slots+572
.size kdi_get_ledger_region_count, .-kdi_get_ledger_region_count
.globl kdi_get_paging_memmap_bad_count
.type kdi_get_paging_memmap_bad_count, @function
kdi_get_paging_memmap_bad_count:
    jmp *kdebug_import_slots+576
.size kdi_get_paging_memmap_bad_count, .-kdi_get_paging_memmap_bad_count
.globl kdi_get_paging_range_reject_count
.type kdi_get_paging_range_reject_count, @function
kdi_get_paging_range_reject_count:
    jmp *kdebug_import_slots+580
.size kdi_get_paging_range_reject_count, .-kdi_get_paging_range_reject_count
.globl kdi_get_ring3_range_reject_addr
.type kdi_get_ring3_range_reject_addr, @function
kdi_get_ring3_range_reject_addr:
    jmp *kdebug_import_slots+584
.size kdi_get_ring3_range_reject_addr, .-kdi_get_ring3_range_reject_addr
.globl kdi_get_ring3_range_reject_count
.type kdi_get_ring3_range_reject_count, @function
kdi_get_ring3_range_reject_count:
    jmp *kdebug_import_slots+588
.size kdi_get_ring3_range_reject_count, .-kdi_get_ring3_range_reject_count
.globl kdi_get_ring3_range_reject_last
.type kdi_get_ring3_range_reject_last, @function
kdi_get_ring3_range_reject_last:
    jmp *kdebug_import_slots+592
.size kdi_get_ring3_range_reject_last, .-kdi_get_ring3_range_reject_last
.globl kdi_get_ring3_wm_depth_underflow
.type kdi_get_ring3_wm_depth_underflow, @function
kdi_get_ring3_wm_depth_underflow:
    jmp *kdebug_import_slots+596
.size kdi_get_ring3_wm_depth_underflow, .-kdi_get_ring3_wm_depth_underflow
.globl kdi_get_cpu_calib_rounds
.type kdi_get_cpu_calib_rounds, @function
kdi_get_cpu_calib_rounds:
    jmp *kdebug_import_slots+600
.size kdi_get_cpu_calib_rounds, .-kdi_get_cpu_calib_rounds
.globl kdi_get_cpu_calib_ticks
.type kdi_get_cpu_calib_ticks, @function
kdi_get_cpu_calib_ticks:
    jmp *kdebug_import_slots+604
.size kdi_get_cpu_calib_ticks, .-kdi_get_cpu_calib_ticks
.globl kdi_get_ktime_clamp_count
.type kdi_get_ktime_clamp_count, @function
kdi_get_ktime_clamp_count:
    jmp *kdebug_import_slots+608
.size kdi_get_ktime_clamp_count, .-kdi_get_ktime_clamp_count
.globl kdi_get_irq_ctx_violations
.type kdi_get_irq_ctx_violations, @function
kdi_get_irq_ctx_violations:
    jmp *kdebug_import_slots+612
.size kdi_get_irq_ctx_violations, .-kdi_get_irq_ctx_violations
.globl kdi_get_irq_storm_masked
.type kdi_get_irq_storm_masked, @function
kdi_get_irq_storm_masked:
    jmp *kdebug_import_slots+616
.size kdi_get_irq_storm_masked, .-kdi_get_irq_storm_masked
.globl kdi_get_irq_line_quarantined
.type kdi_get_irq_line_quarantined, @function
kdi_get_irq_line_quarantined:
    jmp *kdebug_import_slots+620
.size kdi_get_irq_line_quarantined, .-kdi_get_irq_line_quarantined
.globl kdi_get_gui_ime_render_rejected
.type kdi_get_gui_ime_render_rejected, @function
kdi_get_gui_ime_render_rejected:
    jmp *kdebug_import_slots+624
.size kdi_get_gui_ime_render_rejected, .-kdi_get_gui_ime_render_rejected
.globl kdi_get_ring3_in_syscall
.type kdi_get_ring3_in_syscall, @function
kdi_get_ring3_in_syscall:
    jmp *kdebug_import_slots+628
.size kdi_get_ring3_in_syscall, .-kdi_get_ring3_in_syscall
.globl kdi_set_ring3_in_syscall
.type kdi_set_ring3_in_syscall, @function
kdi_set_ring3_in_syscall:
    jmp *kdebug_import_slots+632
.size kdi_set_ring3_in_syscall, .-kdi_set_ring3_in_syscall
.globl kdi_get_ring3_wm_depth
.type kdi_get_ring3_wm_depth, @function
kdi_get_ring3_wm_depth:
    jmp *kdebug_import_slots+636
.size kdi_get_ring3_wm_depth, .-kdi_get_ring3_wm_depth
.globl kdi_set_ring3_wm_depth
.type kdi_set_ring3_wm_depth, @function
kdi_set_ring3_wm_depth:
    jmp *kdebug_import_slots+640
.size kdi_set_ring3_wm_depth, .-kdi_set_ring3_wm_depth
.globl kdi_get_appslot_trim_epoch
.type kdi_get_appslot_trim_epoch, @function
kdi_get_appslot_trim_epoch:
    jmp *kdebug_import_slots+644
.size kdi_get_appslot_trim_epoch, .-kdi_get_appslot_trim_epoch
.globl kdi_set_appslot_trim_epoch
.type kdi_set_appslot_trim_epoch, @function
kdi_set_appslot_trim_epoch:
    jmp *kdebug_import_slots+648
.size kdi_set_appslot_trim_epoch, .-kdi_set_appslot_trim_epoch
.globl kdi_get_appslot_trim_request_count
.type kdi_get_appslot_trim_request_count, @function
kdi_get_appslot_trim_request_count:
    jmp *kdebug_import_slots+652
.size kdi_get_appslot_trim_request_count, .-kdi_get_appslot_trim_request_count
.globl kdi_set_appslot_trim_request_count
.type kdi_set_appslot_trim_request_count, @function
kdi_set_appslot_trim_request_count:
    jmp *kdebug_import_slots+656
.size kdi_set_appslot_trim_request_count, .-kdi_set_appslot_trim_request_count
.globl kdi_get_appslot_trim_mark_count
.type kdi_get_appslot_trim_mark_count, @function
kdi_get_appslot_trim_mark_count:
    jmp *kdebug_import_slots+660
.size kdi_get_appslot_trim_mark_count, .-kdi_get_appslot_trim_mark_count
.globl kdi_set_appslot_trim_mark_count
.type kdi_set_appslot_trim_mark_count, @function
kdi_set_appslot_trim_mark_count:
    jmp *kdebug_import_slots+664
.size kdi_set_appslot_trim_mark_count, .-kdi_set_appslot_trim_mark_count
.globl kdi_get_appslot_trim_done_count
.type kdi_get_appslot_trim_done_count, @function
kdi_get_appslot_trim_done_count:
    jmp *kdebug_import_slots+668
.size kdi_get_appslot_trim_done_count, .-kdi_get_appslot_trim_done_count
.globl kdi_set_appslot_trim_done_count
.type kdi_set_appslot_trim_done_count, @function
kdi_set_appslot_trim_done_count:
    jmp *kdebug_import_slots+672
.size kdi_set_appslot_trim_done_count, .-kdi_set_appslot_trim_done_count
.globl kdi_get_appslot_trim_done_reject_count
.type kdi_get_appslot_trim_done_reject_count, @function
kdi_get_appslot_trim_done_reject_count:
    jmp *kdebug_import_slots+676
.size kdi_get_appslot_trim_done_reject_count, .-kdi_get_appslot_trim_done_reject_count
.globl kdi_set_appslot_trim_done_reject_count
.type kdi_set_appslot_trim_done_reject_count, @function
kdi_set_appslot_trim_done_reject_count:
    jmp *kdebug_import_slots+680
.size kdi_set_appslot_trim_done_reject_count, .-kdi_set_appslot_trim_done_reject_count
.globl kdi_get_appslot_reclaim_count
.type kdi_get_appslot_reclaim_count, @function
kdi_get_appslot_reclaim_count:
    jmp *kdebug_import_slots+684
.size kdi_get_appslot_reclaim_count, .-kdi_get_appslot_reclaim_count
.globl kdi_set_appslot_reclaim_count
.type kdi_set_appslot_reclaim_count, @function
kdi_set_appslot_reclaim_count:
    jmp *kdebug_import_slots+688
.size kdi_set_appslot_reclaim_count, .-kdi_set_appslot_reclaim_count
.globl kdi_get_appslot_last_reclaim_id
.type kdi_get_appslot_last_reclaim_id, @function
kdi_get_appslot_last_reclaim_id:
    jmp *kdebug_import_slots+692
.size kdi_get_appslot_last_reclaim_id, .-kdi_get_appslot_last_reclaim_id
.globl kdi_set_appslot_last_reclaim_id
.type kdi_set_appslot_last_reclaim_id, @function
kdi_set_appslot_last_reclaim_id:
    jmp *kdebug_import_slots+696
.size kdi_set_appslot_last_reclaim_id, .-kdi_set_appslot_last_reclaim_id
.globl kdi_get_irq_unexpected
.type kdi_get_irq_unexpected, @function
kdi_get_irq_unexpected:
    jmp *kdebug_import_slots+700
.size kdi_get_irq_unexpected, .-kdi_get_irq_unexpected
.globl kdi_set_irq_unexpected
.type kdi_set_irq_unexpected, @function
kdi_set_irq_unexpected:
    jmp *kdebug_import_slots+704
.size kdi_set_irq_unexpected, .-kdi_set_irq_unexpected
.globl kdi_get_irq_test_quiet
.type kdi_get_irq_test_quiet, @function
kdi_get_irq_test_quiet:
    jmp *kdebug_import_slots+708
.size kdi_get_irq_test_quiet, .-kdi_get_irq_test_quiet
.globl kdi_set_irq_test_quiet
.type kdi_set_irq_test_quiet, @function
kdi_set_irq_test_quiet:
    jmp *kdebug_import_slots+712
.size kdi_set_irq_test_quiet, .-kdi_set_irq_test_quiet
.globl kdi_get_time_test_feed_n
.type kdi_get_time_test_feed_n, @function
kdi_get_time_test_feed_n:
    jmp *kdebug_import_slots+716
.size kdi_get_time_test_feed_n, .-kdi_get_time_test_feed_n
.globl kdi_set_time_test_feed_n
.type kdi_set_time_test_feed_n, @function
kdi_set_time_test_feed_n:
    jmp *kdebug_import_slots+720
.size kdi_set_time_test_feed_n, .-kdi_set_time_test_feed_n
.globl kdi_ptr_ledger_surfaces
.type kdi_ptr_ledger_surfaces, @function
kdi_ptr_ledger_surfaces:
    jmp *kdebug_import_slots+724
.size kdi_ptr_ledger_surfaces, .-kdi_ptr_ledger_surfaces
.globl kdi_ptr_ledger_regions
.type kdi_ptr_ledger_regions, @function
kdi_ptr_ledger_regions:
    jmp *kdebug_import_slots+728
.size kdi_ptr_ledger_regions, .-kdi_ptr_ledger_regions
.globl kdi_ptr_ledger_resources
.type kdi_ptr_ledger_resources, @function
kdi_ptr_ledger_resources:
    jmp *kdebug_import_slots+732
.size kdi_ptr_ledger_resources, .-kdi_ptr_ledger_resources
.globl kdi_ptr_irq_lines
.type kdi_ptr_irq_lines, @function
kdi_ptr_irq_lines:
    jmp *kdebug_import_slots+736
.size kdi_ptr_irq_lines, .-kdi_ptr_irq_lines
.globl kdi_ptr_paging_memmap_bad
.type kdi_ptr_paging_memmap_bad, @function
kdi_ptr_paging_memmap_bad:
    jmp *kdebug_import_slots+740
.size kdi_ptr_paging_memmap_bad, .-kdi_ptr_paging_memmap_bad
.globl kdi_ptr_time_branch_hits
.type kdi_ptr_time_branch_hits, @function
kdi_ptr_time_branch_hits:
    jmp *kdebug_import_slots+744
.size kdi_ptr_time_branch_hits, .-kdi_ptr_time_branch_hits
.globl kdi_ptr_time_test_feed
.type kdi_ptr_time_test_feed, @function
kdi_ptr_time_test_feed:
    jmp *kdebug_import_slots+748
.size kdi_ptr_time_test_feed, .-kdi_ptr_time_test_feed
.globl kdi_kselftest_count
.type kdi_kselftest_count, @function
kdi_kselftest_count:
    jmp *kdebug_import_slots+752
.size kdi_kselftest_count, .-kdi_kselftest_count
.globl kdi_kselftest_reset
.type kdi_kselftest_reset, @function
kdi_kselftest_reset:
    jmp *kdebug_import_slots+756
.size kdi_kselftest_reset, .-kdi_kselftest_reset
.globl kdi_get_kselftest_pass
.type kdi_get_kselftest_pass, @function
kdi_get_kselftest_pass:
    jmp *kdebug_import_slots+760
.size kdi_get_kselftest_pass, .-kdi_get_kselftest_pass
.globl kdi_get_kselftest_fail
.type kdi_get_kselftest_fail, @function
kdi_get_kselftest_fail:
    jmp *kdebug_import_slots+764
.size kdi_get_kselftest_fail, .-kdi_get_kselftest_fail
.globl kdi_kaudit_last_rc
.type kdi_kaudit_last_rc, @function
kdi_kaudit_last_rc:
    jmp *kdebug_import_slots+768
.size kdi_kaudit_last_rc, .-kdi_kaudit_last_rc
.globl kdi_kaudit_skipped
.type kdi_kaudit_skipped, @function
kdi_kaudit_skipped:
    jmp *kdebug_import_slots+772
.size kdi_kaudit_skipped, .-kdi_kaudit_skipped
.globl console_hw_cursor_enable
.type console_hw_cursor_enable, @function
console_hw_cursor_enable:
    jmp *kdebug_import_slots+776
.size console_hw_cursor_enable, .-console_hw_cursor_enable
.globl exec_ledger_owner
.type exec_ledger_owner, @function
exec_ledger_owner:
    jmp *kdebug_import_slots+780
.size exec_ledger_owner, .-exec_ledger_owner
.globl gfx_get_height
.type gfx_get_height, @function
gfx_get_height:
    jmp *kdebug_import_slots+784
.size gfx_get_height, .-gfx_get_height
.globl kfree
.type kfree, @function
kfree:
    jmp *kdebug_import_slots+788
.size kfree, .-kfree
.globl kmalloc
.type kmalloc, @function
kmalloc:
    jmp *kdebug_import_slots+792
.size kmalloc, .-kmalloc
.globl kzalloc
.type kzalloc, @function
kzalloc:
    jmp *kdebug_import_slots+796
.size kzalloc, .-kzalloc
.globl pegc_restore_text_sync
.type pegc_restore_text_sync, @function
pegc_restore_text_sync:
    jmp *kdebug_import_slots+800
.size pegc_restore_text_sync, .-pegc_restore_text_sync
.globl v86_cui_display_restore
.type v86_cui_display_restore, @function
v86_cui_display_restore:
    jmp *kdebug_import_slots+804
.size v86_cui_display_restore, .-v86_cui_display_restore
.globl v86_gui_refuse
.type v86_gui_refuse, @function
v86_gui_refuse:
    jmp *kdebug_import_slots+808
.size v86_gui_refuse, .-v86_gui_refuse
.globl v86_is_active
.type v86_is_active, @function
v86_is_active:
    jmp *kdebug_import_slots+812
.size v86_is_active, .-v86_is_active
.globl v86_mem_setup
.type v86_mem_setup, @function
v86_mem_setup:
    jmp *kdebug_import_slots+816
.size v86_mem_setup, .-v86_mem_setup
.globl v86_mem_teardown
.type v86_mem_teardown, @function
v86_mem_teardown:
    jmp *kdebug_import_slots+820
.size v86_mem_teardown, .-v86_mem_teardown
.globl v86_pic_set_imr
.type v86_pic_set_imr, @function
v86_pic_set_imr:
    jmp *kdebug_import_slots+824
.size v86_pic_set_imr, .-v86_pic_set_imr
.globl v86_run_limit
.type v86_run_limit, @function
v86_run_limit:
    jmp *kdebug_import_slots+828
.size v86_run_limit, .-v86_run_limit
.globl kdi_get_v86_gp_cs
.type kdi_get_v86_gp_cs, @function
kdi_get_v86_gp_cs:
    jmp *kdebug_import_slots+832
.size kdi_get_v86_gp_cs, .-kdi_get_v86_gp_cs
.globl kdi_get_v86_gp_ip
.type kdi_get_v86_gp_ip, @function
kdi_get_v86_gp_ip:
    jmp *kdebug_import_slots+836
.size kdi_get_v86_gp_ip, .-kdi_get_v86_gp_ip
.globl kdi_ptr_v86_session
.type kdi_ptr_v86_session, @function
kdi_ptr_v86_session:
    jmp *kdebug_import_slots+840
.size kdi_ptr_v86_session, .-kdi_ptr_v86_session
.globl kdi_ptr_v86_gcap_phase
.type kdi_ptr_v86_gcap_phase, @function
kdi_ptr_v86_gcap_phase:
    jmp *kdebug_import_slots+844
.size kdi_ptr_v86_gcap_phase, .-kdi_ptr_v86_gcap_phase
.globl kdi_ptr_v86_gcap_alloc_count
.type kdi_ptr_v86_gcap_alloc_count, @function
kdi_ptr_v86_gcap_alloc_count:
    jmp *kdebug_import_slots+848
.size kdi_ptr_v86_gcap_alloc_count, .-kdi_ptr_v86_gcap_alloc_count
.globl kdi_ptr_v86_gcap_free_count
.type kdi_ptr_v86_gcap_free_count, @function
kdi_ptr_v86_gcap_free_count:
    jmp *kdebug_import_slots+852
.size kdi_ptr_v86_gcap_free_count, .-kdi_ptr_v86_gcap_free_count
.globl vfs_read
.type vfs_read, @function
vfs_read:
    jmp *kdebug_import_slots+856
.size vfs_read, .-vfs_read
.globl vfs_write
.type vfs_write, @function
vfs_write:
    jmp *kdebug_import_slots+860
.size vfs_write, .-vfs_write
.globl vfs_sync
.type vfs_sync, @function
vfs_sync:
    jmp *kdebug_import_slots+864
.size vfs_sync, .-vfs_sync
.globl boot_image_info
.type boot_image_info, @function
boot_image_info:
    jmp *kdebug_import_slots+868
.size boot_image_info, .-boot_image_info
.globl pgalloc_alloc_n_owner
.type pgalloc_alloc_n_owner, @function
pgalloc_alloc_n_owner:
    jmp *kdebug_import_slots+872
.size pgalloc_alloc_n_owner, .-pgalloc_alloc_n_owner
.globl pgalloc_free_n_owner
.type pgalloc_free_n_owner, @function
pgalloc_free_n_owner:
    jmp *kdebug_import_slots+876
.size pgalloc_free_n_owner, .-pgalloc_free_n_owner
.globl pgalloc_free_pages
.type pgalloc_free_pages, @function
pgalloc_free_pages:
    jmp *kdebug_import_slots+880
.size pgalloc_free_pages, .-pgalloc_free_pages
.globl pgalloc_page_owned
.type pgalloc_page_owned, @function
pgalloc_page_owned:
    jmp *kdebug_import_slots+884
.size pgalloc_page_owned, .-pgalloc_page_owned
.globl memory_boot_slack_first
.type memory_boot_slack_first, @function
memory_boot_slack_first:
    jmp *kdebug_import_slots+888
.size memory_boot_slack_first, .-memory_boot_slack_first
.globl unicode_file_debug_phys
.type unicode_file_debug_phys, @function
unicode_file_debug_phys:
    jmp *kdebug_import_slots+892
.size unicode_file_debug_phys, .-unicode_file_debug_phys
.globl unicode_file_lookup
.type unicode_file_lookup, @function
unicode_file_lookup:
    jmp *kdebug_import_slots+896
.size unicode_file_lookup, .-unicode_file_lookup
.globl pgalloc_limit_pfn
.type pgalloc_limit_pfn, @function
pgalloc_limit_pfn:
    jmp *kdebug_import_slots+900
.size pgalloc_limit_pfn, .-pgalloc_limit_pfn
.globl vfs_fstype
.type vfs_fstype, @function
vfs_fstype:
    jmp *kdebug_import_slots+904
.size vfs_fstype, .-vfs_fstype
.globl vfs_stat
.type vfs_stat, @function
vfs_stat:
    jmp *kdebug_import_slots+908
.size vfs_stat, .-vfs_stat
.globl vfs_open
.type vfs_open, @function
vfs_open:
    jmp *kdebug_import_slots+912
.size vfs_open, .-vfs_open
.globl vfs_close
.type vfs_close, @function
vfs_close:
    jmp *kdebug_import_slots+916
.size vfs_close, .-vfs_close
.globl vfs_read_fd
.type vfs_read_fd, @function
vfs_read_fd:
    jmp *kdebug_import_slots+920
.size vfs_read_fd, .-vfs_read_fd
.globl vfs_write_fd
.type vfs_write_fd, @function
vfs_write_fd:
    jmp *kdebug_import_slots+924
.size vfs_write_fd, .-vfs_write_fd
.globl vfs_get_size
.type vfs_get_size, @function
vfs_get_size:
    jmp *kdebug_import_slots+928
.size vfs_get_size, .-vfs_get_size
.globl vfs_rm
.type vfs_rm, @function
vfs_rm:
    jmp *kdebug_import_slots+932
.size vfs_rm, .-vfs_rm
.globl dma_range_ok
.type dma_range_ok, @function
dma_range_ok:
    jmp *kdebug_import_slots+936
.size dma_range_ok, .-dma_range_ok
.globl kcg_rom_read_ank
.type kcg_rom_read_ank, @function
kcg_rom_read_ank:
    jmp *kdebug_import_slots+940
.size kcg_rom_read_ank, .-kcg_rom_read_ank
.globl kcg_cache_stats
.type kcg_cache_stats, @function
kcg_cache_stats:
    jmp *kdebug_import_slots+944
.size kcg_cache_stats, .-kcg_cache_stats
.globl v86_io_reset_policy
.type v86_io_reset_policy, @function
v86_io_reset_policy:
    jmp *kdebug_import_slots+948
.size v86_io_reset_policy, .-v86_io_reset_policy
.globl paging_pte_flags
.type paging_pte_flags, @function
paging_pte_flags:
    jmp *kdebug_import_slots+952
.size paging_pte_flags, .-paging_pte_flags
.globl paging_is_present
.type paging_is_present, @function
paging_is_present:
    jmp *kdebug_import_slots+956
.size paging_is_present, .-paging_is_present
.globl paging_map_phys
.type paging_map_phys, @function
paging_map_phys:
    jmp *kdebug_import_slots+960
.size paging_map_phys, .-paging_map_phys
.globl paging_pte_raw
.type paging_pte_raw, @function
paging_pte_raw:
    jmp *kdebug_import_slots+964
.size paging_pte_raw, .-paging_pte_raw
.globl paging_pte_restore
.type paging_pte_restore, @function
paging_pte_restore:
    jmp *kdebug_import_slots+968
.size paging_pte_restore, .-paging_pte_restore
.globl pci_find
.type pci_find, @function
pci_find:
    jmp *kdebug_import_slots+972
.size pci_find, .-pci_find
.globl pci_get
.type pci_get, @function
pci_get:
    jmp *kdebug_import_slots+976
.size pci_get, .-pci_get
.globl e82557_send
.type e82557_send, @function
e82557_send:
    jmp *kdebug_import_slots+980
.size e82557_send, .-e82557_send
.globl e82557_get_mac
.type e82557_get_mac, @function
e82557_get_mac:
    jmp *kdebug_import_slots+984
.size e82557_get_mac, .-e82557_get_mac
.globl e82557_state
.type e82557_state, @function
e82557_state:
    jmp *kdebug_import_slots+988
.size e82557_state, .-e82557_state
.globl sfs_lan_peer_mac
.type sfs_lan_peer_mac, @function
sfs_lan_peer_mac:
    jmp *kdebug_import_slots+992
.size sfs_lan_peer_mac, .-sfs_lan_peer_mac
.globl display_text_start
.type display_text_start, @function
display_text_start:
    jmp *kdebug_import_slots+996
.size display_text_start, .-display_text_start
.globl serial_get_setup
.type serial_get_setup, @function
serial_get_setup:
    jmp *kdebug_import_slots+1000
.size serial_get_setup, .-serial_get_setup
.globl serial_trygetchar
.type serial_trygetchar, @function
serial_trygetchar:
    jmp *kdebug_import_slots+1004
.size serial_trygetchar, .-serial_trygetchar
.globl serial_cmd_port
.type serial_cmd_port, @function
serial_cmd_port:
    jmp *kdebug_import_slots+1008
.size serial_cmd_port, .-serial_cmd_port
.globl serial_data_port
.type serial_data_port, @function
serial_data_port:
    jmp *kdebug_import_slots+1012
.size serial_data_port, .-serial_data_port
.globl serial_txrdy_mask
.type serial_txrdy_mask, @function
serial_txrdy_mask:
    jmp *kdebug_import_slots+1016
.size serial_txrdy_mask, .-serial_txrdy_mask
.globl display_restore_98
.type display_restore_98, @function
display_restore_98:
    jmp *kdebug_import_slots+1020
.size display_restore_98, .-display_restore_98
.globl display_recover_ready
.type display_recover_ready, @function
display_recover_ready:
    jmp *kdebug_import_slots+1024
.size display_recover_ready, .-display_recover_ready
.globl display_recover_cell_read
.type display_recover_cell_read, @function
display_recover_cell_read:
    jmp *kdebug_import_slots+1028
.size display_recover_cell_read, .-display_recover_cell_read
.globl display_recover_cell_write
.type display_recover_cell_write, @function
display_recover_cell_write:
    jmp *kdebug_import_slots+1032
.size display_recover_cell_write, .-display_recover_cell_write
.globl serial_checked_begin
.type serial_checked_begin, @function
serial_checked_begin:
    jmp *kdebug_import_slots+1036
.size serial_checked_begin, .-serial_checked_begin
.globl serial_checked_ready
.type serial_checked_ready, @function
serial_checked_ready:
    jmp *kdebug_import_slots+1040
.size serial_checked_ready, .-serial_checked_ready
.globl serial_checked_put
.type serial_checked_put, @function
serial_checked_put:
    jmp *kdebug_import_slots+1044
.size serial_checked_put, .-serial_checked_put
.globl serial_checked_empty
.type serial_checked_empty, @function
serial_checked_empty:
    jmp *kdebug_import_slots+1048
.size serial_checked_empty, .-serial_checked_empty
.globl serial_checked_end
.type serial_checked_end, @function
serial_checked_end:
    jmp *kdebug_import_slots+1052
.size serial_checked_end, .-serial_checked_end
.globl pci_cfg_read32
.type pci_cfg_read32, @function
pci_cfg_read32:
    jmp *kdebug_import_slots+1056
.size pci_cfg_read32, .-pci_cfg_read32
.globl pci_cfg_read16
.type pci_cfg_read16, @function
pci_cfg_read16:
    jmp *kdebug_import_slots+1060
.size pci_cfg_read16, .-pci_cfg_read16
.globl pci_cfg_write32
.type pci_cfg_write32, @function
pci_cfg_write32:
    jmp *kdebug_import_slots+1064
.size pci_cfg_write32, .-pci_cfg_write32
.globl pci_cfg_write16
.type pci_cfg_write16, @function
pci_cfg_write16:
    jmp *kdebug_import_slots+1068
.size pci_cfg_write16, .-pci_cfg_write16
.globl hwdev_module_ops
.type hwdev_module_ops, @function
hwdev_module_ops:
    jmp *kdebug_import_slots+1072
.size hwdev_module_ops, .-hwdev_module_ops
.section .note.GNU-stack,"",@progbits
