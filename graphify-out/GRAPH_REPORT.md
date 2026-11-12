# Graph Report - os32-v3  (2026-11-12)

## Corpus Check
- Large corpus: 4887 files · ~5,592,047 words. Semantic extraction will be expensive (many Claude tokens). Consider running on a subfolder.

## Summary
- 54236 nodes · 157366 edges · 1285 communities (937 shown, 348 thin omitted)
- Extraction: 89% EXTRACTED · 11% INFERRED · 0% AMBIGUOUS · INFERRED: 16724 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Host Test Harness (host32)
- SQLite Core API
- SQLite Internals A
- Module Image Tools (mkmod)
- Video Drivers & x86 Kernel
- Kernel Host Tests (Python)
- SQLite Internals B
- KAPI Generated Dispatch
- SQLite Internals C
- SQLite Internals D
- SQLite & IME Dictionary Store
- SQLite Internals E
- C Dialect & AST Checks
- Shared Library Tools (mkshlib)
- LAN & FM Sound Drivers
- SDK Separation Tests
- Exec App Slots & Redirection
- Font Library A
- Host Agent & PPC LAN Host
- GeForce Ops & Recovery
- PC-98 Splash Probe
- PowerPC Kernel Boot
- ext2 Open Host Test
- Serial & kdebug Init
- Font Library B
- Package & Manifest Tools
- GShell Host Layer A
- V86 Graphics Capture
- Rust os32api & GShell
- GShell Host Layer B
- Rust os32api SDK
- Game App Glue & Board View
- SQLite Internals F
- Shell Truncation Host Test
- Rust libos32gui A
- Shell Status Host Test
- Memory Map Generator
- Test Fixtures
- Rust libos32gui B
- Build Compare & Acceptance Tools
- R1 Recovery Host Tests
- GShell Source & GUI
- GeForce Devinit & IRQ
- Legacy V86 Callers
- Rust Filer & About Apps
- Rust GUI Stub & Demo
- cff/cff2
- KAPI DB Host Tests
- SHM & DMA Host Tests
- Rust Filer
- cff/cff1
- PC-98 Legacy Video & gfx v2 Lock
- src/lib
- IME Dictionary
- tests/test_kstring_c
- Console & IME
- crt/help
- Config Library (cfg)
- GeForce Display Service
- host/kbdnav_tests
- src/wm
- src/settings
- tests/test_result_conv_host
- tests/test_font_raster
- sqlite3/os32_sqlite_vfs
- tests/test_geforce
- tests/gf5_fake
- src/modal
- tests/vfs_fd_path_host
- tests/gf5_foreground_host
- legacy/v86
- ramfs/ramfs
- ekakiuta/ekakiuta
- tests/test_vfs_fd_path
- tables/gpos
- legacy/v86_pic
- ggg/mod
- tests/pegc_mode_host
- tests/net_link_host
- src/gserial
- tests/gf_accel_c1_host
- mm/pgalloc
- fatfs/ff
- tests/gf_accel_host
- tables/gvar
- kdebug/recover
- inv/inv_core
- src/host
- src/draw
- tests/kapi_audit_host
- tests/osri_view_host
- src/font
- checks/run
- tests/test_deploy_protect
- src/lib #2
- src/lib #3
- tests/vfs_fd_sqlite_host
- tests/hostsvc_netcd_host
- tests/gfx_attach_host
- tests/gui_text_host
- src/taskbar
- tests/splash_obs_host
- tables/colr
- tests/fdc_track_host
- ui/microui
- tests/font_source_host
- tools/img2vbz
- host/movecopy_tests
- tools/nhd_deploy
- asset/asset_core
- ext2/ext2_super
- tables/math
- md/md_render
- tools/check_arm_compile
- tests/gf5_therm_host
- tests/install_fresh_host
- tests/gf4_display_fake
- tests/test_h3_park_resume
- tests/sqlite_module_host
- tests/ext2_write_io_host
- tests/bootlog_host
- ecs/ecs_core
- tests/cdinst_host
- tests/test_deploy_protect #2
- tests/sh_shell_host
- src/cache
- tables/math #2
- geforce/backend_geforce
- tools/rshell_recover
- shell/main
- display/modeset
- tables/name
- tests/gfx_accel_hal_host
- src/font #2
- cfg/libos32cfg
- tests/cirrus_win_host
- tests/econ_test
- tests/exec_heap_host
- src/kbdnav
- src/input
- tests/hwb_host
- tests/gfx_transfer_host
- chem/chem_core
- tests/text_test
- tests/gf4_c2svc_host
- emu_agent/agent
- gf/push
- src/startmenu
- tests/test_hdd_stage1
- tests/save_test
- tests/irq_math_host
- tests/fatfs_read_cache_host
- tests/test_mk_settings_db
- src/fep
- src/multiapp
- tests/btl_test
- gf/command
- src/engine
- tests/dep_dir_b_io_host
- tests/hostsvc_nic_host
- tests/hsync_h2_host
- tests/test_np21w_ctl
- tilemap/tilemap_compose
- tests/x87_lifecycle_host
- tests/math_test
- tests/gfx2_i1_host
- tests/exec_image_host
- tests/ver_collect_host
- tests/ext2_empty_name_host
- map/map_core
- np21w_mcp/server
- tests/kapi_ranges_host
- econ/econ_core
- sqlite3/sqlite3
- tests/raster
- tests/install_recover_host
- tests/gfx_kernel_fb_host
- cmds/tar
- tests/kbd_dlog_host
- tests/gfx2_backend_reg_host
- ata/ide
- input/kbd
- tests/cd_read_host
- tests/test_hostdrv_manifest
- src/limits
- tests/board_test
- tools/h3_park_resume
- src/parser
- tests/ext2_read_bound_host
- src/prompt
- kdebug/recover #2
- tests/gf4_a3_host
- bench_scale2x/main
- src/reader
- tables/gsub
- v2/surface
- display/hw_load
- tests/ext2_write_batch_host
- tests/caller_access_host
- tests/hostsvc_lgy98_attach_host
- tests/gf_runner_host
- tests/e82557_drv_host
- tests/test_np21w_ini_live
- tests/boot_splash_native_host
- tests/pcm_cs4231_host
- tests/gf_command_host
- tools/osri_enc2_core
- tests/serialfs_session_host
- tools/np21w_ini
- v2/v1_shim
- tests/hwb_shell_host
- generated/kdebug_kexports
- tests/app_band_pde_host
- tests/test_deploy_protect #3
- tests/db_caller_host
- tools/gen_tests_inventory
- tests/x11_shared_host
- tests/hsync_h3_host
- tests/serialfs_host
- tests/test_rshell_recover
- src/outline
- tables/glyf
- src/launch
- tests/appmem_map_host
- tests/pci_decode_host
- tests/fstat_redir_host
- tests/gf_accel_unbound_stubs
- fm/snd_engine
- tests/gf_accel_b8_host
- tests/multiapp_model_host
- tests/test_fetch_fonts
- tests/fatfs_stat_host
- tests/gf5_tile_host
- tests/h4_manifest_host
- tests/fd_redirect_d0a_host
- tools/lan_bridge
- tests/evt_test
- src/lib #4
- src/sink
- system/hsync
- src/guest
- tests/multiapp_impl_host
- shell/cmd_file
- mgxview/mgxview
- tests/rpg_test
- np21w_mcp/gui
- tools/np21w_ctl
- src/text
- shell/rshell
- serialfs/serialfs
- trident/regs
- serialfs/sfs_proto
- kdebug/gf_accel_test
- tools/np21w_ctl #2
- tests/ext2_read_cache_host
- tests/hsync_h1_host
- tests/trim_back_rs_host
- cmds/cfg
- src/lib #5
- tests/test_np21w_trial
- tests/gf1_irq_host
- src/bundle
- tests/engine
- sqlite3/sqlite3 #2
- tests/test_kapi_layout
- powermac/ofw
- tests/v86_low_host
- tests/outline
- tests/con_sink_host
- src/fix16
- display/modes
- tests/splash_wire_host
- emu_agent/playbook
- tests/ext2_part_host
- tests/ai_test
- tests/trident_lifecycle_host
- fs/vfs
- tests/gui_reattach_host
- tests/gf_production_host
- sdk/gen_kapi
- tools/guest_tests
- tests/test_host_agent
- tests/disk_auth_host
- tests/hostsvc_boot_host
- tests/fep_user_store_host
- src/app
- tests/gf4_c2linkb_host
- display/tv
- tests/test_np21w_ini_live #2
- tests/gf_command_c0_host
- zlib/zlib
- e82557/e82557
- trident/backend_trident
- display/cursor
- text/lconsole
- tests/ring3_guard_host
- tools/gui_gate
- tests/test_np21w_ini_live #3
- src/guest #2
- tests/gf4_c2save_host
- tests/console_dual_host
- tests/test_h3_park_resume #2
- tools/img2mgx
- tests/gf5_perf_host
- tests/hostsvc_media_host
- src/lib #6
- tests/gfx2_i3_guest_host
- tests/test_rshell_peek
- src/aat
- tests/h4_backing_host
- tests/gui_trim_host
- tests/shlib_high_host
- tests/test_host_agent #2
- tests/test_host_agent #3
- tables/kern
- tests/gf_wc_host
- src/host #2
- tools/rshell_peek
- tests/surface_bundle_host
- tests/test_db_errstr
- tests/gf_display_host
- tests/hdd_stage1_host
- tools/uimock
- src/gstate
- tests/fep_boundary_host
- tests/gf_command_t2b_host
- tests/recover_host
- tests/test_lanfs
- tests/test_np21w_ctl #2
- tests/fs_kind_fake
- tests/kpeek_host
- ppc/kboot_cpu
- ppc/mmu_fake
- tests/e82557_eeprom_host
- kdebug/gf_display_test
- kdebug/recover_profile
- tests/nano_adapter_host
- tools/check_kprintf_window
- tests/test_np21w_gui
- host/doc_tests
- system/cdinst
- src/lib #7
- cs4231/pcm_cs4231
- geforce/gf_v2_weak
- tests/shell_service_host
- tests/test_emu_playbook
- tests/backend_geforce_host
- tests/gf_v2_ops_host
- shell/cmd_sys
- tests/test_host_agent #4
- tests/dep_dir_d3_boot_host
- tests/kcallback_host
- src/cfgro
- src/fake
- tests/gfinit_bus_host
- v2/a0_weak
- v2/a0_weak #2
- edit/macro_engine
- tests/ext2_mini_host
- isofs/iso9660
- crt/syscalls
- gf/therm
- tests/exec_selftest_host
- tests/font_guard_host
- ime/fep_module
- tests/test_deploy_protect #4
- ppc/lan_p24_fake
- tests/host_lib_host
- tests/test_ci_stab
- host/launch_tests
- zlib/inflate
- tests/loop_dev_host
- powermac/lan_service
- tests/splash_v2_host
- econ/econ_estate
- fatfs/diskio
- shell/hwb_exec
- ppc/pmap
- tests/model
- geforce/gf_v2_gart
- hostdrv/hostdrvfs
- tests/reader
- gf/feature5_links
- tests/probe_host
- tests/trim_focus_host
- tools/doc_graph
- tools/mkfat12
- tests/test_deploy_protect #5
- tests/lanfs_host
- shell/cmd_filer
- tables/kerx
- ppc/kboot_cpu_fixture
- tests/exec_budget_host
- tests/gf5_state_host
- src/host_fake
- tests/test_gui_gate
- tests/sqlite_memsys5_proto
- tests/test_lan_bridge
- system/install
- tests/allocator_bench_host
- tests/test_rshell_serial
- base/dev
- tests/launch_host
- tests/x11_shared_host #2
- gui/types
- tools/rshell_serial
- tests/trident_resources_host
- input/input_core
- system/inst_hdd
- ata/atapi
- run-os32/driver
- tests/hostsvc_block_host
- tests/gf4_b4_host
- pc98/pegc_v2
- tests/trident_mode_host
- tests/widget_scroll_host
- cfg/cfg_import
- kdebug/gfinit_plan
- tests/dma_pool_host
- tests/gf4_c3_host
- tests/gf_accel_host #2
- tests/gf_command_t2a1_host
- tests/test_mk_settings_db #2
- tests/cfg_bench
- dma/dma8237
- ne2000/ne2000
- tests/pci_bind_host
- cs4231/pcm_cs4231_math
- tests/trident_palette_host
- tests/trident_module_host
- tools/uimock_field
- workflows/full-check
- tests/gfinit_romfile_host
- kdebug/gfinit_bus
- pc98/bootinfo_check
- tests/module_runtime_host
- tests/gf5_observe_host
- tests/gf_accel_wire_host
- tests/sep5_console_host
- tests/dep_dir_d_console_grid_host
- tests/hwdev_module_host
- e82557/e82557_desc
- tests/gfx_kernel_fb_host #2
- src/state
- allocator/nano_adapter
- src/cfg
- tools/serialfs_host
- dep_dir_c/manifest
- tests/kernel_shell_host
- tables/sbix
- tests/bundle
- ppc/lan_p14_fake
- tests/pci_scan_host
- tests/trim_stop_host
- tests/hdd_stage2_host
- turn/turn_query
- tests/sep5_gfx_init_host
- tests/test_osri_enc2
- tests/kstr_bench
- kdebug/recover #3
- tests/kdebug_loader_host
- tools/osri_enc2_native
- tests/gf_v2_gart_host
- tests/hwb_exec_host
- shell/hwb_parse
- tests/test_serialfs
- tests/vfs_excl_host
- tests/vfs_mount_dev_host
- src/fontbench
- tests/test_guest_acceptance
- tests/test_np21w_transport
- tests/hwb_scripts_host
- tests/test_ledger
- src/window
- geom/gfx_fill
- tests/cd_read_host #2
- exec/appmem
- workflows/check
- gf/command #2
- tests/gf4_d0_host
- display/scan
- tests/hwb_access_host
- ppc/lan_p3_fake
- tests/cat_linenum_host
- tests/fep_standard_host
- tests/geforce_module_host
- tests/hostsvc_fd_host
- tests/ne2000_dma_host
- tests/test_np21w_ini
- tests/test_serialfs #2
- tests/trident_save_host
- tests/cache
- tests/dep_dir_b_timer_host
- tests/kcg_boot_host
- tests/b8_hostdrv_host
- app/view_tiles
- tests/gfx_boot_host
- gf/runner
- tests/cd_read_host #3
- powermac/log
- tests/gfx2_surface_host
- tests/test_fdc_track
- trident/vram
- src/hostsvc
- gf/fifo
- tests/trim_flow_host
- tests/kbd_inject_host
- tools/move_docs
- tests/debug_probe_host
- tests/test_deploy_set
- tests/gfx2_alpha_host
- tests/gfx2_fence_host
- tests/gfx2_submit_host
- tests/sh_launch_host
- tests/vfs_kind_host
- src/doc
- tables/colr #2
- raster/raster
- display/ddc
- src/stream
- kdebug/trident_command_test
- tests/gf_accel_host #3
- tests/gfx2_lock_host
- tests/gfx2_v1_bridge_host
- tests/test_vmkernel_lz4
- tests/trident_emergency_host
- tests/trident_route_host
- src/lib #8
- src/lib #9
- kdebug/kdebug_window
- tests/trident_device_fake
- trident/video
- src/vec2
- tools/uimock_map
- gf/volt
- host/textcore_tests
- tests/test_np21w_gui_mcp
- tests/stat_cmd_host
- tests/test_deploy_protect #6
- tests/test_doc_delink_archive
- tests/test_guest_tests
- tests/sqlite_engine_host
- tests/access_walk_host
- shell/cmd_peek
- block/fdc
- geforce/gf_v2_display
- tests/fep_store_info_host
- tools/uimock_map2
- tests/gf4_b10_host
- tests/kstring_c_host
- tests/raw_wait_late_stop_host
- tools/fetch_fonts
- dep_dir_c/manifest #2
- tests/geforce_fgpoll_host
- tests/gf_init_contract_host
- tests/test_gserial_host
- src/model
- tools/osri_enc2_spec
- tests/gfx2_i2_core_host
- tests/ime_display_host
- tests/rework
- sdk/os32x_hdr
- tests/dep_dir_d_splash_host
- tools/pc98pt
- shell/hwb_log
- edit/command
- tests/raw_wait_guest_host
- gf/perf
- powermac/mpic
- tests/gfx2_v1_shim_host
- tests/hostsvc_sfs_lan_host
- tests/m3_lease_host
- tests/pgalloc_range_host
- tests/test_ci_stab #2
- tests/test_emu_playbook #2
- tests/test_np21w_ini_live #4
- src/trim
- cfg/cfg_backend
- shell/cmd_hdprep
- ppc/kboot_capture_fake
- tests/vfs_set_mtime_host
- src/clipboard
- src/lib #10
- kdebug/gfinit_plan_c
- kdebug/gf5_test
- ppc/lan_p20_test
- tools/host_agent
- tests/ide_pio_host
- dep_dir_c/manifest #3
- tests/gfx2_display_host
- tests/gfx2_rh
- tests/test_doc_graph
- tests/test_h5_shapes
- tables/morx
- vdpview/vdpview
- tests/x11_park_host
- tests/h5_shapes_host
- gf/feature5_service
- display/access
- tests/gfinit_budget_host
- ppc/lan_p12_fake
- host/libos32host
- tests/test_np21w_symbols
- tools/serialfs_host #2
- tests/gfinit_host
- src/lib #11
- clang_ast/type_occurrences
- tests/ring3_str_host
- tests/sep5_provider_host
- tests/lease_host
- sqlite3/sqlite3 #3
- about/Cargo
- tools/rshell_serial #2
- tests/unicode_lookup_host
- tests/trident_sync_fake
- tests/kpeek_cli_host
- tests/test_docs_status
- tests/test_guest_acceptance #2
- tests/test_memory_boot
- tests/trim_fixtures_host
- edit/sys_file
- kernel/mtrr_wc
- cirrus/wab_cirrus
- geforce/gf_v2_ops
- time/clock_service
- tools/uimock_block
- tests/vmkernel_lz4_host
- none/legacy_guest
- tools/mk_blank_nhd
- ppc/kboot_hpt_host
- tests/kapi_print_host
- console/con_sink
- tests/lzss_pkg_host
- tests/gfx2_i3_host
- tools/img2osri
- ppc/mmu_flow_fake
- tests/gfinit_c_only_host
- tests/test_hostsvc_image
- tests/test_kapi_out
- tests/test_h3_park_resume #3
- tests/trident_cpu32_host
- tests/trident_pc98init_host
- cmds/less
- cff/dict
- tables/os2
- src/cache #2
- src/input #2
- src/cache #3
- ppc/lan_p19_fake
- trident/accel
- tests/multiapp_impl_host #2
- tests/gfx_policy_host
- tests/hostsvc_init_host
- gf/clock
- gf/grctx
- module/module_image
- tests/klibc_test
- tools/host_agent #3
- ppc/flow_fake
- ppc/lan_p22_fixture
- tests/cd_pkg_host
- tests/gfinit_exec_host
- tests/gfx2_modeset_host
- tests/layout_cycle_host
- tests/test_emu_playbook #3
- tests/test_rshell_hwb
- tests/test_src_layout
- tests/fdc_seek_host
- gem/gem_phy
- kdebug/kpeek
- tools/uimock #2
- tools/uimock_hud_scroll
- sync/a0_weak
- build-setup/action
- tests/gf_prim_bench_host
- tests/gfx2_exec_host
- ppc/lan_probe
- tests/gf1_dispatch_host
- tests/gfx2_i2_host
- tests/host_cmd_host
- tests/irq_controller_host
- tests/lspci_cmd_host
- tests/test_device_reservation
- tests/test_mk_settings_db #3
- tests/test_np21w_ini #2
- tests/tri_burst2_host
- src/inject
- tests/host_sdk_rs_host
- block/fdc_track
- tests/ne2000_ring_test
- v2/enum
- shell/kbd_watch
- tests/trident_surface_lifetime_host
- mm/pgalloc #2
- tests/gfx2_core_fake
- tests/gfx_text_host
- tests/hostsvc_sdk_host
- tests/hwb_probe_engine
- tests/pcm_wav_host
- tests/trident_restore_host
- src/lerp
- edit/text_block
- edit/core
- tests/kbd_status_host
- ggg/feature_variations
- serialfs/sfs_lan
- v2/backend_reg
- tests/gfx2_check_op_host
- kdebug/recover #4
- tools/gen_font16
- tests/test_np21w_ctl #3
- ppc/kboot_cpu_host
- tests/dep_dir_b_pic_host
- tests/exec_image_host #2
- tests/gf4_platform_host
- tests/gf_v2_display_host
- tests/gfx_cursor_host
- tests/hsync_fep_protect_host
- tests/test_gfx_boot
- tests/test_serialfs #3
- tests/test_tar_cmd
- src/fixture
- src/trig
- tests/ring3_marker
- tests/display_cleanup_host
- ppc/lan_p15_fake
- tests/trident_gap_host
- exec/launch
- host/host_call
- display/flip
- display/hw_state_crtc
- tests/trim_back
- tests/pci_hwb_host
- powermac/kboot_console
- powermac/pci_config
- tools/gfinit_mode_tables
- ppc/lan_p24d_test
- tests/gf4_c2linit_host
- tests/gf_accel_b7_host
- pcm_hostshim/io
- cmap/format14
- tables/feat
- kdebug/recover #5
- kdebug/recover #6
- mm/pgalloc #3
- ppc/lan_p21_fixture
- src/ls
- tools/host_linux
- ppc/test_lan_p22d_host
- tests/dep_dir_a_l09_host
- tests/host_lib_host #2
- tests/test_nhd_deploy_failure
- tests/test_np21w_ctl #4
- tests/test_np21w_ctl #5
- tests/test_serialfs #4
- tests/widget_scroll_host #2
- tables/loca
- tables/os2 #2
- rt/dbgserial
- tests/allocator_bench
- ppc/mmu_run
- x86/exec_shlib
- video/gf_accept_cpu
- tests/sep5_display_host
- lib/divdi3
- tests/hwdev_trace_image
- kdebug/recover #7
- tests/shutdown_probe_host
- src/status
- tools/host_agent #4
- tools/host_notify
- np21w_mcp/symbols
- tests/fep_budget_linux
- tests/fep_conn_host
- tests/test_trim_flow
- tests/gf5_gpio_diag_host
- tests/test_pgalloc_model
- tests/tri_burst6_host
- tables/os2 #3
- src/random
- tests/restest
- tests/x87_host
- kernel/video_wc
- tests/exec_entry_host
- cirrus/wab_glue_xe10
- tests/sqlite_memsys5_linux
- tests/fep_module_host
- snd/libos32snd
- v2/a0_weak #3
- v2/format
- tests/hsync_sqlite_product_host
- gf/ramclock
- tools/host_agent #5
- tests/gf_surface_case_host
- tests/gf_v2_readback_host
- tests/gfx2_ckey_host
- tests/gfx2_enum_host
- tests/recover_tables_host
- tests/test_bootlog
- tests/test_deploy_protect #7
- tests/test_e9_observation
- tests/test_guest_tests #2
- tests/test_lan_bridge #2
- tests/trident_gap_shell_host
- cmds/hclip
- tables/os2 #4
- tests/e2test
- tests/clip
- input/kbd_dlog
- v2/a0_weak #4
- tests/cpu_calibrate_host
- tests/paging_gfx2_host
- powermac/display
- powermac/display_provider
- ppc/lan_p13_fake
- powermac/lan_monitor
- powermac/mpic_tree
- tests/public_surface_host
- tools/guest_tests #2
- tools/osri_enc2_row
- tests/fep_boundary_runtime
- tests/gf_command_b0_host
- tests/gfinit_owner_host
- tests/gfx2_caps_host
- tests/gfx2_owner_release_host
- tests/hostsvc_e82557_host
- tests/nic_trace_host
- tests/recover_wire_host
- tests/sbrk_crt_host
- tests/test_preflight
- tests/trident_core_host
- cmds/lpr
- cmds/more
- ggg/lookup
- src/sqrt
- edit/text_search
- ppc/exec_surface
- ppc/kboot_clock_fake
- sync/sync_write
- tests/host_exec_context
- kdebug/recover #8
- kdebug/recover #9
- tests/vk32_host
- mm/pgalloc #4
- tests/test_np21w_ctl #6
- ppc/test_user_trap_host
- tests/gf_copy_case_host
- tests/gfx2_line_host
- tests/gfx2_rop4_host
- tests/gfx2_sw_rop_host
- tests/recover_gf_inventory
- tests/recover_tables_oracle
- tests/test_check_times
- tests/test_highram_stage
- tests/test_host_agent #5
- tests/test_jobs
- tests/test_np21w_ini #3
- tests/test_physmem
- tests/tri_burst5_host
- ggg/lookup #2
- src/doc #2
- src/lib #12
- ppc/exec_font_lease
- ppc/exec_legacy
- v2/gart
- workflows/build
- mm/pgalloc #5
- mm/pgalloc #6
- pc98/gfx_selector
- tools/check_gui_proto
- ppc/kboot_mmu_guest
- tests/gf_v2_enum_host
- tests/gfx2_hostdata_host
- host_arch/arch_io
- kernel_shell_arch/arch_io
- tests/ls_client_host
- tests/test_mk_settings_db #4
- tests/tri_burst4_host
- cmds/v86
- cmds/wget
- tables/ankr
- src/clip
- src/timer
- src/atan2
- tests/faulttest
- tests/gui_busy
- tests/pcm_test
- geforce/gf_v2_life
- kdebug/recover #10
- kdebug/recover #11
- kdebug/recover #12
- kdebug/recover #13
- powermac/ime_display
- ppc/test_lan_p8_fake
- hostdrv_hostshim/io
- dep_dir_c/manifest #4
- tests/gf_foundation_width_host
- tests/gf_v2_rop3_host
- tests/gfx2_copy_host
- tests/gfx2_mono_host
- tests/gfx2_sw_fill_host
- tests/pointer_fixture_host
- tests/test_filer_normalize
- tests/tri_burst3_host
- tests/x87_lifecycle_host #2
- userland/deploy
- src/paths
- tables/os2 #5
- tables/trak
- ppc/exec_shlib
- e82557/e82557_desc #2
- hostdrv/hostdrvfs #2
- workflows/build #2
- workflows/build #3
- kdebug/recover #14
- tests/pit_clock_host
- g16/scan_ui_chars
- np21w_mcp/gui #2
- tests/gf_v2_fence_host
- tests/gfx2_readback_host
- tests/gfx2_sysmem_host
- tests/hwdev_fixture
- tests/test_docs_links
- tests/test_ext2_write_batch
- tests/test_mk_blank_nhd
- tests/test_np21w_ini #4
- tables/avar
- cff/argstack
- tables/morx #2
- apps/package_defs
- ppc/exec_font_guard
- ata/cd_body
- tests/multiapp_impl_host #3
- v2/sw_rop4
- workflows/build #4
- workflows/build #5
- kdebug/recover #15
- kdebug/recover #16
- kdebug/recover #17
- kdebug/recover #18
- kdebug/recover #19
- mm/pgalloc #7
- tests/splash_wait_host
- ci/build_cross
- tests/gui_reattach_host #2
- pegc_hostshim/io
- tests/test_harness_tools
- tests/test_mk_blank_nhd #2
- cmds/sort
- src/recip
- tests/gfx2_i3
- tests/mouse_test
- tests/pegcchk
- apps/deploy
- ppc/exec_r1
- ppc/video_wc
- sqlite3/sqlite3 #4
- tests/clock_fixture_host
- .mcp
- game/deploy
- v2/owner_release
- workflows/build #6
- kdebug/recover #20
- kdebug/recover #21
- kdebug/recover #22
- src/generations
- tests/test_ci_stab #3
- tools/np21w_ctl #3
- ppc/kboot_pdwalk_host
- tests/gf1_engine_stop_host
- tests/gf_v2_alloc_host
- tests/gfx2_heap_host
- tests/test_boot_splash_native
- tests/test_gfinit_mode
- tests/test_host_agent #6
- tests/test_np21w_ini #5
- tests/test_np21w_ini #6
- tables/os2 #6
- tests/gfx2_i1
- ppc/exec_gfx
- ppc/exec_gui
- v2/vram_heap
- kdebug/recover #23
- sqlite3/sqlite3 #5
- src/host #3
- tools/guest_tests #3
- tools/jobs
- np21w_mcp/gui #3
- ppc/build_cross
- tests/test_mk_blank_nhd #3
- tests/widget_scroll_host #3
- cff/mod
- tables/kern #2
- tables/os2 #7
- cirrus/cirrus_body
- e82557/e82557_desc #3
- exec/owner_diag
- workflows/build #7
- tools/create_fat12_d88
- ppc/known_header_dependencies
- tools/preflight
- tests/geforce_module_host #2
- tests/gfinit_fixture_host
- tests/h2_stack_host
- tests/test_caller_access
- tables/ankr #2
- tables/trak #2
- v2/a0_weak #5
- v2/a0_weak #6
- gui/types #2
- tools/ci_fetch
- ppc/kboot_reserve_fake
- tests/gui_reattach_host #3
- tests/gui_reattach_host #4
- tests/x87_lifecycle_host #3
- src/delta_set
- tables/avar #2
- tables/kerx #2
- tables/os2 #8
- tables/post
- hooks/after_compact
- hooks/protect_generated
- hostdrv/hostdrvfs #3
- v2/a0_weak #7
- v2/a0_weak #8
- v2/a0_weak #9
- tests/x87_lifecycle_host #4
- tools/audit_cast_align
- tools/build_compare
- checks/gfx2_no_weak
- tools/sync_docs_to_win
- tools/sync_hwdocs
- tests/b8_hostdrv_host #2
- tests/gui_reattach_host #5
- tests/x87_lifecycle_host #5
- src/language
- src/parser #2
- cmap/mod
- tables/feat #2
- tables/fvar
- tables/kern #3
- tables/kerx #3
- tables/math #3
- tables/name #2
- tables/svg
- data/hello
- host_tests/Cargo
- ttf-parser-0.21.1/Cargo
- src/lib #13

## God Nodes (most connected - your core abstractions)
1. `include_flags()` - 652 edges
2. `sqlite3` - 488 edges
3. `sqlite3_free()` - 451 edges
4. `run()` - 408 edges
5. `sqlite3_value` - 287 edges
6. `sqlite3_context` - 287 edges
7. `Language` - 215 edges
8. `init()` - 200 edges
9. `sqlite3VdbeExec()` - 192 edges
10. `header_sha256` - 186 edges

## Surprising Connections (you probably didn't know these)
- `case_vol()` --calls--> `pcm_vol_reg()`  [INFERRED]
  tools/tests/pcm_cs4231_host.c → drivers/sound/cs4231/pcm_cs4231_math.c
- `case_start()` --calls--> `pcm_start_plan()`  [INFERRED]
  tools/tests/pcm_cs4231_host.c → drivers/sound/cs4231/pcm_cs4231_math.c
- `snd_bgm_set_persist()` --calls--> `res_owner_get()`  [INFERRED]
  drivers/sound/fm/snd_engine.c → fs/fd_redirect.c
- `geforce_init()` --calls--> `video_framebuffer()`  [INFERRED]
  drivers/video/geforce/backend_geforce.c → hwdev/gf/service.c
- `geforce_dma_free()` --calls--> `dma_free()`  [INFERRED]
  drivers/video/geforce/geforce_dma.c → mm/dma_pool.c

## Import Cycles
- 1-file cycle: `userland/gshell/host/mocks.rs -> userland/gshell/host/mocks.rs`
- 1-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/gvar.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/gvar.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/ggg/chained_context.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/ggg/mod.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/ggg/chained_context.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/avar.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/aat.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/aat.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/ankr.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/cbdt.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/cblc.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/cff/cff2.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/colr.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/cpal.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/fvar.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/gdef.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/glyf.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/gpos.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/gsub.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/gvar.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/hmtx.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/hvar.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`
- 2-file cycle: `userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/tables/kern.rs -> userland/lib/font/vendor/ttf-parser-0.21.1/src/lib.rs`

## Communities (1285 total, 348 thin omitted)

### Community 0 - "Host Test Harness (host32)"
Cohesion: 0.01
Nodes (599): main(), exercise(), main(), exercise(), main(), exercise(), main(), exercise() (+591 more)

### Community 1 - "SQLite Core API"
Cohesion: 0.00
Nodes (863): access, os32VfsGetLastError(), os32VfsRandomness(), os32VfsSleep(), adjustStats(), analyzeFilterKeyword(), analyzeOverKeyword(), analyzeWindowKeyword() (+855 more)

### Community 2 - "SQLite Internals A"
Cohesion: 0.01
Nodes (888): addAggInfoColumn(), addAggInfoFunc(), addArgumentToVtab(), addModuleArgument(), addOp4IntSlow(), adjustOrderByCol(), agginfoPersistExprCb(), aggregateConvertIndexedExprRefToColumn() (+880 more)

### Community 3 - "Module Image Tools (mkmod)"
Cohesion: 0.01
Nodes (328): main(), generate(), main(), main(), create_schema(), main(), make_tile_blob(), populate_test_data() (+320 more)

### Community 4 - "Video Drivers & x86 Kernel"
Cohesion: 0.01
Nodes (87): arch_entry_prepare(), exec_kapi_table(), arch_video_cache_inspect(), exec_gfx_poll(), exec_db_cleanup_owned(), exec_kapi_table(), arch_entry_prepare(), arch_syscall_emit() (+79 more)

### Community 5 - "Kernel Host Tests (Python)"
Cohesion: 0.01
Nodes (353): run(), run(), commit_id(), git(), main(), render(), cross_audit(), exercise() (+345 more)

### Community 6 - "SQLite Internals B"
Cohesion: 0.01
Nodes (555): memmove(), absFunc(), addConstraintFunc(), addSpaceSeparator(), autoAdjustDate(), avgFinalize(), bytecodevtabColumn(), bytecodevtabFilter() (+547 more)

### Community 7 - "KAPI Generated Dispatch"
Cohesion: 0.01
Nodes (339): exec_host_owner_exit(), ide_get_info(), np2_check_hostdrv(), np2_detect(), np2_get_clock(), np2_get_cpu(), np2_get_version(), np2_recv_str() (+331 more)

### Community 8 - "SQLite Internals C"
Cohesion: 0.01
Nodes (483): os32_sqlite_shutdown(), addToBlockedList(), addToVTrans(), afpCheckReservedLock(), afpClose(), afpLock(), afpSetLock(), afpUnlock() (+475 more)

### Community 9 - "SQLite Internals D"
Cohesion: 0.01
Nodes (470): accessPayload(), accessPayloadChecked(), agginfoFree(), allocateBtreePage(), allocateCursor(), allocateSpace(), allocateTempSpace(), alsoAnInt() (+462 more)

### Community 10 - "SQLite & IME Dictionary Store"
Cohesion: 0.02
Nodes (451): slot_save_error(), dict_learn_once(), ime_dict_search_once(), ime_meta_read(), store_search_once(), user_list_once(), fep_user_open(), user_create() (+443 more)

### Community 11 - "SQLite Internals E"
Cohesion: 0.01
Nodes (396): memcmp(), assert_expr_depth_ok(), countLeadingZeros(), fts3auxGrowStatArray(), fts3BinCompare(), fts3ChecksumEntry(), fts3CompareElemByTerm(), fts3GrowSegReaderBuffer() (+388 more)

### Community 12 - "C Dialect & AST Checks"
Cohesion: 0.01
Nodes (206): _check_unit(), compute(), findings(), marked(), main(), check_build_flags(), _check_internal_unit(), compute() (+198 more)

### Community 13 - "Shared Library Tools (mkshlib)"
Cohesion: 0.01
Nodes (181): check(), selected_inputs(), validate_output(), absent_result(), main(), read(), check_layout(), check_spec_table() (+173 more)

### Community 14 - "LAN & FM Sound Drivers"
Cohesion: 0.01
Nodes (237): exec_gfx_poll_due(), v86_module_abort(), v86_module_load(), v86_module_session_acquire(), body_contains(), cd_detach(), cd_module_load(), cd_start() (+229 more)

### Community 15 - "SDK Separation Tests"
Cohesion: 0.01
Nodes (187): main(), query(), run_mutations(), broken_read(), read_out_budget(), forbidden_dependencies(), validate(), classify() (+179 more)

### Community 16 - "Exec App Slots & Redirection"
Cohesion: 0.01
Nodes (203): exec_selftest_run_audit(), exec_gui_owner_exit(), exec_gui_pump(), arch_kapi_invoke(), arch_kapi_read_block(), arch_kapi_va_next(), shared_stack(), v86_call_admit() (+195 more)

### Community 17 - "Font Library A"
Cohesion: 0.01
Nodes (163): BinarySearchTable, DELETED_GLYPH, END_OF_TEXT, ExtendedStateTable, GenericStateEntry, Lookup, OUT_OF_BOUNDS, START_OF_TEXT (+155 more)

### Community 18 - "Host Agent & PPC LAN Host"
Cohesion: 0.01
Nodes (129): build_user_prompt(), cmd_status(), cmd_stop(), cmd_tail(), flm_chat(), main(), pick_key_from_reply(), read_mailbox() (+121 more)

### Community 19 - "GeForce Ops & Recovery"
Cohesion: 0.01
Nodes (130): exec_gfx_poll_due(), serial_get_setup(), geforce_foundation_ops(), geforce_blit(), geforce_fill_rect(), geforce_fill_rop(), hook_deadline(), hook_feature5_poll() (+122 more)

### Community 20 - "PC-98 Splash Probe"
Cohesion: 0.02
Nodes (196): exec_serial_puts_polled(), pc98_osri_black(), pc98_osri_show(), pc98_present_rect(), pc98_set_palette(), capture_fall(), color(), diag_close() (+188 more)

### Community 21 - "PowerPC Kernel Boot"
Cohesion: 0.01
Nodes (136): arch_mmu_current_root(), arch_mmu_load_root(), _enable(), _halt(), _idle(), _irq_enabled(), _stop(), arch_exec_publish() (+128 more)

### Community 22 - "ext2 Open Host Test"
Cohesion: 0.04
Nodes (226): ext2_vfs_mkdir(), ext2_vfs_write(), any_fd_open(), b8_start_c(), block_in_use(), case_bmap_contract(), case_create_add_entry_nospc(), case_create_cleanup_failure() (+218 more)

### Community 23 - "Serial & kdebug Init"
Cohesion: 0.02
Nodes (141): pc98_serial_puts_polled(), serial_cmd_port(), serial_data_port(), serial_err_mask(), serial_fe_mask(), serial_fifo_detected(), serial_oe_mask(), serial_pe_mask() (+133 more)

### Community 24 - "Font Library B"
Cohesion: 0.01
Nodes (207): Language, Afrikaans_SouthAfrica, Albanian_Albania, Alsatian_France, Amharic_Ethiopia, Arabic_Algeria, Arabic_Bahrain, Arabic_Egypt (+199 more)

### Community 25 - "Package & Manifest Tools"
Cohesion: 0.02
Nodes (117): _dependencies(), require_fresh(), find_lychee(), link_commands(), main(), present_inputs(), docs_md(), link_targets() (+109 more)

### Community 26 - "GShell Host Layer A"
Cohesion: 0.04
Nodes (195): g3fix_control_without_input(), g3fix_gate_foreground_input_before_backs(), g3fix_model_product_ready_matrix(), g3fix_order_probe(), g3fix_stop_click_during_hook_pump(), init(), os32x_header(), present_counts() (+187 more)

### Community 27 - "V86 Graphics Capture"
Cohesion: 0.02
Nodes (103): v86_is_active(), gcap_cui_rebuild(), gcap_ran(), gcap_reset_keep(), gcap_rom(), gcap_run(), gcap_run_status(), gcap_selftest() (+95 more)

### Community 28 - "Rust os32api & GShell"
Cohesion: 0.01
Nodes (191): GUI_COLOR_ACCENT, GUI_COLOR_ALERT, GUI_COLOR_CLOSE, GUI_COLOR_DESKTOP, GUI_COLOR_DISABLED, GUI_COLOR_EDIT_BG, GUI_COLOR_FACE, GUI_COLOR_HIGHLIGHT (+183 more)

### Community 29 - "GShell Host Layer B"
Cohesion: 0.02
Nodes (152): ABORT_CLEARS, ALLOCS, ank(), APP_STATE, calls(), cfg_begin(), cfg_close(), cfg_commit() (+144 more)

### Community 30 - "Rust os32api SDK"
Cohesion: 0.02
Nodes (163): App, AppVTable, bind(), detach_gfx(), E_ARGS_PTR, E_BASE_VIOLATION_COUNT, E_BLIT, E_CFG_GET_INT (+155 more)

### Community 31 - "Game App Glue & Board View"
Cohesion: 0.03
Nodes (154): apply_event(), event_tick(), floor_value(), glue_ai_attack_cmd(), glue_ai_defend_cmd(), glue_apply_terrain(), glue_atk_cmd_name(), glue_battle_resolve_turn() (+146 more)

### Community 32 - "SQLite Internals F"
Cohesion: 0.03
Nodes (179): actionName(), checkChangeParams(), fts5HashKey2(), fts5PoslistCallback(), fts5PoslistPrefix(), fts5PrefixCompress(), fts5StorageDecodeSizeArray(), getLocalPayload() (+171 more)

### Community 33 - "Shell Truncation Host Test"
Cohesion: 0.04
Nodes (142): execute_command(), big_line(), bigscript_build(), build_api(), case_boundary(), case_cfg_set_key(), case_cmd_name_refuses(), case_env_expand_name() (+134 more)

### Community 34 - "Rust libos32gui A"
Cohesion: 0.03
Nodes (155): child_cross(), child_main(), clamp_i16(), first_child(), layout_children(), layout_window(), next_sibling(), place() (+147 more)

### Community 35 - "Shell Status Host Test"
Cohesion: 0.03
Nodes (135): sh_status_get(), sh_status_set(), build_api(), case_gfxmode_diag(), case_gfxmode_geforce(), case_gserial_gui(), case_hw_grammar(), case_profile_status() (+127 more)

### Community 36 - "Memory Map Generator"
Cohesion: 0.03
Nodes (110): addr(), bands(), plus(), v(), budget(), chain_slack(), concrete(), defects() (+102 more)

### Community 37 - "Test Fixtures"
Cohesion: 0.01
Nodes (186): header_sha256, arch/x86/arch_cpu.h, arch/x86/arch_io.h, arch/x86/kernel/double_fault.h, arch/x86/kernel/gdt.h, arch/x86/kernel/idt.h, arch/x86/kernel/tss.h, arch/x86/legacy/v86_gcap.h (+178 more)

### Community 38 - "Rust libos32gui B"
Cohesion: 0.03
Nodes (158): GuiWinSpec, style_of(), Stats, .ZERO, call(), commit(), GuiErr, .FULL (+150 more)

### Community 39 - "Build Compare & Acceptance Tools"
Cohesion: 0.02
Nodes (106): check_target(), digest(), main(), digest(), entry_code(), generate_offsets(), main(), read_mem() (+98 more)

### Community 40 - "R1 Recovery Host Tests"
Cohesion: 0.02
Nodes (89): _disable(), _stop(), exec_font_guard_active(), exec_font_guard_fault(), exec_font_guard_fixture(), exec_font_guard_run(), exec_gfx_return_emergency(), exec_r1_fixture_lease() (+81 more)

### Community 41 - "GShell Source & GUI"
Cohesion: 0.02
Nodes (96): GUI_PROTO_VERSION, GUI_SHM_OFFSET, GuiSlotHeader, GSHELL_DBG_DESC, MAGIC, VERSION, WORDS, cand_has() (+88 more)

### Community 42 - "GeForce Devinit & IRQ"
Cohesion: 0.03
Nodes (110): measure(), measure_locked(), pci_bar_get(), pci_barsize_record(), pci_cfg_read16(), pci_cfg_read32(), pci_cfg_read8(), pci_cfg_write16() (+102 more)

### Community 43 - "Legacy V86 Callers"
Cohesion: 0.02
Nodes (91): geforce_range_reserved(), geforce_resource_lookup(), gf_instmem_unmap(), gf_instmem_map(), rd(), _start(), die(), lookup_refused() (+83 more)

### Community 44 - "Rust Filer & About Apps"
Cohesion: 0.02
Nodes (110): OS32_VER_COLS, OS32_VER_DEBUG, OS32_VER_DEVICE, OS32_VER_HEAD, OS32_VER_IDENTITY, OS32_VER_INDENT, OS32_VER_KIND_MASK, OS32_VER_LINES (+102 more)

### Community 45 - "Rust GUI Stub & Demo"
Cohesion: 0.03
Nodes (101): WidgetId, .NULL, add_help(), BGM_MML, clamp(), concat(), Demo, main() (+93 more)

### Community 46 - "cff/cff2"
Cohesion: 0.02
Nodes (93): OutlineBuilder, RectF, i16, .SIZE, i32, .SIZE, Option<Offset16>, .SIZE (+85 more)

### Community 47 - "KAPI DB Host Tests"
Cohesion: 0.03
Nodes (131): db_cleanup_all(), kapi_db_close(), kapi_db_column_int(), kapi_db_column_text(), kapi_db_exec(), kapi_db_finalize(), kapi_db_last_error(), kapi_db_open() (+123 more)

### Community 48 - "SHM & DMA Host Tests"
Cohesion: 0.02
Nodes (86): exec_init(), launch_init(), fd_redirect_init(), pipe_alloc(), pipe_buffer_init(), pipe_clear(), pipe_free(), pipe_free_owned() (+78 more)

### Community 49 - "Rust Filer"
Cohesion: 0.04
Nodes (77): Rect, print_target_path_and_basename(), put(), put_num(), put_u32(), clamp(), Filer, fit() (+69 more)

### Community 50 - "cff/cff1"
Cohesion: 0.02
Nodes (113): DummyOutline, CALL_GLOBAL_SUBROUTINE, CALL_LOCAL_SUBROUTINE, CHAR_STRINGS_OFFSET, CHARSET_OFFSET, CharStringParserContext, CIDMetadata, COUNTER_MASK (+105 more)

### Community 51 - "PC-98 Legacy Video & gfx v2 Lock"
Cohesion: 0.02
Nodes (109): loop_dev_is_hdd(), cirrus_identify(), geforce_palette_init(), geforce_boot_reserve(), direct_rect(), gf_direct_get(), gf_direct_ok(), gf_direct_put() (+101 more)

### Community 52 - "src/lib"
Cohesion: 0.02
Nodes (43): Feature, Feature<'a>, FeatureList, LanguageIndex, LanguageSystem, LanguageSystem<'a>, LanguageSystemList, LayoutTable (+35 more)

### Community 53 - "IME Dictionary"
Cohesion: 0.03
Nodes (114): serial_puts(), number(), report(), geforce_wc_report(), ls_collect_entry(), vfs_fd_is_stale(), fep_conn_close(), fep_conn_ready() (+106 more)

### Community 54 - "tests/test_kstring_c"
Cohesion: 0.02
Nodes (80): run_script_in_tree(), run_with_control(), build_host(), host_cmd(), one_mutation(), run_mutations(), host_cmd(), one_mutation() (+72 more)

### Community 55 - "Console & IME"
Cohesion: 0.02
Nodes (115): exec_surface_at(), exec_surface_backend(), exec_surface_bridge_access(), exec_surface_bridge_begin(), exec_surface_bridge_end(), exec_surface_policy_reeval(), mouse_cursor_show(), geforce_direct_ok() (+107 more)

### Community 56 - "crt/help"
Cohesion: 0.02
Nodes (65): _is_help_flag(), _start_c(), build_man_path(), extract_cmd_name(), is_code_fence(), os32_help_exists(), os32_help_show(), render_line() (+57 more)

### Community 57 - "Config Library (cfg)"
Cohesion: 0.06
Nodes (122): be_fclose(), be_fopen(), be_fread(), be_fseek(), be_rename(), be_stat(), be_tick(), be_unlink() (+114 more)

### Community 58 - "GeForce Display Service"
Cohesion: 0.04
Nodes (114): gf_hook_ctx_bind(), close_gates(), command_irq(), command_stop(), gf2_recover(), gf2_start(), gf2_stop(), gf2_dma_get() (+106 more)

### Community 59 - "host/kbdnav_tests"
Cohesion: 0.05
Nodes (136): app_commit_during_drag_keeps_the_live_frame(), app_paints_and_commits(), buttons(), clear_rings(), CLIENT_SENTINEL, commit_frame_redraw_keeps_the_modal_on_top(), commit_over_modal_fixture(), commit_overlay_redraw_keeps_the_cursor() (+128 more)

### Community 60 - "src/wm"
Cohesion: 0.04
Nodes (92): p0a_busy_between_gfw1_cycles(), p0a_idle_cycles_preserve_gfw1_count(), present(), setup(), the_wm_draws_nothing_while_another_app_owns_the_screen(), w2_regen_cycle_and_pending(), Cursor, .EMPTY (+84 more)

### Community 61 - "src/settings"
Cohesion: 0.03
Nodes (123): cfg(), cfg_calls(), cfg_get_int(), cfg_last_close_error(), cfg_open_calls(), cfg_status(), CfgCall, Begin (+115 more)

### Community 62 - "tests/test_result_conv_host"
Cohesion: 0.03
Nodes (87): cap_reset(), check(), checkf(), fake_api_init(), fd1_addn(), fk_get_tick(), fk_kprintf(), fk_mem_alloc() (+79 more)

### Community 63 - "tests/test_font_raster"
Cohesion: 0.02
Nodes (73): allowed(), main(), scan(), find_objects(), load_allow(), main(), objdump_bin(), scan_object() (+65 more)

### Community 64 - "sqlite3/os32_sqlite_vfs"
Cohesion: 0.04
Nodes (113): vfs_sqlite_claim_main(), vfs_sqlite_fd_identity(), vfs_sync_fd(), os32_sqlite_test(), budget_free(), budget_malloc(), budget_realloc(), budget_roundup() (+105 more)

### Community 65 - "tests/test_geforce"
Cohesion: 0.02
Nodes (77): build(), generate(), main(), ownerless_body(), build_font(), Dispatch, Runtime, stats() (+69 more)

### Community 66 - "tests/gf5_fake"
Cohesion: 0.06
Nodes (122): gf4_host_reads(), calculate(), fixture(), guard_cases(), readback_cases(), reject(), run(), set_reg() (+114 more)

### Community 67 - "src/modal"
Cohesion: 0.04
Nodes (133): mark_redraw(), activate(), after_text_change(), BTN_GAP, BTN_H, BTN_W, build_selection(), button_label() (+125 more)

### Community 68 - "tests/vfs_fd_path_host"
Cohesion: 0.04
Nodes (107): cd_module_abort(), vfs_fd_pinned_busy(), vfs_rename(), vfs_rmdir(), wrap_sys_rmdir(), _start(), fatfs_init(), pc98_boot_names_bind() (+99 more)

### Community 69 - "tests/gf5_foreground_host"
Cohesion: 0.03
Nodes (78): geforce_backend_feature5_bind(), geforce_prepare(), geforce_probe(), geforce_tick(), gf5_rom_acquire(), gf5_rom_release(), rom_error(), geforce_heat_latch_clear() (+70 more)

### Community 70 - "legacy/v86"
Cohesion: 0.03
Nodes (99): irq_restore(), irq_save(), exec_legacy_abort_begin(), exec_legacy_call_dispatch(), exec_legacy_cleanup(), exec_legacy_closing(), exec_legacy_end(), exec_legacy_lowmap_busy() (+91 more)

### Community 71 - "ramfs/ramfs"
Cohesion: 0.02
Nodes (87): dev_get_names(), loop_dev_init(), ram_ro_dev_read(), ram_ro_dev_write(), ram_ro_read(), ram_ro_register(), pc98_read_date(), rtc_init() (+79 more)

### Community 72 - "ekakiuta/ekakiuta"
Cohesion: 0.04
Nodes (92): animate_circle(), animate_line(), main(), setup_palette(), show_guide(), show_lyric(), step_bezier_demo(), step_body() (+84 more)

### Community 73 - "tests/test_vfs_fd_path"
Cohesion: 0.03
Nodes (72): check_rebuild_table(), run_timeout(), timeout_red(), TimeoutVerdict, build(), build_target(), _commands(), _FirstFail (+64 more)

### Community 74 - "tables/gpos"
Cohesion: 0.03
Nodes (62): Class, ClassDefinition, Format1, Format2, Offset16, .SIZE, Anchor, Anchor<'a> (+54 more)

### Community 75 - "legacy/v86_pic"
Cohesion: 0.02
Nodes (80): arch_cpu_has_cpuid(), arch_cpu_has_mmx(), arch_cpu_serialize(), arch_mmu_current_root(), arch_mmu_load_root(), _lidt(), arch_boot_exception_stack(), arch_boot_segments() (+72 more)

### Community 76 - "ggg/mod"
Cohesion: 0.02
Nodes (46): ClassDefinition<'a>, Coverage, Coverage<'a>, Format1, Format2, LazyArray16<'_, RangeRecord>, RangeRecord, .SIZE (+38 more)

### Community 77 - "tests/pegc_mode_host"
Cohesion: 0.05
Nodes (96): pegc_enter_480_ports(), pegc_init(), pegc_prepare(), pegc_probe(), pegc_sync_drop(), boot_with(), bootlog_push(), case_boot_clock() (+88 more)

### Community 78 - "tests/net_link_host"
Cohesion: 0.07
Nodes (104): wrap_host_open(), kapi_host_open(), check_owner(), link_host_close(), link_host_open(), link_host_owner_exit(), link_host_read_stage(), link_host_status() (+96 more)

### Community 79 - "src/gserial"
Cohesion: 0.03
Nodes (95): command(), gserial_config_default_disabled_and_exact_response_prefixes(), gserial_ctrl_escape_follows_local_input_and_pump_does_not_read(), gserial_cui_two_stage_cfg_failure_and_success_restore_rshell(), gserial_disabled_sends_no_notifications_or_cui_output(), gserial_framing_resync_control_length_and_ids(), gserial_lost_counts_raw_and_rejected_fullscreen_injection(), gserial_one_line_per_cycle() (+87 more)

### Community 80 - "tests/gf_accel_c1_host"
Cohesion: 0.06
Nodes (95): bad_command(), call(), prepare(), reference(), snapshot(), wait_fault(), gf_display_quiesce(), gf_display_stop() (+87 more)

### Community 81 - "mm/pgalloc"
Cohesion: 0.04
Nodes (92): sys_memory_bootstrap_model(), sys_memory_init_model(), sys_memory_stage_online(), sys_phys_end(), sys_usable_mem_end(), memory_boot_add_high(), memory_boot_boot_areas(), memory_boot_count_pages() (+84 more)

### Community 82 - "fatfs/ff"
Cohesion: 0.08
Nodes (118): change_bitmap(), check_fs(), chk_share(), clear_share(), clmt_clust(), clst2sect(), cmp_lfn(), create_chain() (+110 more)

### Community 83 - "tests/gf_accel_host"
Cohesion: 0.05
Nodes (102): _start(), fresh(), _start(), test(), exercise(), expected_rop(), packet(), _start() (+94 more)

### Community 84 - "tables/gvar"
Cohesion: 0.03
Nodes (77): NormalizedCoordinate, F2DOT14, .SIZE, GlyphPoint, Control, .DELTA_RUN_COUNT_MASK, .DELTAS_ARE_WORDS_FLAG, .DELTAS_ARE_ZERO_FLAG (+69 more)

### Community 85 - "kdebug/recover"
Cohesion: 0.02
Nodes (106): pci_locate_fn0(), reserve(), reserve_fail(), gf_production_identify(), recover_gf_dev(), recover_gf_second(), gf_stage_state, recover_arg_kind (+98 more)

### Community 86 - "inv/inv_core"
Cohesion: 0.05
Nodes (76): glue_devil_transform(), glue_init(), inv_add(), inv_bag_init(), inv_count_item(), inv_free_slots(), inv_get_slot(), inv_has() (+68 more)

### Community 87 - "src/host"
Cohesion: 0.03
Nodes (52): GuiIcon16, .ZERO, ICON_EXEC, ICON_FILE, ICON_FOLDER, draw_icon16(), ICON16_SIZE, run() (+44 more)

### Community 88 - "src/draw"
Cohesion: 0.04
Nodes (88): check_gfx(), resolve_target(), Target, ANK_W, ANK_YEN, base_violation_count(), blit(), cell_from_glyph() (+80 more)

### Community 89 - "tests/kapi_audit_host"
Cohesion: 0.02
Nodes (57): wrap_fm_play_mml(), wrap_ime_getchar(), wrap_ime_getkey(), wrap_kbd_getchar(), wrap_rshell_set_active(), input_getchar_kapi(), ktime_ticks(), port_write() (+49 more)

### Community 90 - "tests/osri_view_host"
Cohesion: 0.03
Nodes (73): v86_call_init(), iso9660_mount(), serialfs_is_mounted(), gfx_osri_view(), wrap_gfx_osri_show(), kapi_osri_show(), kapi_print_call(), kapi_strings_cleanup_owned() (+65 more)

### Community 91 - "src/font"
Cohesion: 0.04
Nodes (92): bare(), f10_open_uses_leases_outside_guard_and_reads_only_inside(), f11_reopen_closes_old_leases_first_and_bumps_slot(), f12_open_failures_pick_the_reason_and_retry_once(), f13_engine_open_failure_inside_guard_defers_the_close(), f14_text_checks_once_and_guards_only_on_miss(), f15_guard_busy_draws_this_call_with_kcg_only(), f16_stale_lease_falls_back_to_kcg_and_closes() (+84 more)

### Community 92 - "checks/run"
Cohesion: 0.05
Nodes (38): build_content(), changed_files(), Check, closure(), compiler_headers(), Context, declaration_paths(), default_base() (+30 more)

### Community 93 - "tests/test_deploy_protect"
Cohesion: 0.03
Nodes (29): check_dest(), check_root_etc(), guest_path_of(), _inside(), is_protected(), mkdir_chain(), name_is_protected(), normalize_guest_path() (+21 more)

### Community 94 - "src/lib #2"
Cohesion: 0.03
Nodes (98): Change, Fullscreen, None, Restored, f(), Fs, FS, .NEW (+90 more)

### Community 95 - "src/lib #3"
Cohesion: 0.03
Nodes (77): check_gfx(), ATTR_CYAN, ATTR_GREEN, ATTR_RED, ATTR_WHITE, ATTR_YELLOW, KAPI_DATA_FIELDS_OFF, KAPI_FUNC_CAPACITY (+69 more)

### Community 96 - "tests/vfs_fd_sqlite_host"
Cohesion: 0.03
Nodes (78): detect_format(), lba_slot_ok(), loop_dev_attach(), loop_dev_detach(), loop_dev_get_geometry(), loop_dev_get_sec_n(), loop_dev_read_lba(), loop_dev_write_lba() (+70 more)

### Community 97 - "tests/hostsvc_netcd_host"
Cohesion: 0.04
Nodes (78): vfs_device_names_register(), host_mailbox_commit(), host_mailbox_init(), host_mailbox_latest_serial(), host_mailbox_put(), host_mailbox_stage(), remove_at(), serialfs_init() (+70 more)

### Community 98 - "tests/gfx_attach_host"
Cohesion: 0.04
Nodes (74): main(), os32_start_c(), main(), asm_blit_transparent_core(), asm_fill_plane_rect(), asm_gfx_clear(), asm_gfx_draw_sprite_core(), asm_gfx_hline() (+66 more)

### Community 99 - "tests/gui_text_host"
Cohesion: 0.04
Nodes (88): ascii_lines(), BLOCKS, CALLS, Cell, CFG, CH, check(), corpus() (+80 more)

### Community 100 - "src/taskbar"
Cohesion: 0.04
Nodes (87): s13_shrinking_clock_dirties_the_union_of_old_and_new(), covers(), dither50(), dither50_in(), dotted_rect(), dotted_rect_in(), draw_drag_outline(), draw_text_in() (+79 more)

### Community 101 - "tests/splash_obs_host"
Cohesion: 0.04
Nodes (66): le_read16(), le_read32(), le_read_n(), le_write16(), le_write32(), link_credit_pages(), link_drop_all(), link_init() (+58 more)

### Community 102 - "tables/colr"
Cohesion: 0.04
Nodes (48): DeltaSetIndexMap, RgbaColor, BaseGlyphPaintRecord, .SIZE, BaseGlyphRecord, .SIZE, ClipBox, ClipList (+40 more)

### Community 103 - "tests/fdc_track_host"
Cohesion: 0.04
Nodes (89): fdc_get_known_cyl(), fdc_get_stats(), fdc_set_media(), fdc_track_invalidate(), inp(), outp(), cache_reset(), cache_rules() (+81 more)

### Community 104 - "ui/microui"
Cohesion: 0.07
Nodes (95): demo_ui(), main(), cb_text_height(), cb_text_width(), draw_icon(), draw_text_clipped(), feed_char(), fill_rect_clipped() (+87 more)

### Community 105 - "tests/font_source_host"
Cohesion: 0.05
Nodes (81): exec_font_lease_close_owned(), exec_font_lease_close_shell(), exec_font_lease_shell_close(), exec_font_lease_shell_open(), exec_font_lease_shell_read_at(), exec_font_lease_shell_validate(), vfs_close_font(), vfs_font_read_at() (+73 more)

### Community 106 - "tools/img2vbz"
Cohesion: 0.03
Nodes (65): assign_village_ids(), build(), check(), main(), region(), write_db(), render_overview(), main() (+57 more)

### Community 107 - "host/movecopy_tests"
Cohesion: 0.04
Nodes (68): pixels(), b5_modal_caret_follows_the_latin_half_rule(), COPIES, COPY_RC, FLUSH_RC, has(), movecopy_capacity_falls_back_to_paint(), movecopy_failure_repaints_and_frame_damage_excludes_copy() (+60 more)

### Community 108 - "tools/nhd_deploy"
Cohesion: 0.04
Nodes (44): configure_profile(), do_format(), do_init(), do_ls(), do_migrate_pt(), do_mkdirs(), do_mount(), do_pull() (+36 more)

### Community 109 - "asset/asset_core"
Cohesion: 0.03
Nodes (53): check(), main(), script_reset(), ck_ascii(), ck_name(), main(), main(), read_file() (+45 more)

### Community 110 - "ext2/ext2_super"
Cohesion: 0.06
Nodes (61): ext2_add_entry(), ext2_delete_entry(), ext2_find_entry(), ext2_find_entry_loc(), ext2_list_dir(), ext2_lookup(), ext2_name_check(), ext2_same_sector() (+53 more)

### Community 111 - "tables/math"
Cohesion: 0.04
Nodes (20): ChainedSequenceRule<'a>, ChainedSequenceRuleSet<'a>, SequenceRule<'a>, SequenceRuleSet<'a>, FromSlice, AlternateSet<'a>, Ligature<'a>, LigatureSet<'a> (+12 more)

### Community 112 - "md/md_render"
Cohesion: 0.04
Nodes (64): build_toc(), ci_match(), do_toc_jump(), draw_toc_overlay(), find_next(), main(), mdview_gfx_init(), mdview_gfx_shutdown() (+56 more)

### Community 113 - "tools/check_arm_compile"
Cohesion: 0.03
Nodes (67): build_flags(), classify(), compile_one(), first_error(), inc_var_for(), is_known_non_kernel(), is_temp_asm(), main() (+59 more)

### Community 114 - "tests/gf5_therm_host"
Cohesion: 0.08
Nodes (75): contains(), reset(), _start(), sys_time_now(), cleanup_fault(), focused(), invoke(), number() (+67 more)

### Community 115 - "tests/install_fresh_host"
Cohesion: 0.06
Nodes (78): api_init(), bad_disk(), case_boot_fail(), case_bounds(), case_copy_fail(), case_decline(), case_erase(), case_erase_fail() (+70 more)

### Community 116 - "tests/gf4_display_fake"
Cohesion: 0.07
Nodes (83): _start(), test(), _start(), test(), _start(), deadlines(), failures(), getters() (+75 more)

### Community 117 - "tests/test_h3_park_resume"
Cohesion: 0.04
Nodes (18): Tests, click(), observe(), sleep(), stop(), freeze(), pump(), start() (+10 more)

### Community 118 - "tests/sqlite_module_host"
Cohesion: 0.05
Nodes (78): fep_module_abort(), services_settings(), sqlite_engine_stack_high_water(), sqlite_module_ready(), os32_sqlite_column_int(), os32_sqlite_exec(), os32_sqlite_outer_group_acquire(), os32_sqlite_outer_group_begin_close() (+70 more)

### Community 119 - "tests/ext2_write_io_host"
Cohesion: 0.06
Nodes (82): ext2_fill_stat(), ext2_path_is_root(), ext2_resolve_path(), ext2_split_path(), ext2_sqlite_resize(), ext2_sqlite_writable(), ext2_to_vfs_err(), ext2_vfs_block_size() (+74 more)

### Community 120 - "tests/bootlog_host"
Cohesion: 0.05
Nodes (79): bl_put_dec(), bl_put_hex8(), bl_put_str(), bl_streq(), bl_trim_partial_tail(), bl_utf8_len(), bootlog_compose(), bootlog_dropped() (+71 more)

### Community 121 - "ecs/ecs_core"
Cohesion: 0.05
Nodes (57): chem_adapter(), main(), on_destroy(), on_timer_expire(), print_frame(), spawn_enemy(), spawn_player(), spawn_projectile() (+49 more)

### Community 122 - "tests/cdinst_host"
Cohesion: 0.07
Nodes (80): any_outside(), arena_get(), bad_disk(), boot_pkg(), cap_ch(), cap_num(), cap_vfmt(), case_erase() (+72 more)

### Community 123 - "tests/test_deploy_protect #2"
Cohesion: 0.05
Nodes (20): do_copy(), do_copy_all(), Base, HostdrvSync, ManifestEntryPoints, NhdCli, NhdSync, Review2AncestorOnFinalPath (+12 more)

### Community 124 - "tests/sh_shell_host"
Cohesion: 0.05
Nodes (64): f, build_api(), case_argv_bound(), case_ask_backspace(), case_backspace_erases(), case_copy_failure(), case_exit_breaks_goto_loop(), case_exit_stops_rest() (+56 more)

### Community 125 - "src/cache"
Cohesion: 0.04
Nodes (58): ascent16(), bucket_of(), byte(), C_NEXT, C_SET, C_W0, Cell, Full (+50 more)

### Community 126 - "tables/math #2"
Cohesion: 0.03
Nodes (79): ACCENT_BASE_HEIGHT_OFFSET, AXIS_HEIGHT_OFFSET, Constants, DELIMITED_SUB_FORMULA_MIN_HEIGHT_OFFSET, DISPLAY_OPERATOR_MIN_HEIGHT_OFFSET, FLATTENED_ACCENT_BASE_HEIGHT_OFFSET, FRACTION_DENOM_DISPLAY_STYLE_GAP_MIN_OFFSET, FRACTION_DENOMINATOR_DISPLAY_STYLE_SHIFT_DOWN_OFFSET (+71 more)

### Community 127 - "geforce/backend_geforce"
Cohesion: 0.04
Nodes (64): display_deadline(), display_format(), display_ops(), display_result(), feature5_poll(), geforce_backend_bind(), geforce_backend_framebuffer_info(), geforce_backend_stop_bind() (+56 more)

### Community 128 - "tools/rshell_recover"
Cohesion: 0.04
Nodes (45): FilterPort, main(), abort_frame(), exchange(), record_failure(), main(), save(), open_port() (+37 more)

### Community 129 - "shell/main"
Cohesion: 0.05
Nodes (76): sh_boot(), sh_boot(), cmd_exit(), cmd_time(), cmd_ver(), shell_cmd_base_init(), shell_print_kdebug(), cmd_cd() (+68 more)

### Community 130 - "display/modeset"
Cohesion: 0.04
Nodes (58): gfinit_mode_calc(), mode_bit(), gf_mode_cursor(), gf_mode_dac_format(), dp_check(), dp_crtc(), dp_read(), dp_screen() (+50 more)

### Community 131 - "tables/name"
Cohesion: 0.03
Nodes (61): Subtable10, EncodingRecord, .SIZE, Format, ByteEncodingTable, HighByteMappingThroughTable, ManyToOneRangeMappings, MixedCoverage (+53 more)

### Community 132 - "tests/gfx_accel_hal_host"
Cohesion: 0.03
Nodes (46): ring3_user_range_writable(), gfx_hw_mono_cpu(), gfx_hw_submit(), submit_one(), transfer_range(), check_one(), fields(), gfx2_submit() (+38 more)

### Community 133 - "src/font #2"
Cohesion: 0.04
Nodes (53): app_font_lifecycle(), bundle_phase(), check(), font_root(), install(), k(), K, Kapi (+45 more)

### Community 134 - "cfg/libos32cfg"
Cohesion: 0.07
Nodes (72): c_r3_tsv_long(), en_cb_reenter(), cfg_enum(), cfg_enum_scopes(), copy_name(), enum_ready(), build_new(), init_done() (+64 more)

### Community 135 - "tests/cirrus_win_host"
Cohesion: 0.05
Nodes (64): cirrus_blit(), cirrus_clip(), cirrus_cpu_fill(), cirrus_cpu_write(), cirrus_enter(), cirrus_fill_rect(), cirrus_init(), cirrus_leave() (+56 more)

### Community 136 - "tests/econ_test"
Cohesion: 0.06
Nodes (59): econ__curve_lookup(), econ_debug_dump(), econ__find_currency(), econ__find_diplomacy(), econ__find_item(), econ__find_market(), econ__find_merchant(), econ__find_recipe() (+51 more)

### Community 137 - "tests/exec_heap_host"
Cohesion: 0.05
Nodes (60): appmem_map(), appmem_exec_trim(), appmem_exec_unmap(), appmem_unmap(), unmap_mask(), appslot_init(), appslot_trim_done(), exec_heap_alloc() (+52 more)

### Community 138 - "src/kbdnav"
Cohesion: 0.04
Nodes (87): shortcut(), advance_switch(), begin_switch(), BTN_LEFT, BTN_RIGHT, build_switch_list(), cancel_switch(), click() (+79 more)

### Community 139 - "src/input"
Cohesion: 0.06
Nodes (80): commit_frame_redraw_under_the_cursor_saves_the_real_background(), file_dialog_refuses_names_that_do_not_fit_instead_of_truncating(), CUR_H, CUR_W, discard(), gfx_cmd_enqueue_mono(), gfx_cmd_enqueue_put(), gfx_cmd_get() (+72 more)

### Community 140 - "tests/hwb_host"
Cohesion: 0.04
Nodes (35): add_arguments(), inspect(), inspect_file(), run(), terminal_row(), build(), build_command(), cross_gcc() (+27 more)

### Community 141 - "tests/gfx_transfer_host"
Cohesion: 0.05
Nodes (56): add_sprites(), main(), make_sprite(), rnd(), write_log(), build_assets(), main(), rnd() (+48 more)

### Community 142 - "chem/chem_core"
Cohesion: 0.07
Nodes (61): chem_active_count(), chem_add_element(), chem_add_temperature(), chem_destroy(), chem_get(), chem__get_callback(), chem__get_objects(), chem__get_phase_count() (+53 more)

### Community 143 - "tests/text_test"
Cohesion: 0.08
Nodes (67): parse_pages(), slot_reset(), slot_valid(), text_close(), text_debug_dump(), text__fire_done(), text__get_slot(), text_init() (+59 more)

### Community 144 - "tests/gf4_c2svc_host"
Cohesion: 0.07
Nodes (72): gf4_service_owner_table(), c1_admission(), c1_clear_trace(), c1_expect(), c1_failed_stop(), c1_failures(), c1_flips(), c1_formats() (+64 more)

### Community 145 - "emu_agent/agent"
Cohesion: 0.03
Nodes (39): act_cmd_nowait(), act_deploy(), act_make(), act_selftest(), clip(), _host_run(), llm_chat(), load_tasks() (+31 more)

### Community 146 - "gf/push"
Cohesion: 0.05
Nodes (47): gf3_present_m2mf(), gf_v2_fail(), gf_v2_fence_emit(), gf_v2_fence_idle(), gf_v2_fence_state(), gf_v2_fence_wait(), kick(), copy_overlap() (+39 more)

### Community 147 - "src/startmenu"
Cohesion: 0.05
Nodes (80): s07b_choosing_settings_only_reserves_the_request(), winmenu_enabled(), activate(), BORDER, build_prog_path(), close(), confirm_halt(), CT_FILEMAN (+72 more)

### Community 148 - "tests/test_hdd_stage1"
Cohesion: 0.04
Nodes (46): gcc_deps(), _build(), build_part(), build_pure(), build_target(), _capture_err(), _controls(), e2fsck_part() (+38 more)

### Community 149 - "tests/save_test"
Cohesion: 0.04
Nodes (47): build_save_context(), cga_bits(), invariants(), main(), pc98_passthrough(), real_callers(), head_buffer(), head_file() (+39 more)

### Community 150 - "tests/irq_math_host"
Cohesion: 0.05
Nodes (50): isr_unexpected_report(), pcm_e4_arm(), pcm_e4_attach(), pcm_e4_dispatch_enter(), pcm_e4_dispatch_leave(), pcm_e4_fake_irq(), pcm_e4_irq(), pcm_e4_passed() (+42 more)

### Community 151 - "tests/fatfs_read_cache_host"
Cohesion: 0.06
Nodes (61): FSIZE_t, chs_to_lba(), fat_name_fold(), fatfs_vfs_block_size(), fatfs_vfs_free_blocks(), fatfs_vfs_get_size(), fatfs_vfs_mkdir(), fatfs_vfs_read() (+53 more)

### Community 152 - "tests/test_mk_settings_db"
Cohesion: 0.04
Nodes (37): create_schema(), main(), populate_elements(), populate_object_types(), populate_phase_transitions(), populate_reactions(), build_game_estates(), generate() (+29 more)

### Community 153 - "src/fep"
Cohesion: 0.06
Nodes (76): movecopy_fep_preedit_is_repainted(), anchor(), apply_pending_commit(), apply_pending_toggle(), attr_color(), box_rect(), cancel_input(), CELL_H (+68 more)

### Community 154 - "src/multiapp"
Cohesion: 0.07
Nodes (79): op_wait(), abort_at_top_level(), abort_target(), abort_targets_current(), adopt_running(), App, APP_ID_MAX, APP_ID_MIN (+71 more)

### Community 155 - "tests/btl_test"
Cohesion: 0.06
Nodes (50): btl_calc_damage(), btl_calc_dodge_rate(), btl_calc_flee_rate(), btl_calc_magic_damage(), btl_effective_def_guard(), btl_set_damage_policy(), btl_set_magic_policy(), btl_get_element_count() (+42 more)

### Community 156 - "gf/command"
Cohesion: 0.03
Nodes (66): gf2_dma_buf, pa, size, va, gf2_dma_ops, gf2_fault, cache_get, caches (+58 more)

### Community 157 - "src/engine"
Cohesion: 0.05
Nodes (38): Bundle, BundleStats, ascent_of(), At, Nowhere, Scratch, Slot, bundle_entry() (+30 more)

### Community 158 - "tests/dep_dir_b_io_host"
Cohesion: 0.04
Nodes (60): kcg_cache_stats(), kcg_rom_read_ank(), kcg_rom_read_kanji(), kcg_v86_restore(), public_api(), stub_ank(), stub_kanji(), stub_stats() (+52 more)

### Community 159 - "tests/hostsvc_nic_host"
Cohesion: 0.04
Nodes (55): net_tick(), irq_restore(), irq_save(), hs_if(), hs_set_if(), acquire(), acquire_e(), acquire_l() (+47 more)

### Community 160 - "tests/hsync_h2_host"
Cohesion: 0.11
Nodes (66): main(), prepare(), test_format(), boot_pair(), case_a14a(), case_a14a2(), case_a14a3(), case_a14b() (+58 more)

### Community 161 - "tests/test_np21w_ctl"
Cohesion: 0.07
Nodes (5): AfterStart, Arguments, Cd, Fdd, WaitReady

### Community 162 - "tilemap/tilemap_compose"
Cohesion: 0.06
Nodes (56): draw_hud(), key_trigger(), keys_poll(), keys_save(), main(), tilemap_fill(), tilemap_get(), tilemap_scroll_sync() (+48 more)

### Community 163 - "tests/x87_lifecycle_host"
Cohesion: 0.04
Nodes (48): appmem_init(), appslot_cui_only_admit(), appslot_mark_scheduled(), appslot_reclaim(), appslot_start_admit(), appslot_start_commit(), arch_fp_init(), arch_fp_restore() (+40 more)

### Community 164 - "tests/math_test"
Cohesion: 0.07
Nodes (58): isin(), gfx_deg_to_idx(), gfx_icos(), gfx_isin(), angle_between(), iatan2(), fix16_abs(), fix16_ceil() (+50 more)

### Community 165 - "tests/gfx2_i1_host"
Cohesion: 0.03
Nodes (41): arch_exec_image_accept(), os32x_decode(), os32x_layout_check(), vfs_open(), defaults(), eq(), field(), gfxsync_cfg_lookup() (+33 more)

### Community 166 - "tests/exec_image_host"
Cohesion: 0.05
Nodes (55): appmem_init(), appslot_at(), appslot_cui_only_admit(), appslot_mark_scheduled(), appslot_start_admit(), appslot_start_commit(), argv_case(), argv_done() (+47 more)

### Community 167 - "tests/ver_collect_host"
Cohesion: 0.05
Nodes (40): defaults(), gfxsync_bind(), gfxsync_cfg_check(), gfxsync_mode(), gfxsync_override(), gfxsync_override_clear(), gfxsync_profile(), gfxsync_unbind() (+32 more)

### Community 168 - "tests/ext2_empty_name_host"
Cohesion: 0.06
Nodes (58): case_cdinst(), case_ext2(), case_legacy(), case_root(), case_synth(), case_vfs(), cd_index(), check_at() (+50 more)

### Community 169 - "map/map_core"
Cohesion: 0.06
Nodes (53): load_encounters(), load_events(), load_tile_props(), load_warps(), map_current(), map_debug_dump(), map__encounter_count(), map__event_count() (+45 more)

### Community 170 - "np21w_mcp/server"
Cohesion: 0.06
Nodes (51): _curl_response(), EmuError, get(), _http(), post(), token_file(), token_headers(), _annotate_eip() (+43 more)

### Community 171 - "tests/kapi_ranges_host"
Cohesion: 0.05
Nodes (51): exec_heap_reset(), kaudit_boot_pages(), appslot_at(), dev_blk_read_lba(), dev_blk_write_lba(), dev_find(), dispatch_early(), exec_heap_total() (+43 more)

### Community 172 - "econ/econ_core"
Cohesion: 0.08
Nodes (60): load_enemies(), econ_active_market_count(), econ_init(), econ_item_count(), econ_market_count(), econ_reset(), econ_set_price_callback(), econ_set_price_policy() (+52 more)

### Community 173 - "sqlite3/sqlite3"
Cohesion: 0.09
Nodes (67): AdjustTree(), cellArea(), cellContains(), cellMargin(), cellOverlap(), cellUnion(), ChooseLeaf(), deleteCell() (+59 more)

### Community 174 - "tests/raster"
Cohesion: 0.07
Nodes (58): Placement, bbox_limits(), bitmap_box_uses_26_6_points(), composite(), control_box_includes_off_curve_points(), coordinate_overflow(), destination_capacity(), diagonal_coverage_is_exact_area() (+50 more)

### Community 175 - "tests/install_recover_host"
Cohesion: 0.10
Nodes (60): case_approve(), case_backup_fail(), case_chain(), case_gate(), case_happy(), case_master(), case_master_copy(), case_media() (+52 more)

### Community 176 - "tests/gfx_kernel_fb_host"
Cohesion: 0.04
Nodes (41): appslot_at(), appslot_get(), cpu_delay_us(), die(), exec_tramp_page_addr(), gfx2_sync_publish(), gfx_policy_blocks_map(), gfxsync_bind() (+33 more)

### Community 177 - "cmds/tar"
Cohesion: 0.07
Nodes (67): checksum(), file_close(), file_read(), file_seek(), file_write(), header_to_raw(), mtar_close(), mtar_copy_field() (+59 more)

### Community 178 - "tests/kbd_dlog_host"
Cohesion: 0.04
Nodes (34): kbd_trygetrawkey(), kbd_diag(), kbd_diag_log(), kbd_discard_stop(), kbd_dropped_count(), kbd_get_modifiers(), kbd_is_pressed(), ring3_user_ranges_writable() (+26 more)

### Community 179 - "tests/gfx2_backend_reg_host"
Cohesion: 0.04
Nodes (32): fail(), fake_caps(), fake_cursor_image(), fake_cursor_move(), fake_cursor_show(), fake_dpms(), fake_edid(), fake_emit() (+24 more)

### Community 180 - "ata/ide"
Cohesion: 0.04
Nodes (40): ide_addr_mode_of(), ide_chs_addr(), ide_drive_present(), ide_get_geom(), ide_identify(), ide_init(), ide_range_ok(), ide_select_bank() (+32 more)

### Community 181 - "input/kbd"
Cohesion: 0.06
Nodes (55): serial_peekchar(), serial_trygetchar(), kbd_bios_shift_peek(), kbd_deliver(), kbd_diag(), kbd_diag_log(), kbd_dropped_count(), kbd_get_modifiers() (+47 more)

### Community 182 - "tests/cd_read_host"
Cohesion: 0.09
Nodes (57): atapi_media_gen(), big_secs(), bound(), build_media(), ceil_div(), cpu_delay_us(), do_mount(), file_byte() (+49 more)

### Community 183 - "tests/test_hostdrv_manifest"
Cohesion: 0.06
Nodes (33): build_id(), deployed_kapi_layout(), do_diff(), do_ls(), do_sync(), guard_root(), kapi_json_layout(), load_deploy_yaml() (+25 more)

### Community 184 - "src/limits"
Cohesion: 0.03
Nodes (70): APP_PRELOAD_MAX, ARENA_BYTES_APP, ARENA_BYTES_SHELL, ASCENT_JA_PX, ASCENT_LATIN_PX, BUCKETS, CACHE_STATIC_BYTES, CELL_BITMAP_BYTES (+62 more)

### Community 185 - "tests/board_test"
Cohesion: 0.10
Nodes (53): board_add_connection(), board_add_mass(), board_area_count(), board_is_area_unlocked(), board_lock_area(), board_remove_connection(), board_unlock_area(), board_find_index() (+45 more)

### Community 186 - "tools/h3_park_resume"
Cohesion: 0.08
Nodes (26): capture_counts(), capture_trace(), get(), post(), CaptureTrap, debug_reply(), digest(), Emulator (+18 more)

### Community 187 - "src/parser"
Cohesion: 0.04
Nodes (14): i8, .SIZE, LazyArray16<'a, T>, LazyArray16<'_, T>, LazyArray32<'a, T>, LazyArrayIter16<'a, T>, LazyArrayIter16<'_, T>, LazyArrayIter32<'a, T> (+6 more)

### Community 188 - "tests/ext2_read_bound_host"
Cohesion: 0.08
Nodes (59): bounds(), canaries(), dev_blk_read_lba(), dev_blk_write_lba(), errors(), fail(), finish(), kprintf() (+51 more)

### Community 189 - "src/prompt"
Cohesion: 0.04
Nodes (52): attached_row(), attached_row_names_the_child_and_offers_cancel(), CANCELLING, CANCELLING_HINT, Candidates, candidates(), candidates_drop_what_launch_req_cannot_take(), candidates_follow_usr_bin_then_bin() (+44 more)

### Community 190 - "kdebug/recover #2"
Cohesion: 0.03
Nodes (69): recover, access_blocked, accessed, bars, bound_hash, bound_profile, budget, buffers (+61 more)

### Community 191 - "tests/gf4_a3_host"
Cohesion: 0.09
Nodes (59): abandon_call(), buffer_crc(), case_event(), case_failures(), clock_read(), cui_controls(), cursor_flip_failures(), cursor_image_call() (+51 more)

### Community 192 - "bench_scale2x/main"
Cohesion: 0.06
Nodes (48): asm_fill_plane_rect(), asm_gfx_clear(), asm_gfx_hline(), asm_gfx_line(), asm_kcg_draw_font(), check(), finish(), get() (+40 more)

### Community 193 - "src/reader"
Cohesion: 0.08
Nodes (31): be16(), be32(), Cmap, .NONE, cmap_read(), cmap_u16(), Error, Bounds (+23 more)

### Community 194 - "tables/gsub"
Cohesion: 0.04
Nodes (48): ChainedContextLookup, ChainedContextLookup<'a>, Format1, Format2, Format3, ChainedSequenceRule, ChainedSequenceRuleSet, ChainedSequenceRuleSets (+40 more)

### Community 195 - "v2/surface"
Cohesion: 0.07
Nodes (54): gfx_screen_owner(), gfx_policy_scan_changed(), by_sid(), context(), gfx2_cursor_image(), gfx2_cursor_move(), gfx2_cursor_show(), gfx2_dpms() (+46 more)

### Community 196 - "display/hw_load"
Cohesion: 0.08
Nodes (54): enc_clock(), enc_output(), enc_read(), enc_tv_load(), enc_tv_read(), enc_tv_save(), enc_tv_write(), enc_write() (+46 more)

### Community 197 - "tests/ext2_write_batch_host"
Cohesion: 0.09
Nodes (59): block_lba(), cache_slots_invalid(), case_counts(), case_edges(), case_faults(), case_fragment(), case_nospace(), case_reservations() (+51 more)

### Community 198 - "tests/caller_access_host"
Cohesion: 0.05
Nodes (34): ring3_syscall_dispatch(), kcg_module_init(), kcg_wait(), read_ank(), read_glyph(), read_kanji(), rom_ank(), rom_kanji() (+26 more)

### Community 199 - "tests/hostsvc_lgy98_attach_host"
Cohesion: 0.05
Nodes (43): lgy98_attach(), lgy98_init(), lgy98_validate_base(), lgy98_validate_irq(), print_mac(), ne2k_get_stats(), ne2k_ram_bytes(), net_adapter_register() (+35 more)

### Community 200 - "tests/gf_runner_host"
Cohesion: 0.06
Nodes (52): gf_init_index_alias(), gf_op_cr_index_address_latched_run(), gf_op_done_run(), gf_op_io_flag_condition_run(), gf_op_io_restrict_pll2_run(), gf_op_io_restrict_prog_run(), gf_op_pll2_run(), gf_op_reset_run() (+44 more)

### Community 201 - "tests/e82557_drv_host"
Cohesion: 0.08
Nodes (57): abort(), atoi(), boot_image_info(), check_pending(), cpu_delay_us(), deliver_irq(), desc_event(), dma_alloc() (+49 more)

### Community 202 - "tests/test_np21w_ini_live"
Cohesion: 0.06
Nodes (14): bind_model_operation(), dispatch(), Live, absent(), query(), Crt31k, CtlLaunchShapes, Fake (+6 more)

### Community 203 - "tests/boot_splash_native_host"
Cohesion: 0.04
Nodes (36): appslot_get(), bank_sync(), check_caller_write_range(), check_clear(), copy_to_caller(), cpu_delay_us(), gfx2_head_info(), gfx_policy_blocks_map() (+28 more)

### Community 204 - "tests/pcm_cs4231_host"
Cohesion: 0.06
Nodes (55): pcm_reclaim(), case_clock(), case_close(), case_close_dl(), case_cont(), case_drain(), case_guard(), case_obs() (+47 more)

### Community 205 - "tests/gf_command_host"
Cohesion: 0.06
Nodes (46): _start(), test(), test_readback_order(), _start(), alloc_stub(), clock_stub(), clock_us(), dma_alloc() (+38 more)

### Community 206 - "tools/osri_enc2_core"
Cohesion: 0.12
Nodes (54): add_visit(), allocate(), cache_entry(), cache_grow(), cache_save(), candidate_job(), centroid(), channel() (+46 more)

### Community 207 - "tests/serialfs_session_host"
Cohesion: 0.06
Nodes (38): sfs_ms_ticks(), serialfs_session_begin(), serialfs_session_end(), cpu_delay_us(), e82557_get_mac(), e82557_recv(), e82557_rx_ready(), e82557_stat() (+30 more)

### Community 208 - "tools/np21w_ini"
Cohesion: 0.08
Nodes (38): exact_equal(), _atomic(), _changes(), derive(), _directory(), _extra_value(), _hash(), image_name() (+30 more)

### Community 209 - "v2/v1_shim"
Cohesion: 0.05
Nodes (48): lost_caps(), lost_cursor_image(), lost_cursor_move(), lost_cursor_show(), lost_dpms(), lost_edid(), lost_emit(), lost_enum_conn() (+40 more)

### Community 210 - "tests/hwb_shell_host"
Cohesion: 0.08
Nodes (55): bounded_yield(), hwb_test_check(), hwb_test_exit(), hwb_test_write(), hwbf_access(), hwbf_access_result(), hwbf_add_file(), hwbf_add_register() (+47 more)

### Community 211 - "generated/kdebug_kexports"
Cohesion: 0.05
Nodes (48): kdi_get_appslot_reclaim_count(), kdi_get_appslot_trim_done_count(), kdi_get_appslot_trim_done_reject_count(), kdi_get_appslot_trim_epoch(), kdi_get_appslot_trim_mark_count(), kdi_get_appslot_trim_request_count(), kdi_get_cpu_calib_rounds(), kdi_get_cpu_calib_ticks() (+40 more)

### Community 212 - "tests/app_band_pde_host"
Cohesion: 0.05
Nodes (40): paging_app_band_selftest(), selftest_as_end(), check(), die(), kprintf(), ledger_boot_regression(), report(), say_number() (+32 more)

### Community 213 - "tests/test_deploy_protect #3"
Cohesion: 0.06
Nodes (14): find_stale(), hostdrv_root(), main(), prune_hostdrv(), prune_mounted_nhd(), prune_nhd(), show(), wanted_guest_paths() (+6 more)

### Community 214 - "tests/db_caller_host"
Cohesion: 0.05
Nodes (42): copied(), dev_blk_write_lba(), dev_find(), dispatch_probe(), exec_kapi_table(), geforce_fg_due(), host_stat(), kapi_invoke() (+34 more)

### Community 215 - "tools/gen_tests_inventory"
Cohesion: 0.05
Nodes (43): cache_lines(), cpu_signature(), main(), parse_log(), stats(), check(), has_id(), ledger_ids() (+35 more)

### Community 216 - "tests/x11_shared_host"
Cohesion: 0.05
Nodes (30): appslot_get(), check_caller_write_range(), clear_row(), copy_caller_cstr(), copy_to_caller(), draw_char(), draw_wide(), exec_park_poll() (+22 more)

### Community 217 - "tests/hsync_h3_host"
Cohesion: 0.07
Nodes (43): case_a03(), case_a04(), case_a16(), case_dry_run(), case_nosys_and_failure(), case_skip_by_mtime(), case_tally(), case_unknown_mtime() (+35 more)

### Community 218 - "tests/serialfs_host"
Cohesion: 0.07
Nodes (43): serialfs_get_ops(), sfs_client_init(), sfs_trial_ticks(), bad_host_replies(), bootold_rules(), cannot_wait(), dir_name(), f_get() (+35 more)

### Community 219 - "tests/test_rshell_recover"
Cohesion: 0.05
Nodes (34): prune_fixtures(), verify_binfmt(), verify_native(), CacheTests, build(), count(), key(), racing_stat() (+26 more)

### Community 220 - "src/outline"
Cohesion: 0.07
Nodes (34): div_fix(), div_round(), f2dot14_dot(), F2DOT14_ONE, mul_fix(), root4_ceil(), Builder, Collector (+26 more)

### Community 221 - "tables/glyf"
Cohesion: 0.05
Nodes (23): Transform, Builder, Builder<'a>, CompositeGlyphFlags, CompositeGlyphInfo, CompositeGlyphIter, CompositeGlyphIter<'a>, CoordsIter (+15 more)

### Community 222 - "src/launch"
Cohesion: 0.05
Nodes (53): a_failed_launch_also_frees_the_table_only_through_the_poll(), a_failed_launch_carries_the_negative_rc_back(), a_negative_poll_returns_to_the_prompt_instead_of_hanging(), a_short_lived_child_is_done_on_the_first_poll(), again_retries_the_cancel_on_the_next_timer(), Attach, Cancel, Armed (+45 more)

### Community 223 - "tests/appmem_map_host"
Cohesion: 0.09
Nodes (43): all_zero(), appslot_get(), appslot_mark_scheduled(), case_map(), cleanup(), diagnostic_field(), die(), equal() (+35 more)

### Community 224 - "tests/pci_decode_host"
Cohesion: 0.09
Nodes (50): pci_bar_base(), pci_bar_kind(), pci_bar_prefetchable(), pci_cfg_addr(), pci_class_name(), pci_extract16(), pci_extract8(), pci_header_layout() (+42 more)

### Community 225 - "tests/fstat_redir_host"
Cohesion: 0.06
Nodes (46): appslot_shell_commit(), redir_switch_in(), redir_switch_out(), fd_redirect_buf_write_ok(), fd_redirect_clear_state(), fd_redirect_get_buf_len(), fd_redirect_ifmt(), fd_redirect_read() (+38 more)

### Community 226 - "tests/gf_accel_unbound_stubs"
Cohesion: 0.05
Nodes (40): dead(), fini(), init(), inject(), reset(), retained(), _start(), geforce_backend_framebuffer_info() (+32 more)

### Community 227 - "fm/snd_engine"
Cohesion: 0.06
Nodes (46): fm_module_init(), fm_all_off(), fm_note_off(), fm_note_on(), fm_play_mml(), fm_play_mml_kapi(), fm_play_mml_wait(), fm_quiet() (+38 more)

### Community 228 - "tests/gf_accel_b8_host"
Cohesion: 0.08
Nodes (50): gf4_data_bind(), cvt(), gf4_tv_modes(), gf4_tv_state_calc(), is_ctv(), normalize(), quotient(), valid() (+42 more)

### Community 229 - "tests/multiapp_model_host"
Cohesion: 0.16
Nodes (60): case_abort_targets_running(), case_cross_round_bound(), case_cui_nesting(), case_exit_reclaims_one(), case_failed_start_is_clean(), case_fault_isolated(), case_fifth_refused(), case_gui_start_only_from_toplevel() (+52 more)

### Community 230 - "tests/test_fetch_fonts"
Cohesion: 0.07
Nodes (9): FetchFontsTest, FontVendorTest, GuiFontDeployTest, InconsolataTest, JfDotTest, load_module(), PipeIn, sha() (+1 more)

### Community 231 - "tests/fatfs_stat_host"
Cohesion: 0.05
Nodes (44): fatfs_vfs_list(), fatfs_vfs_stat(), fatfs_vfs_sync(), bl_mkdir(), bl_rename(), bl_rm(), bl_sync(), bl_write() (+36 more)

### Community 232 - "tests/gf5_tile_host"
Cohesion: 0.10
Nodes (50): gf_v2_alloc(), gf_v2_release(), gf5_tile_clear(), gf5_tile_find(), gf5_tile_set(), gf5_tiles_init(), gf5_tiles_retire(), tile_apply() (+42 more)

### Community 233 - "tests/h4_manifest_host"
Cohesion: 0.11
Nodes (52): blob_crc(), broken_case(), case_kapi(), case_limits(), case_m10(), case_m5(), case_m6(), case_m7_m8() (+44 more)

### Community 234 - "tests/fd_redirect_d0a_host"
Cohesion: 0.04
Nodes (25): appslot_get(), as_access_page(), as_va_to_pa(), as_va_to_pa_read(), kmemcpy(), main(), map_backing(), paging_current_cr3() (+17 more)

### Community 235 - "tools/lan_bridge"
Cohesion: 0.05
Nodes (15): Bridge, BridgeError, check_macs(), describe(), FakeNicPort, FrameStream, mac_str(), main() (+7 more)

### Community 236 - "tests/evt_test"
Cohesion: 0.10
Nodes (40): evt_active_count(), evt_active_list(), evt_cancel(), evt_is_active(), evt_get_counter(), evt_get_fired(), evt_init(), evt_reset() (+32 more)

### Community 237 - "src/lib #4"
Cohesion: 0.07
Nodes (45): GlyphScratch, RasterScratch, Face, bits(), BITS, Check, check_face(), CHECK_RANGES (+37 more)

### Community 238 - "src/sink"
Cohesion: 0.05
Nodes (39): a_full_screen_wraps_instead_of_freezing(), a_wedged_fixture_model_recovers_into_live_on_the_first_record(), Applied, cursor_outside_the_model_is_ignored_not_clamped(), live_applies_print_clear_and_cursor(), print(), row(), Session (+31 more)

### Community 239 - "system/hsync"
Cohesion: 0.11
Nodes (55): apply_mtime(), buf_equal(), build_temp_path(), clean_temps(), compare_files(), copy_body(), copy_verify(), drop_temp() (+47 more)

### Community 240 - "src/guest"
Cohesion: 0.04
Nodes (35): jis_checks_both_bytes(), two_windows_fit_supported_screens(), valid_jis(), windows(), build_window(), GuestGlyphs, gui_rect(), pixels() (+27 more)

### Community 241 - "tests/multiapp_impl_host"
Cohesion: 0.14
Nodes (53): appslot_live(), appslot_alloc_id(), case_abort_admit(), case_abort_clear(), case_abort_targets_running(), case_cross_round_bound(), case_cui_nesting(), case_cui_only_and_reject_kill() (+45 more)

### Community 242 - "shell/cmd_file"
Cohesion: 0.06
Nodes (41): main(), main(), ls_windows(), os32_ls(), cmd_ls(), cmd_mkdir(), cmd_rmdir(), ls_run() (+33 more)

### Community 243 - "mgxview/mgxview"
Cohesion: 0.08
Nodes (41): build_path(), cache_drop(), cache_evict_farthest(), copy_str(), draw_page(), has_mgx_ext(), lower_ch(), main() (+33 more)

### Community 244 - "tests/rpg_test"
Cohesion: 0.07
Nodes (34): glue_status_tick(), rpg_actor_init(), rpg_find_reborn_def(), rpg_find_status_def(), rpg_init(), rpg_shutdown(), rpg_add_exp(), rpg_alloc_point() (+26 more)

### Community 245 - "np21w_mcp/gui"
Cohesion: 0.07
Nodes (15): c_constants(), resolve(), visit(), c_word_offsets(), Gui, metadata(), read(), read() (+7 more)

### Community 246 - "tools/np21w_ctl"
Cohesion: 0.05
Nodes (23): _ascii_upper(), _b64s(), build_parser(), decode_ini(), Http, lock_script(), main(), media_targets() (+15 more)

### Community 247 - "src/text"
Cohesion: 0.06
Nodes (49): font_draws(), FontDraw, draws(), FULL, kcg_only(), t01_route_keeps_the_kcg_width_rules(), t02_kcg_fallback_draws_cells_and_advances_like_kcg_draw_utf8(), t03_clip_cuts_rows_by_pointer_and_columns_by_mask() (+41 more)

### Community 248 - "shell/rshell"
Cohesion: 0.07
Nodes (50): serial_plan(), serial_tx_budget_ticks(), serial_tx_budget_us(), serial_vfast_div(), compat_exact(), compat_inexact(), fifo_detect(), main() (+42 more)

### Community 249 - "serialfs/serialfs"
Cohesion: 0.08
Nodes (46): serial_gate_set(), serial_is_initialized(), observe_snapshot(), sfs_alive_observe(), sfs_ensure_session(), sfs_persistent_client(), sfs_persistent_enabled(), sfs_persistent_generation() (+38 more)

### Community 250 - "trident/regs"
Cohesion: 0.08
Nodes (39): cursor_pixel(), cursor_supported(), tri_cursor_colors(), tri_cursor_convert(), tri_cursor_image(), tri_cursor_move(), tri_cursor_show(), tri_ddc1_read() (+31 more)

### Community 251 - "serialfs/sfs_proto"
Cohesion: 0.13
Nodes (44): sfs_list_dir(), sfs_call_deadline(), sfs_client_call(), sfs_client_call_deadline(), sfs_client_hello(), sfs_client_hello_deadline(), sfs_client_oneway(), sfs_client_oneway_deadline() (+36 more)

### Community 252 - "kdebug/gf_accel_test"
Cohesion: 0.12
Nodes (47): bytes_transfer(), clamp8(), compare(), compare_impl(), compare_transfer(), copy_case(), cpp(), deadline() (+39 more)

### Community 253 - "tools/np21w_ctl #2"
Cohesion: 0.09
Nodes (17): check_name(), Ctl, done_line(), entry(), reflected(), reflected(), slot(), alive_now() (+9 more)

### Community 254 - "tests/ext2_read_cache_host"
Cohesion: 0.12
Nodes (51): block_lba(), bytes_check(), cache_slots_invalid(), case_boundaries(), case_contexts(), case_double(), case_full_blocks(), case_lifetime() (+43 more)

### Community 255 - "tests/hsync_h1_host"
Cohesion: 0.13
Nodes (49): build_deep(), case_a01_a02(), case_a05(), case_a06(), case_a07(), case_a08(), case_a09(), case_a10() (+41 more)

### Community 256 - "tests/trim_back_rs_host"
Cohesion: 0.06
Nodes (44): AFTER, APP_ID_MAX, APP_ID_MIN, APP_STATE_PARKED, append(), AppVTable, CACHE, call() (+36 more)

### Community 257 - "cmds/cfg"
Cohesion: 0.10
Nodes (51): c_args(), cfg_cmd_parse(), close_note(), collect_key(), collect_scope(), do_export(), do_get(), do_import() (+43 more)

### Community 258 - "src/lib #5"
Cohesion: 0.06
Nodes (42): ank(), ank_bits(), CELL_HEIGHT, CELL_WIDTH, checked_axis(), draw(), Error, CoordinateRange (+34 more)

### Community 259 - "tests/test_np21w_trial"
Cohesion: 0.06
Nodes (14): identity_mismatch(), launch_command(), make_plan(), transform_trial(), bound(), fixture(), plan(), TrialTests (+6 more)

### Community 260 - "tests/gf1_irq_host"
Cohesion: 0.07
Nodes (43): clean(), geforce_tick_bind(), gf_engine_stop(), gf_inst_free(), gf_instmem_init(), gf_irq_attach(), gf_irq_detach(), gf_irq_enable() (+35 more)

### Community 261 - "src/bundle"
Cohesion: 0.05
Nodes (42): BITS_PER_BLOCK_BYTES, Bundle<S>, byte_at(), CRC32_POLY, crc32_update(), Fetch, Err, Hit (+34 more)

### Community 262 - "tests/engine"
Cohesion: 0.08
Nodes (44): arena(), borrowed_bitmap_blocks_release_paths(), cell_rows(), cells_draw_like_uncached(), Disk, disk(), draw(), entry_check_once_and_stale() (+36 more)

### Community 263 - "sqlite3/sqlite3 #2"
Cohesion: 0.09
Nodes (48): allConstraintsUsed(), createMask(), decodeIntArray(), estLog(), exprSelectUsage(), indexMightHelpWithOrderBy(), isLimitTerm(), sqlite3LogEst() (+40 more)

### Community 264 - "tests/test_kapi_layout"
Cohesion: 0.06
Nodes (34): _copy_real_dir(), _listed_files(), overlay(), _submodule_dirs(), command(), main(), run(), foreground_case() (+26 more)

### Community 265 - "powermac/ofw"
Cohesion: 0.11
Nodes (45): ppc_ptr_cell(), kstrncmp(), powermac_boot(), channel(), named(), pm_escc_prepare(), be32(), cells() (+37 more)

### Community 266 - "tests/v86_low_host"
Cohesion: 0.07
Nodes (33): paging_v86_session_open(), paging_lowmap_busy(), before(), bios_call(), bios_disk_cases(), bios_ok(), bios_pair(), boundary() (+25 more)

### Community 267 - "tests/outline"
Cohesion: 0.09
Nodes (35): bbox(), byte_limits(), Command, Close, Line, Move, Quad, commands() (+27 more)

### Community 268 - "tests/con_sink_host"
Cohesion: 0.10
Nodes (40): con_sink_read(), bootlog_push(), capture_init(), case_boot_serial(), case_console_render_gate(), case_disabled_is_silent(), case_discard_on_cui(), case_exit_record() (+32 more)

### Community 269 - "src/fix16"
Cohesion: 0.05
Nodes (28): Fix16, fix16_abs(), fix16_ceil(), fix16_clamp(), fix16_div(), fix16_floor(), fix16_from_frac(), .HALF (+20 more)

### Community 270 - "display/modes"
Cohesion: 0.10
Nodes (44): candidate_pll(), connector_timings(), detect_block(), gf4_detect(), gf4_modes(), gf4_surface_check(), gf_display_framebuffer(), gf_display_mode_info() (+36 more)

### Community 271 - "tests/splash_wire_host"
Cohesion: 0.06
Nodes (34): tick_dispatch(), add(), mark(), now(), tick_probe_begin(), tick_probe_boot(), tick_probe_control(), tick_probe_end() (+26 more)

### Community 272 - "emu_agent/playbook"
Cohesion: 0.07
Nodes (21): _bind(), InputFailure, _live_factory(), LiveDriver, checked_file(), checked_get(), checked_post(), _observation() (+13 more)

### Community 273 - "tests/ext2_part_host"
Cohesion: 0.10
Nodes (47): ext2_format(), ext2_format_at(), atou(), bootinfo_part_geom(), case_find_bios(), case_find_fail(), case_format_at_16652(), case_format_at_refuse() (+39 more)

### Community 274 - "tests/ai_test"
Cohesion: 0.11
Nodes (35): load_ai_profiles(), ai_get_param(), ai_init(), ai_load_profile(), ai_profile_count(), ai_save_profile(), ai_set_param(), ai_shutdown() (+27 more)

### Community 275 - "tests/trident_lifecycle_host"
Cohesion: 0.06
Nodes (33): trident_dpms_set(), accel_init(), disable(), failed(), mode_enable(), restore_lock(), return98(), trident_close() (+25 more)

### Community 276 - "fs/vfs"
Cohesion: 0.09
Nodes (46): persist(), vfs_open_internal(), vfs_open_probe(), vfs_sqlite_backend(), vfs_sqlite_writable(), vfs_chdir(), vfs_check_dot_prefix(), vfs_dev_mount_count() (+38 more)

### Community 277 - "tests/gui_reattach_host"
Cohesion: 0.06
Nodes (33): CHECKS, Damage, DETACHED, DETACHES, FLUSH_RC, FLUSHED_FILLS, gfx_direct, gfx_fb (+25 more)

### Community 278 - "tests/gf_production_host"
Cohesion: 0.08
Nodes (40): gf_production_configure(), gf_production_identify(), gf_foundation_start(), gf_foundation_stop(), gf_production_clock(), gf_production_mmio(), gf_production_wait(), kmemset() (+32 more)

### Community 279 - "sdk/gen_kapi"
Cohesion: 0.06
Nodes (27): _arg_decl_map(), check_all_out(), get_arg_names(), guarded_args(), input_errors(), input_guard_c(), _is_out_ptr(), layout_errors() (+19 more)

### Community 280 - "tools/guest_tests"
Cohesion: 0.06
Nodes (23): aggregate(), Block, build_script(), classify(), Entry, file_size(), format_report(), hostdrv_verdict() (+15 more)

### Community 281 - "tests/test_host_agent"
Cohesion: 0.05
Nodes (28): n2_b2_decl_range(), n2_b5_clip_none_503(), n2_b6_put_501(), n2_new1_wdata_after_400_acks_only(), n2_open_kind_and_name(), n2_unknown_id_409(), n3_file_dotdot_escape_403(), n3_file_no_root_403() (+20 more)

### Community 282 - "tests/disk_auth_host"
Cohesion: 0.06
Nodes (24): appslot_get(), caller_copy_tests(), dev_blk_write_lba(), dev_find(), kstrlen(), kstrncpy(), platform_ext2_format(), platform_ext2_format_at() (+16 more)

### Community 283 - "tests/hostsvc_boot_host"
Cohesion: 0.05
Nodes (26): lan_module_init(), boot_common_get(), boot_run(), bootinfo_get(), hw_free(), hw_mac(), hw_recv(), hw_send() (+18 more)

### Community 284 - "tests/fep_user_store_host"
Cohesion: 0.08
Nodes (38): fep_test_done(), fep_test_next(), fep_test_reset(), fep_test_resident_finish(), fep_test_resident_take(), fep_test_script(), fep_test_settings(), next() (+30 more)

### Community 285 - "src/app"
Cohesion: 0.09
Nodes (16): dispatch(), drain_pending_focus(), emit(), flush_damage(), FOCUS_DRAIN_MAX, MOUSE_BTN_LEFT, paint_damaged(), run_vt() (+8 more)

### Community 286 - "tests/gf4_c2linkb_host"
Cohesion: 0.10
Nodes (42): gf4_exec_table(), gf4_links_table(), gf4_tmds_script_select(), links_admission(), links_bind_head(), links_diagnostic_publish(), links_diagnostic_restore(), links_diagnostic_save() (+34 more)

### Community 287 - "display/tv"
Cohesion: 0.12
Nodes (42): gf4_analog_dpms(), gf4_analog_load(), gf4_dac_set(), gf4_route_check(), output_offset(), route_equal(), tx_encoder(), gf4_tv_dpms() (+34 more)

### Community 288 - "tests/test_np21w_ini_live #2"
Cohesion: 0.05
Nodes (13): bootstrap_argv(), checked_snapshot(), failure_detail(), identify(), launch_arguments(), launch_of(), PowerShellTransport, receipt_size_bound() (+5 more)

### Community 289 - "tests/gf_command_c0_host"
Cohesion: 0.11
Nodes (45): _start(), c0_clear_trace(), c0_clock(), c0_done(), c0_engine_stop_failure(), c0_expect(), c0_expect_cleanup(), c0_expect_gates() (+37 more)

### Community 290 - "zlib/zlib"
Cohesion: 0.04
Nodes (42): alloc_func, free_func, gz_header, gz_header_s, comm_max, comment, done, extra (+34 more)

### Community 291 - "e82557/e82557"
Cohesion: 0.11
Nodes (44): bm_restore(), cb_run(), e82557_get_mac(), e82557_irq(), e82557_probe(), e82557_recv(), e82557_rx_free(), e82557_send() (+36 more)

### Community 292 - "trident/backend_trident"
Cohesion: 0.11
Nodes (43): accel_counters(), build_modes(), clip_pair(), cursor_origin(), cursor_result(), drawable(), enter(), finish() (+35 more)

### Community 293 - "display/cursor"
Cohesion: 0.12
Nodes (43): cursor_context(), cursor_cr(), cursor_deadline(), cursor_fix(), cursor_head(), cursor_input(), cursor_offset(), cursor_position() (+35 more)

### Community 294 - "text/lconsole"
Cohesion: 0.08
Nodes (37): su_redraw_filer(), su_adjust_scroll(), su_draw_text(), su_draw_funcbar(), su_draw_help(), su_draw_statusbar(), su_prompt(), su_redraw_screen() (+29 more)

### Community 295 - "tests/ring3_guard_host"
Cohesion: 0.09
Nodes (38): ring3_call_from_user(), ring3_guard_active(), wrap_gui_register(), gui_register(), app_handler_b(), case_gui_call(), case_ime_render(), case_owner_exit() (+30 more)

### Community 296 - "tools/gui_gate"
Cohesion: 0.08
Nodes (27): back_to_cui(), begin_gui(), close_rshell(), one(), cmd(), _cmd_raw(), enter_gshell(), get() (+19 more)

### Community 297 - "tests/test_np21w_ini_live #3"
Cohesion: 0.05
Nodes (9): file_key(), main(), WindowsExecutor, FailureDiagnosis, ReviewRetryRaces, Transport, WindowsContract, factory() (+1 more)

### Community 298 - "src/guest #2"
Cohesion: 0.10
Nodes (12): DisplayApp, Paste, message(), message_rc(), Mode, Attached, Prompt, Msg (+4 more)

### Community 299 - "tests/gf4_c2save_host"
Cohesion: 0.13
Nodes (43): gf4_diagnostic_abandon(), gf4_diagnostic_restore(), gf4_diagnostic_save(), gf4_display_restore(), gf4_display_restore_stage(), gf4_display_save(), gf4_display_saved_release(), save_check() (+35 more)

### Community 300 - "tests/console_dual_host"
Cohesion: 0.06
Nodes (23): a1(), a3_a4(), bootlog_push(), con_backend_clear(), con_backend_color(), con_backend_putchar(), con_sink_push_print(), gfx2_adapter_get() (+15 more)

### Community 301 - "tests/test_h3_park_resume #2"
Cohesion: 0.06
Nodes (24): _buffer(), Client, _fill(), key_sequence(), main(), read_buffered(), read_eot_keep_rest(), _read_line() (+16 more)

### Community 302 - "tools/img2mgx"
Cohesion: 0.06
Nodes (26): _bayer8(), choose_bpp(), cluster_matrix(), compress_payload(), decode(), dither_matrix(), dp_best_levels(), encode() (+18 more)

### Community 303 - "tests/gf5_perf_host"
Cohesion: 0.11
Nodes (41): gf4_host_init(), gf5_host_init(), dense_entries(), expect_get(), fresh(), heat_at(), heat_failures(), heat_fresh() (+33 more)

### Community 304 - "tests/hostsvc_media_host"
Cohesion: 0.08
Nodes (32): dev_media_check(), dev_unregister(), iso9660_media_drop(), iso9660_resource_check(), iso9660_resource_refresh(), media_drop_caches(), media_read(), vfs_fd_stale_mount() (+24 more)

### Community 305 - "src/lib #6"
Cohesion: 0.11
Nodes (17): BOGUS_DIALOG, clamp(), fmt2(), fmt_num(), ICON_PIXELS, LAUNCH_PATH, main(), MODE_FULL (+9 more)

### Community 306 - "tests/gfx2_i3_guest_host"
Cohesion: 0.07
Nodes (31): blank(), caps(), conn(), contains(), count(), create(), desc(), flip() (+23 more)

### Community 307 - "tests/test_rshell_peek"
Cohesion: 0.07
Nodes (23): case_lan(), case_parse(), case_reject(), case_run_address(), case_serial_glue(), case_symbol(), check(), dump_packet() (+15 more)

### Community 308 - "src/aat"
Cohesion: 0.05
Nodes (21): BinarySearchTable<'a, T>, BinarySearchValue, ExtendedStateTable<'a, T>, GenericStateEntry<T>, .SIZE, Lookup<'a>, LookupInner, LookupInner<'a> (+13 more)

### Community 309 - "tests/h4_backing_host"
Cohesion: 0.09
Nodes (37): dma_pool_bad_free(), dma_pool_free_pages(), dma_pool_init(), dma_pool_leaked(), lan_module_load(), pcm_module_load(), selftest(), check() (+29 more)

### Community 310 - "tests/gui_trim_host"
Cohesion: 0.06
Nodes (30): AppVTable, cache_hook(), call(), CALLS, check(), commit(), dispatch(), drain_pending_focus() (+22 more)

### Community 311 - "tests/shlib_high_host"
Cohesion: 0.06
Nodes (24): die(), report(), die(), report(), die(), kprintf(), pgalloc_alloc_phys(), pgalloc_alloc_pt() (+16 more)

### Community 312 - "tests/test_host_agent #2"
Cohesion: 0.07
Nodes (23): FakeOS32, h5_hello_list_and_notify(), h5_mutations(), hello_paylen_per_stage(), hs_syn_candidates(), main(), n2_b4_two_agents_unique_files(), n2fix_b_clip_arg_validation() (+15 more)

### Community 313 - "tests/test_host_agent #3"
Cohesion: 0.07
Nodes (25): a11_named_body_roundtrip(), h5_linux_print_lifecycle(), h5_named_services_and_linux_clip(), n2_b1_wdata_after_done_acks_no_dup(), n2_b2_close_after_data_complete_no_release(), n2_b2_close_while_data_incomplete_409(), n2_b2_second_data_409(), n2_b3_body_on_non_200() (+17 more)

### Community 314 - "tables/kern"
Cohesion: 0.05
Nodes (25): ExtendedStateTable<'_, T>, StateTable, AATCoverage, .SIZE, Format, Format0, Format1, Format2 (+17 more)

### Community 315 - "tests/gf_wc_host"
Cohesion: 0.06
Nodes (22): fake_colors(), fake_mode(), fake_palette(), fake_present(), fake_shadow(), fake_start(), gf4_get_probe_decls(), gf_wc_cpuid() (+14 more)

### Community 316 - "src/host #2"
Cohesion: 0.06
Nodes (41): AGAIN, DECL_MAX, FULL, host_call(), host_clip_get(), HOST_CLIP_MAX, HOST_EABORT, HOST_EINVAL (+33 more)

### Community 317 - "tools/rshell_peek"
Cohesion: 0.07
Nodes (23): add_arguments(), check_identity(), collect_frames(), feed(), dump_frame(), DumpCollector, elf_cstring(), _lan_modules() (+15 more)

### Community 318 - "tests/surface_bundle_host"
Cohesion: 0.06
Nodes (20): lease_release(), lease_check(), caller_copy_tests(), get_bundle(), gfx_sf_backend(), hash_tables(), host_bundle_unmap(), host_lease_copyin() (+12 more)

### Community 319 - "tests/test_db_errstr"
Cohesion: 0.07
Nodes (26): build(), prepare(), probe_source(), build(), build_api(), check_no_block_size(), check_no_block_size_quiet(), check_reclaim_order() (+18 more)

### Community 320 - "tests/gf_display_host"
Cohesion: 0.09
Nodes (31): gf_display_modeset(), gfinit_mode_calc(), calc_equivalence(), pll_failure_cases(), record_cases(), row_comparison(), service_cases(), sim_clock() (+23 more)

### Community 321 - "tests/hdd_stage1_host"
Cohesion: 0.12
Nodes (36): ide_addr_make(), ide_addr_mode(), ide_addr_range_ok(), geom_ok(), pc98pt_chs_to_lba(), pc98pt_count_used(), pc98pt_entry_empty(), pc98pt_entry_range() (+28 more)

### Community 322 - "tools/uimock"
Cohesion: 0.10
Nodes (21): clip_text(), cursor(), gauge(), assets(), branch_window(), draw_board(), hp_bar_color(), plan_a() (+13 more)

### Community 323 - "src/gstate"
Cohesion: 0.08
Nodes (29): ScreenInfo, .ZERO, clear_base_clip(), current_clip(), pop_clip(), push_clip(), set_base_clip(), screen_info() (+21 more)

### Community 324 - "tests/fep_boundary_host"
Cohesion: 0.09
Nodes (36): wrap_ime_user_delete(), wrap_ime_user_export(), wrap_ime_user_list(), ime_user_delete_facade(), ime_user_export_facade(), ime_user_list_facade(), bad_output(), bind_int() (+28 more)

### Community 325 - "tests/gf_command_t2b_host"
Cohesion: 0.11
Nodes (37): clock_now(), command_call(), display_crc(), emit(), gf_command_test(), heads(), measure(), number() (+29 more)

### Community 326 - "tests/recover_host"
Cohesion: 0.12
Nodes (37): recover_request, REC_REQUEST_NOW, REC_REQUEST_STATUS, trial_step(), abort_requested(), begin(), bound_begin(), bus_fault() (+29 more)

### Community 327 - "tests/test_lanfs"
Cohesion: 0.07
Nodes (17): crc32(), encode(), _err(), Frame, Server, _take_path(), try_frame(), exercise() (+9 more)

### Community 328 - "tests/test_np21w_ctl #2"
Cohesion: 0.08
Nodes (6): Proc, FakeOps, LockWait, StartProcesses, Status, Stop

### Community 329 - "tests/fs_kind_fake"
Cohesion: 0.07
Nodes (29): data_is(), main(), mark_unknown(), no_side_effects(), os32_ls(), refused(), run(), shell_register_cmds() (+21 more)

### Community 330 - "tests/kpeek_host"
Cohesion: 0.14
Nodes (43): kpeek_call(), call(), data_eq(), data_is(), data_untouched(), e82557_get_mac(), e82557_send(), finish() (+35 more)

### Community 331 - "ppc/kboot_cpu"
Cohesion: 0.07
Nodes (27): build(), lan_host(), main(), observe(), host(), build(), main(), observe() (+19 more)

### Community 332 - "ppc/mmu_fake"
Cohesion: 0.08
Nodes (36): ppc_cpu_setup(), ppc_dec_count(), ppc_dec_init(), ppc_dec_interrupt(), powermac_boot(), cell_at(), event(), exit_code() (+28 more)

### Community 333 - "tests/e82557_eeprom_host"
Cohesion: 0.10
Nodes (38): ee_mac_read(), e82557_ee_frame(), e82557_ee_mac(), e82557_ee_read_word(), e82557_io_window(), e82557_mac_form(), e82557_window_name(), ee_width_ok() (+30 more)

### Community 334 - "kdebug/gf_display_test"
Cohesion: 0.19
Nodes (40): display_admit(), display_begin(), display_cui(), display_cui_cleanup(), display_cui_restore(), display_cursor_step(), display_deadline(), display_decode() (+32 more)

### Community 335 - "kdebug/recover_profile"
Cohesion: 0.07
Nodes (30): recovery_mode_cell(), recovery_screen_verify(), cell_access(), recover_budget_apply(), recover_cell_98(), recover_gf_profile(), recover_hal_wait_frame(), recover_indexed_cleanup() (+22 more)

### Community 336 - "tests/nano_adapter_host"
Cohesion: 0.12
Nodes (40): _calloc_r(), malloc(), _malloc_r(), os32_gui_trim_serve(), os32_nano_morecore(), os32_nano_sbrk(), os32_nano_select(), realloc() (+32 more)

### Community 337 - "tools/check_kprintf_window"
Cohesion: 0.09
Nodes (18): audit(), c_files(), Call, calls_in(), count_arguments(), delimiter_pairs(), literal_format(), main() (+10 more)

### Community 338 - "tests/test_np21w_gui"
Cohesion: 0.07
Nodes (14): post(), decode_ring(), GuiTests, inject(), observe(), read(), emit_marker(), in_wait() (+6 more)

### Community 339 - "host/doc_tests"
Cohesion: 0.11
Nodes (42): a_file_without_a_trailing_newline_gets_one(), a_logical_line_is_covered_exactly_once_by_its_visual_rows(), a_real_blank_last_line_survives(), caret_row_is_inside_the_view_after_scrolling(), caret_stays_on_a_utf8_boundary_when_moving_between_lines(), caret_vrow_follows_the_wrap(), CLOSED, delete_at_end_of_line_joins_the_next() (+34 more)

### Community 340 - "system/cdinst"
Cohesion: 0.10
Nodes (30): install_package_hd(), ensure_parent_dirs(), lzss_decode_local(), pkg_extract(), pkg_first_overflow(), pkg_get_name(), pkg_parse(), boot_img_free() (+22 more)

### Community 341 - "src/lib #7"
Cohesion: 0.10
Nodes (43): reset(), set_get_int(), set_get_text(), set_get_text_rc(), set_set_rc(), ERR_INVAL, ERR_ISDIR, ERR_NOTFOUND (+35 more)

### Community 342 - "cs4231/pcm_cs4231"
Cohesion: 0.12
Nodes (38): dma_chan_ack_tc(), pcm_module_init(), cs_read(), cs_write(), cs_write_locked(), pcm_advance(), pcm_advance_restart(), pcm_advance_run() (+30 more)

### Community 343 - "geforce/gf_v2_weak"
Cohesion: 0.08
Nodes (29): gf3_copy_emit(), gf3_fill_emit(), gf3_rop3_set(), gf3_scale_emit(), gf_v2_cursor_image(), gf_v2_cursor_move(), gf_v2_cursor_show(), gf_v2_deadline() (+21 more)

### Community 344 - "tests/shell_service_host"
Cohesion: 0.11
Nodes (29): appslot_get(), caller_access_leave(), caller_access_save(), case_gate(), case_guard(), case_publish(), case_reload(), case_wire() (+21 more)

### Community 345 - "tests/test_emu_playbook"
Cohesion: 0.09
Nodes (12): _freeze(), main(), Runner, _thaw(), validate_plan(), CLITests, ExtraBoundaryTests, FinalBoundaryTests (+4 more)

### Community 346 - "tests/backend_geforce_host"
Cohesion: 0.06
Nodes (24): close_display(), fake_mode(), fake_pal(), fake_palette_get_all(), fake_palette_shadow_set(), fake_present(), fake_start(), fake_stop() (+16 more)

### Community 347 - "tests/gf_v2_ops_host"
Cohesion: 0.08
Nodes (31): fill(), packet(), setup(), _start(), clear(), copy(), no_submit(), observe() (+23 more)

### Community 348 - "shell/cmd_sys"
Cohesion: 0.07
Nodes (31): config_failures_and_limits(), die(), emit(), open_file(), print(), read_file(), sh_refuse(), _start() (+23 more)

### Community 349 - "tests/test_host_agent #4"
Cohesion: 0.07
Nodes (19): FakeProc, FakeSpawn, n2_b1_async_processing_and_other_rid(), n2_b1_deadline_503(), n2_b1_release_discards_child(), n2_b5_clip_wsl_empty_200(), n2_b5_clip_wsl_get_base64(), n2_b5_clip_wsl_non_base64_503() (+11 more)

### Community 350 - "tests/dep_dir_d3_boot_host"
Cohesion: 0.08
Nodes (33): dev_find(), dev_init(), dev_register_hdd(), dma8237_init(), _enable(), event(), ext2_init(), fake_tvram() (+25 more)

### Community 351 - "tests/kcallback_host"
Cohesion: 0.08
Nodes (31): ext2_to_vfs_cb(), wrap_sys_ls(), callback(), caller_access_get(), caller_access_get_user(), check_caller_write_range(), client_callback(), client_window() (+23 more)

### Community 352 - "src/cfgro"
Cohesion: 0.07
Nodes (35): w01_fold_get_int_is_def_on_any_failure(), w03_fold_set_prefers_earlier_failure(), w04_scope_is_app_table(), w05_copy_cstr_and_copy_value(), w30_raw_span_ok_table(), w32_set_text_null_value_with_nonzero_len_is_rejected(), w33_set_text_null_value_with_zero_len_is_the_empty_value(), w34_huge_len_is_rejected_before_slicing() (+27 more)

### Community 353 - "src/fake"
Cohesion: 0.09
Nodes (39): Call, Begin, Close, Commit, GetInt, GetText, Open, Rollback (+31 more)

### Community 354 - "tests/gfinit_bus_host"
Cohesion: 0.10
Nodes (38): pci_barsize_reset(), accessor_tests(), address(), bus_read_test(), bus_setup(), bus_write_test(), cr1f_two_visit_test(), GF_S5_ENTRY() (+30 more)

### Community 355 - "v2/a0_weak"
Cohesion: 0.05
Nodes (5): gfx2_caps(), gfx2_caps_from_v1(), gfx2_fence_query(), gfx2_mode_set(), paging_gfx2_map()

### Community 356 - "v2/a0_weak #2"
Cohesion: 0.05
Nodes (43): gfx2_adapter_gen(), gfx2_adapter_get(), gfx2_cpu_begin(), gfx2_cpu_end(), gfx2_cursor_image(), gfx2_cursor_show(), gfx2_div255(), gfx2_dpms() (+35 more)

### Community 357 - "edit/macro_engine"
Cohesion: 0.10
Nodes (35): action_backspace(), action_ctrl_a(), action_ctrl_b(), action_ctrl_c(), action_ctrl_e(), action_ctrl_f(), action_ctrl_r(), action_ctrl_u() (+27 more)

### Community 358 - "tests/ext2_mini_host"
Cohesion: 0.12
Nodes (29): boot_main(), hex32(), hex8(), show_dump(), show_val(), boot_main(), bootinfo_set_image(), find_partition() (+21 more)

### Community 359 - "isofs/iso9660"
Cohesion: 0.11
Nodes (27): iso9660_block_size_fn(), iso9660_free_blocks(), iso9660_get_file_size(), iso9660_list_dir(), iso9660_read_file(), iso9660_read_stream(), iso9660_stat(), iso9660_total_blocks() (+19 more)

### Community 360 - "crt/syscalls"
Cohesion: 0.09
Nodes (25): _close(), _fstat(), _lseek(), _open(), os32_errno(), os32_ret(), _read(), _sbrk() (+17 more)

### Community 361 - "gf/therm"
Cohesion: 0.21
Nodes (38): geforce_heat_clear(), gf5_fan_check_cstate(), gf5_fan_cstate(), gf5_fan_get(), gf5_fan_mode_set(), gf5_fan_tick(), gf5_therm_monitor_restart(), gf5_therm_read() (+30 more)

### Community 362 - "tests/exec_selftest_host"
Cohesion: 0.06
Nodes (17): fd_redirect_clear_state(), gf_display_owner_exit(), same_slots(), _start(), take_check(), test(), host_flags_set(), host_init() (+9 more)

### Community 363 - "tests/font_guard_host"
Cohesion: 0.06
Nodes (22): appslot_get(), appslot_park_check(), appslot_park_commit(), appslot_park_kbd_check(), appslot_park_kbd_commit(), appslot_park_poll_check(), callback(), die() (+14 more)

### Community 364 - "ime/fep_module"
Cohesion: 0.07
Nodes (24): fep_services_boot_fault(), ime_boot_bytes(), ime_boot_connections(), ime_dict_info_facade(), ime_dict_suspend(), ime_feed_key(), ime_get_mode(), ime_getchar() (+16 more)

### Community 365 - "tests/test_deploy_protect #4"
Cohesion: 0.07
Nodes (6): check_tree(), do_clean(), Review3TreeSymlink, Review4CleanRootStat, Review5LostFound, ReviewB3Clean

### Community 366 - "ppc/lan_p24_fake"
Cohesion: 0.10
Nodes (31): be32(), decimal(), die(), inject(), kmemcpy(), kmemset(), kprintf(), log_head() (+23 more)

### Community 367 - "tests/host_lib_host"
Cohesion: 0.18
Nodes (39): host_clip_get(), host_clip_put(), host_get(), host_print_stream(), host_time(), case_clip_get_503(), case_clip_put_empty(), case_clip_put_ok() (+31 more)

### Community 368 - "tests/test_ci_stab"
Cohesion: 0.06
Nodes (5): InfrastructureTests, run(), one(), mutation_control_skips(), mutation_test_sources()

### Community 369 - "host/launch_tests"
Cohesion: 0.07
Nodes (30): accepted_launch_allows_new_folder_dialog(), accepted_launch_allows_relaunch_without_quit(), accepted_launch_shows_basename_and_bounds_long_status(), app(), Filer, GuiErr, launch_errors_remain_visible_and_allow_retry(), LAUNCH_RC (+22 more)

### Community 370 - "zlib/inflate"
Cohesion: 0.07
Nodes (20): adler32(), adler32_combine_(), adler32_combine64(), adler32_combine(), adler32_z(), fixedtables(), inflateInit_(), inflateInit2_() (+12 more)

### Community 371 - "tests/loop_dev_host"
Cohesion: 0.13
Nodes (29): loop_dev_attach_fd(), compare_d88(), d88_sector(), dd_alloc(), dd_open(), dd_read(), dd_write(), dev_register() (+21 more)

### Community 372 - "powermac/lan_service"
Cohesion: 0.10
Nodes (28): gem_mac_enable_step(), gem_mac_init(), beacon(), copy(), logs(), off(), pm_lan_poll(), pm_lan_start() (+20 more)

### Community 373 - "tests/splash_v2_host"
Cohesion: 0.07
Nodes (23): pc98_splash_raster_diag(), _start(), advance(), faults(), finish(), fixed_palette(), make_logo(), ports() (+15 more)

### Community 374 - "econ/econ_estate"
Cohesion: 0.17
Nodes (35): restore_estates(), snapshot_estates(), econ__count_routes_for_stage(), econ_estate_accumulate(), econ_estate_at(), econ_estate_claim(), econ_estate_clear_co_owner(), econ_estate_clear_flag() (+27 more)

### Community 375 - "fatfs/diskio"
Cohesion: 0.11
Nodes (33): disk_initialize(), disk_ioctl(), disk_read(), disk_status(), disk_write(), diskio_set_hdd_partition(), DRESULT, DSTATUS (+25 more)

### Community 376 - "shell/hwb_exec"
Cohesion: 0.13
Nodes (36): hw_access(), hw_find_id(), hw_initial(), hw_line(), hw_log_append(), hw_log_error(), hw_log_flush(), hw_point() (+28 more)

### Community 377 - "ppc/pmap"
Cohesion: 0.10
Nodes (23): ppc_exception(), ppc_current_context(), ppc_pmap_bootstrap(), ppc_pmap_enter(), ppc_pmap_remove(), ppc_select_context(), pte_insert(), pte_set() (+15 more)

### Community 379 - "geforce/gf_v2_gart"
Cohesion: 0.12
Nodes (26): gf_v2_gart_bind(), gf_v2_gart_range(), gf_v2_gart_start(), gf_v2_gart_unbind(), live(), pte_write(), resume(), gf2_dma_new() (+18 more)

### Community 380 - "hostdrv/hostdrvfs"
Cohesion: 0.14
Nodes (31): hdrv_block_size(), hdrv_delete(), hdrv_free_blocks(), hdrv_get_file_size(), hdrv_list_dir(), hdrv_list_step(), hdrv_mkdir(), hdrv_read_file() (+23 more)

### Community 381 - "tests/reader"
Cohesion: 0.17
Nodes (33): cmap12(), cmap4(), cmap_resident_limit_boundary(), compare(), Disk, disk(), exact_and_exceeded_limits(), expect_resident() (+25 more)

### Community 382 - "gf/feature5_links"
Cohesion: 0.15
Nodes (31): gf5_link_admission(), gf5_link_alloc(), gf5_link_clock64(), gf5_link_copy_extent(), gf5_link_display_restore(), gf5_link_display_save(), gf5_link_drain(), gf5_link_drain_pause() (+23 more)

### Community 383 - "tests/probe_host"
Cohesion: 0.11
Nodes (26): probe_call(), add(), command(), exec_hwb_abort_take(), fake_load(), fake_store(), hit(), kmemset() (+18 more)

### Community 384 - "tests/trim_focus_host"
Cohesion: 0.06
Nodes (25): Api, api(), Cache, CACHE, Error, FOCUS_ERROR, LOG, kprint! (+17 more)

### Community 385 - "tools/doc_graph"
Cohesion: 0.09
Nodes (24): archive_target(), Change, delink(), render(), in_archive(), main(), replacement(), analyze() (+16 more)

### Community 386 - "tools/mkfat12"
Cohesion: 0.10
Nodes (11): Floppy, fat12_get(), fat12_set(), Fat12Builder, main(), make_bpb(), make_dir_entry(), name_to_83() (+3 more)

### Community 387 - "tests/test_deploy_protect #5"
Cohesion: 0.09
Nodes (10): do_deploy(), file_sha256(), stamp_path(), verify_pull_stamp(), write_pull_stamp(), boom(), Review2StampWrite, ReviewB8PullStamp (+2 more)

### Community 388 - "tests/lanfs_host"
Cohesion: 0.07
Nodes (28): clock_now(), clock_us(), net_mac(), net_rx_reset(), net_rx_take(), net_tx(), sfs_ensure_session(), sfs_persistent_client() (+20 more)

### Community 389 - "shell/cmd_filer"
Cohesion: 0.12
Nodes (32): fldraw_all(), fldraw_clear_line(), fldraw_cursor_update(), fldraw_entry(), fldraw_entry_at(), fldraw_footer(), fldraw_header(), fldraw_help() (+24 more)

### Community 390 - "tables/kerx"
Cohesion: 0.06
Nodes (18): AnchorPoints, Coverage, Format, Format0, Format1, Format2, Format4, Format6 (+10 more)

### Community 391 - "ppc/kboot_cpu_fixture"
Cohesion: 0.12
Nodes (30): ppc_dcbf_line(), ppc_dcbi_line(), ppc_dcbst_line(), ppc_dma_sync(), ppc_sync(), ppc_kernel_real_start(), ppc_cpu_disable_bats(), ppc_cpu_identify() (+22 more)

### Community 392 - "tests/exec_budget_host"
Cohesion: 0.08
Nodes (26): exec_shell_layout(), exec_heap_free(), appslot_at(), budget_admit(), budget_kmalloc(), digest(), exec_disk_write_path_allowed(), exec_heap_save_state() (+18 more)

### Community 393 - "tests/gf5_state_host"
Cohesion: 0.19
Nodes (35): gf5_state_boot_target(), gf5_state_free(), gf5_state_restore(), gf5_state_save(), state_cancel(), state_deadline(), state_fault(), state_links() (+27 more)

### Community 394 - "src/host_fake"
Cohesion: 0.10
Nodes (36): Call, ClipGet, ClipPut, FileClose, FileOpen, FileRead, Get, PrintStream (+28 more)

### Community 395 - "tests/test_gui_gate"
Cohesion: 0.08
Nodes (22): table(), check(), run(), take(), case_begin(), case_begin_cirrus(), case_close(), case_fresh() (+14 more)

### Community 396 - "tests/sqlite_memsys5_proto"
Cohesion: 0.13
Nodes (29): sqlite3_memory_used(), capacity(), charge(), done(), exhaust(), hooks(), main(), power_round() (+21 more)

### Community 397 - "tests/test_lan_bridge"
Cohesion: 0.08
Nodes (17): af_packet_needs_root(), foreign_ethertype_is_dropped(), framing_matches_host_agent(), l0_hello_roundtrip(), load_bridge(), mac_mismatch_warns(), mac_str(), main() (+9 more)

### Community 398 - "system/install"
Cohesion: 0.07
Nodes (19): copy_directory(), copy_file(), fd_map_to_hdd(), lower_c(), ls_cb(), measure_need(), precheck_media(), read_exact() (+11 more)

### Community 399 - "tests/allocator_bench_host"
Cohesion: 0.08
Nodes (22): backing(), child_run(), die(), equal(), exec_owner_diag(), host_start(), last_result(), ledger_owner_pages() (+14 more)

### Community 400 - "tests/test_rshell_serial"
Cohesion: 0.07
Nodes (15): case_ack(), case_bauds(), case_echo(), case_lan(), case_probe(), case_restore(), case_switch(), case_wait_ack() (+7 more)

### Community 401 - "base/dev"
Cohesion: 0.11
Nodes (30): dev_api_get_info(), dev_blk_read_lba(), dev_blk_write_lba(), dev_count(), dev_find(), dev_get(), dev_init(), dev_register() (+22 more)

### Community 402 - "tests/launch_host"
Cohesion: 0.20
Nodes (31): launch_pending(), launch_report(), launch_selftest(), launch_take(), cancel_from(), case_cancel(), case_chain(), case_kill_report_before_reclaim() (+23 more)

### Community 403 - "tests/x11_shared_host #2"
Cohesion: 0.12
Nodes (33): wrap_ime_switch_dict(), wrap_ime_user_clear(), ime_switch_dict(), ime_user_clear_facade(), fep_store_close(), fep_store_open(), ime_set_mode(), ime_suspend_state() (+25 more)

### Community 404 - "gui/types"
Cohesion: 0.09
Nodes (19): style_bits(), Color, GFX_CAP_HW_BLT, GFX_CAP_HW_FILL, GFX_CAP_PAGE_FLIP, GFX_CAP_TEXT_OVERLAY, GFX_FMT_PACKED8, GFX_FMT_PLANAR4 (+11 more)

### Community 405 - "tools/rshell_serial"
Cohesion: 0.10
Nodes (22): ack_ok(), check_echo(), echo_line(), lan_result(), main(), make_server(), open_port(), probe() (+14 more)

### Community 406 - "tests/trident_resources_host"
Cohesion: 0.11
Nodes (32): trident_boot_reserve(), trident_resources_rollback(), check_geometry(), configure_cases(), failure(), kmemset(), kprintf(), lease_revoke_surface() (+24 more)

### Community 407 - "input/input_core"
Cohesion: 0.08
Nodes (20): bind_internal(), input_bind(), input_bind_ex(), input_unbind_all(), input_load_config(), input_save_config(), ensure_mouse_polled(), input_get_mouse() (+12 more)

### Community 408 - "system/inst_hdd"
Cohesion: 0.15
Nodes (30): inst_check_boot_files(), inst_reason(), ih_check_mounts(), ih_erasable(), ih_erase(), ih_erased_hint(), ih_foreign_hint(), ih_geom_from_kapi() (+22 more)

### Community 409 - "ata/atapi"
Cohesion: 0.16
Nodes (33): atapi_bus_dead_fail(), atapi_capacity_raw(), atapi_capacity_ready(), atapi_clear_cdb(), atapi_delay_us(), atapi_diag_fail(), atapi_has_media(), atapi_note() (+25 more)

### Community 410 - "run-os32/driver"
Cohesion: 0.10
Nodes (19): cmd_once(), die(), do_build(), do_deploy(), do_doctor(), do_pull(), do_shot(), do_smoke() (+11 more)

### Community 411 - "tests/hostsvc_block_host"
Cohesion: 0.10
Nodes (20): netcd_check(), netcd_clock(), netcd_media_check(), netcd_read(), netcd_write(), same_image(), block_call(), block_session() (+12 more)

### Community 412 - "tests/gf4_b4_host"
Cohesion: 0.20
Nodes (32): add(), add_table(), base(), checksum(), cta(), cvt(), dtd(), gf4_edid_modes() (+24 more)

### Community 413 - "pc98/pegc_v2"
Cohesion: 0.09
Nodes (24): pegc2_caps(), pegc2_cursor_image(), pegc2_cursor_move(), pegc2_cursor_show(), pegc2_dpms(), pegc2_edid(), pegc2_emit(), pegc2_enum_conn() (+16 more)

### Community 414 - "tests/trident_mode_host"
Cohesion: 0.13
Nodes (28): mode_clock(), mode_crtc(), mode_format(), mode_mux(), mode_normalize(), mode_pll_frequency(), mode_round_shift(), palette_access() (+20 more)

### Community 415 - "tests/widget_scroll_host"
Cohesion: 0.10
Nodes (26): call(), CLIP, draw_rect(), Filer, fill_rect(), gfx_direct, GuiResult, H (+18 more)

### Community 416 - "cfg/cfg_import"
Cohesion: 0.13
Nodes (30): c_s3_json(), jhdr(), jrec(), jsyn(), cfg_import_detail_name(), imp_apply(), imp_finish(), imp_get() (+22 more)

### Community 417 - "kdebug/gfinit_plan"
Cohesion: 0.10
Nodes (29): gfinit_exec_prepare(), gfinit_exec_restore(), gfinit_exec_run(), runner_begin(), runner_clock(), runner_decode_safe(), runner_read(), runner_wait() (+21 more)

### Community 418 - "tests/dma_pool_host"
Cohesion: 0.15
Nodes (30): dma_crosses_64k(), dma_range_ok(), dma_pool_align_ok(), dma_pool_pages_for(), dma_pool_span_at(), dma_pool_span_slot(), dma_pool_state_alloc(), dma_pool_state_alloc_buf() (+22 more)

### Community 419 - "tests/gf4_c3_host"
Cohesion: 0.08
Nodes (14): c3_dpms(), c3_flip(), c3_gamma(), c3_image(), c3_mode(), c3_move(), c3_show(), c3_vsync() (+6 more)

### Community 420 - "tests/gf_accel_host #2"
Cohesion: 0.05
Nodes (38): gf3_host, alloc_off, calls, cevents, ch, complete_after, cpackets, cregs (+30 more)

### Community 421 - "tests/gf_command_t2a1_host"
Cohesion: 0.15
Nodes (31): clear_events(), clock_us(), expect(), fresh(), gf2_emit(), gf2_fence_emit(), gf2_fence_wait(), gf2_fifo_pause() (+23 more)

### Community 423 - "tests/cfg_bench"
Cohesion: 0.18
Nodes (30): c_s5_pure(), bn_add_sat(), bn_args(), bn_avg(), bn_bad(), bn_drain(), bn_emit(), bn_get_failed() (+22 more)

### Community 424 - "dma/dma8237"
Cohesion: 0.12
Nodes (26): dma8237_init(), dma_chan_mask(), dma_chan_remaining(), dma_chan_setup(), dma_chan_unmask(), dma_read_count(), dma_status_poll(), dma_accept_pair() (+18 more)

### Community 425 - "ne2000/ne2000"
Cohesion: 0.12
Nodes (29): lgy98_tick(), cr_page0_idle(), cr_page1_idle(), cr_run_bits(), diag_dma_test(), diag_ram_test(), dma_read(), dma_transfer() (+21 more)

### Community 426 - "tests/pci_bind_host"
Cohesion: 0.12
Nodes (30): pci_bind_info_compose(), pci_bind_match(), pci_bind_next(), pci_bind_one(), pci_bind_reason_reset(), pci_bind_set_line_state_hook(), pci_bind_set_reason(), pci_bind_info_get() (+22 more)

### Community 427 - "cs4231/pcm_cs4231_math"
Cohesion: 0.11
Nodes (33): pcm_close_ticks(), pcm_cont(), pcm_counters_pack(), pcm_drain_next(), pcm_fail(), pcm_fmt_for_rate(), pcm_half_ms(), pcm_obs() (+25 more)

### Community 428 - "tests/trident_palette_host"
Cohesion: 0.15
Nodes (27): palette_color_sum(), palette_range(), palette_set_overscan(), palette_write(), palette_write_dac6(), tri_overscan(), tri_palette_load_default(), tri_palette_record() (+19 more)

### Community 429 - "tests/trident_module_host"
Cohesion: 0.07
Nodes (19): trident_module_init(), trident_module_abort(), trident_diagnostic_ops(), boot_common_get(), bootinfo_get(), configure(), cpu_loops_per_tick(), failure_mode() (+11 more)

### Community 430 - "tools/uimock_field"
Cohesion: 0.09
Nodes (25): arrow(), build(), castle(), cave(), character(), chest(), circle_mark(), den() (+17 more)

### Community 431 - "workflows/full-check"
Cohesion: 0.05
Nodes (36): concurrency, cancel-in-progress, group, OS32_ACCEPT_IPA_LICENSE, OS32_CLANG_CROSS_FREE, OS32_MUT_JOBS, runs-on, steps (+28 more)

### Community 432 - "tests/gfinit_romfile_host"
Cohesion: 0.11
Nodes (21): gf_display_modeset(), gf_load_state(), gfinit_mode_palette(), gfinit_mode_row(), dump(), field(), load_row(), load_section() (+13 more)

### Community 433 - "kdebug/gfinit_bus"
Cohesion: 0.14
Nodes (30): actual(), bus_access(), cfg32(), gfinit_bus_identity(), gfinit_bus_mode_ar(), gfinit_bus_mode_dac(), gfinit_bus_mode_read_row(), gfinit_bus_mode_row() (+22 more)

### Community 434 - "pc98/bootinfo_check"
Cohesion: 0.17
Nodes (31): bootinfo_drive_usable(), bootinfo_format_ata(), bootinfo_format_bios(), bootinfo_parse(), bootinfo_seal(), bootinfo_seal_image(), bootinfo_sum(), clear_out() (+23 more)

### Community 435 - "tests/module_runtime_host"
Cohesion: 0.10
Nodes (23): init_cd(), abort_one(), detach(), empty(), finish(), index_of(), kmemcpy(), kmemset() (+15 more)

### Community 436 - "tests/gf5_observe_host"
Cohesion: 0.10
Nodes (24): arg(), diag_clock(), diag_count(), diag_entry(), diag_restore(), diag_save(), diag_select(), gf5_rc_map() (+16 more)

### Community 437 - "tests/gf_accel_wire_host"
Cohesion: 0.07
Nodes (15): gf_display_blank(), gf_display_palette(), gf_present_rect(), palette_get_all(), palette_shadow_set(), wire_call(), wire_video_start(), fake_read() (+7 more)

### Community 438 - "tests/sep5_console_host"
Cohesion: 0.08
Nodes (15): at(), bootlog_push(), con_backend_color(), con_backend_cursor_at(), con_backend_putchar(), con_backend_putkanji(), con_backend_readchar(), con_backend_reverse() (+7 more)

### Community 439 - "tests/dep_dir_d_console_grid_host"
Cohesion: 0.12
Nodes (26): bootlog_push(), cell(), con_output_putchar(), con_sink_disable(), con_sink_enable(), con_sink_push_clear(), con_sink_push_cursor(), con_sink_push_print() (+18 more)

### Community 440 - "tests/hwdev_module_host"
Cohesion: 0.08
Nodes (20): boot_common_get(), bootinfo_get(), finish(), fixture_kernel(), kmemcpy(), kmemset(), kprintf(), kstrncpy() (+12 more)

### Community 441 - "e82557/e82557_desc"
Cohesion: 0.18
Nodes (29): copy_bytes(), e82557_cb_config(), e82557_cb_header(), e82557_cb_ia(), e82557_config_bytes(), e82557_link_wait_step(), e82557_mdi_ctl(), e82557_mdi_data() (+21 more)

### Community 442 - "tests/gfx_kernel_fb_host #2"
Cohesion: 0.13
Nodes (30): gfx_init_200(), lease_acquire(), pegc_window_base(), surface_query_refs(), waited_key(), diag_present_check(), diag_px_check(), display_check() (+22 more)

### Community 443 - "src/state"
Cohesion: 0.08
Nodes (26): COLS, CONTROLS, Display, Display<'a>, exact_capacity(), Fixture, Exact, Finish (+18 more)

### Community 444 - "allocator/nano_adapter"
Cohesion: 0.13
Nodes (28): allocate(), calloc_once(), enter(), free(), _free_r(), initialize(), large_allocate(), large_link() (+20 more)

### Community 445 - "src/cfg"
Cohesion: 0.08
Nodes (32): cfg_begin(), CFG_BLOB_MAX, cfg_close(), cfg_commit(), CFG_CORRUPT, CFG_DB_PATH, CFG_ERROR, cfg_get_blob() (+24 more)

### Community 446 - "tools/serialfs_host"
Cohesion: 0.14
Nodes (3): HostFS, op(), SfsError

### Community 447 - "dep_dir_c/manifest"
Cohesion: 0.06
Nodes (35): base_sha, capture_environment, compiler, log, runner, script, capture_sha256, tools/tests/dep_dir_c_memory_host.c (+27 more)

### Community 448 - "tests/kernel_shell_host"
Cohesion: 0.11
Nodes (24): append(), console_session_clear(), exec_run(), finish(), fixture_strcat(), fixture_strcmp(), fixture_strcpy(), fixture_strlen() (+16 more)

### Community 449 - "tables/sbix"
Cohesion: 0.06
Nodes (18): RasterGlyphImage, RasterImageFormat, BitmapGray2, BitmapGray2Packed, BitmapGray4, BitmapGray4Packed, BitmapGray8, BitmapMono (+10 more)

### Community 450 - "tests/bundle"
Cohesion: 0.14
Nodes (21): arena(), bundle_cps(), bundle_resolves_like_the_ttf_with_one_read(), draw(), eight_failed_reads_close_the_bundle(), every_close_path_drops_the_bundle_source(), Files, files() (+13 more)

### Community 451 - "ppc/lan_p14_fake"
Cohesion: 0.11
Nodes (24): gem_enter_reset(), gem_poll(), gem_reset_step(), next(), ticks(), wait_clear(), gem_tx(), gem_tx_reclaim() (+16 more)

### Community 452 - "tests/pci_scan_host"
Cohesion: 0.10
Nodes (20): geforce_accel_ops(), clock_now(), dispatch(), foundation(), geometry(), gf_prim_bench(), measure(), number() (+12 more)

### Community 453 - "tests/trim_stop_host"
Cohesion: 0.09
Nodes (26): appslot_abort_clear(), appslot_park_stop_commit(), g4_alloc(), g4_case(), g4_check(), g4_epoch(), g4_free(), g4_initial_large() (+18 more)

### Community 454 - "tests/hdd_stage2_host"
Cohesion: 0.13
Nodes (28): ext2_layout_group_overhead(), ext2_layout_is_sparse(), ext2_layout_plan(), is_power_of(), layout_fill(), case_layout(), case_blocks(), case_bootfiles() (+20 more)

### Community 455 - "turn/turn_query"
Cohesion: 0.15
Nodes (23): glue_save(), sync_turn_mirror(), turn_advance(), turn_init(), turn_set_active(), turn_set_order(), turn_skip(), turn_alive_count() (+15 more)

### Community 456 - "tests/sep5_gfx_init_host"
Cohesion: 0.12
Nodes (26): backend_enter(), display_palette4_write(), event(), expect(), finish(), flip_case(), _flush_dirty_queue(), gfx_bind_client() (+18 more)

### Community 457 - "tests/test_osri_enc2"
Cohesion: 0.15
Nodes (21): quantize(), trunc16(), blur_numerator(), metrics(), optimize_order(), OrderCost, Config, cuts() (+13 more)

### Community 458 - "tests/kstr_bench"
Cohesion: 0.16
Nodes (30): kb_add(), kb_bench_all(), kb_build_table(), kb_call(), kb_case_offsets(), kb_copy8(), kb_diff8(), kb_emit() (+22 more)

### Community 459 - "kdebug/recover #3"
Cohesion: 0.06
Nodes (34): recover_bar_extent, base, length, raw, valid, recover_bar_measurement, bar, identity (+26 more)

### Community 460 - "tests/kdebug_loader_host"
Cohesion: 0.09
Nodes (23): kdebug_load(), boot_common_get(), bootinfo_get(), check_boot_dispatch(), check_kdebug_call(), fake_dbg_call(), finish(), kdebug_forbidden_import() (+15 more)

### Community 461 - "tools/osri_enc2_native"
Cohesion: 0.15
Nodes (23): resize_image(), compare(), encode(), greedy(), main(), write_tables(), decode(), encode_image() (+15 more)

### Community 462 - "tests/gf_v2_gart_host"
Cohesion: 0.09
Nodes (20): failures(), fresh(), gf2_fence_emit(), gf2_fence_wait(), gf2_ramht_add(), gf_inst_alloc(), gf_inst_write32(), scenario() (+12 more)

### Community 463 - "tests/hwb_exec_host"
Cohesion: 0.24
Nodes (32): aborts(), arrays(), bad_args(), bars(), begin_records(), blocks(), clocks(), command() (+24 more)

### Community 464 - "shell/hwb_parse"
Cohesion: 0.21
Nodes (31): design_script(), eval_case(), expressions(), grammar(), instructions(), labels(), length(), limits() (+23 more)

### Community 465 - "tests/test_serialfs"
Cohesion: 0.07
Nodes (13): Args, case_outside_session(), case_serve_line(), step3(), step4(), step7(), guest_script(), first() (+5 more)

### Community 466 - "tests/vfs_excl_host"
Cohesion: 0.09
Nodes (20): case_regression(), case_x1(), case_x2(), check(), console_write(), drv_get_file_size(), drv_list_dir(), drv_stat() (+12 more)

### Community 467 - "tests/vfs_mount_dev_host"
Cohesion: 0.12
Nodes (25): boot_sequence_has_one_ext2(), duplicate_device_refused(), encode(), ext2_check_writable(), ext2_current_time(), ext2_find_entry(), ext2_get_size_ino(), ext2_is_mounted_ctx() (+17 more)

### Community 468 - "src/fontbench"
Cohesion: 0.09
Nodes (32): SCATTER, SEQ, char_starts(), Env, esp(), fstats(), I_ALLOC_FAILS, I_ARENA (+24 more)

### Community 469 - "tests/test_guest_acceptance"
Cohesion: 0.09
Nodes (7): validate(), AcceptanceTests, main(), parse_and_display(), parse_tvdump(), pc98_attr_to_ansi(), receive_tvdump()

### Community 470 - "tests/test_np21w_transport"
Cohesion: 0.11
Nodes (11): main(), WindowsExecutor, PipeFixture, close(), TransportCleanup, factory(), run(), factory() (+3 more)

### Community 471 - "tests/hwb_scripts_host"
Cohesion: 0.16
Nodes (30): array_restore(), bus(), contains(), copy(), file(), fill_tests(), find(), init_trace() (+22 more)

### Community 472 - "tests/test_ledger"
Cohesion: 0.10
Nodes (12): all_checks(), asm_problems(), _body(), broker_problems(), _code(), jmpbuf_hardcoded(), Ledger, load() (+4 more)

### Community 473 - "src/window"
Cohesion: 0.11
Nodes (9): client_size_of(), count(), drop_window(), invalidate_of(), is_focused_of(), relayout_of(), surface_of(), Window (+1 more)

### Community 474 - "geom/gfx_fill"
Cohesion: 0.11
Nodes (20): apply_palette(), draw_path_filled(), draw_path_outline(), main(), parse_vbz_header(), render_paths(), asm_gfx_hline(), command() (+12 more)

### Community 475 - "tests/cd_read_host #2"
Cohesion: 0.19
Nodes (30): atapi_drive_index(), atapi_get_stats(), atapi_init(), atapi_read_capacity(), atapi_read_sectors(), cd_module_init(), fixture_cd_read(), np2_check_protocol() (+22 more)

### Community 476 - "exec/appmem"
Cohesion: 0.16
Nodes (29): aligned(), appmem_error_public(), appmem_init(), appmem_plan_valid(), appmem_prepare(), appmem_publish(), appmem_unmap_plan_valid(), appmem_unmap_prepare() (+21 more)

### Community 477 - "workflows/check"
Cohesion: 0.06
Nodes (32): CHECK_NO_NEEDS, CI_WORKFLOW, GH_TOKEN, LYCHEE_SHA256, LYCHEE_VERSION, OS32_CLANG_CROSS_FREE, jobs, static-check (+24 more)

### Community 478 - "gf/command #2"
Cohesion: 0.06
Nodes (32): gf2_chan, B, bar1, bitmap, cur, dma_ops, dma_rc, epoch (+24 more)

### Community 479 - "tests/gf4_d0_host"
Cohesion: 0.21
Nodes (25): bmp_port(), dcb_table_read(), gf4_dcb_parse(), gpio_read(), i2c_read(), rd16(), rd32(), routes_read() (+17 more)

### Community 480 - "display/scan"
Cohesion: 0.21
Nodes (26): gf4_scan_changed(), gf4_scan_discard(), gf4_scan_prepare(), gf4_scan_release_all(), gf4_scan_reserve_initial(), scan_context(), scan_drop(), scan_image_uses() (+18 more)

### Community 481 - "tests/hwb_access_host"
Cohesion: 0.15
Nodes (28): accepted(), accesses(), aperture(), arg(), arguments(), blocks(), exec_hwb_abort_take(), failures() (+20 more)

### Community 482 - "ppc/lan_p3_fake"
Cohesion: 0.11
Nodes (22): be32(), bootargs(), compat(), get(), hex(), pm_gmac_boot_id(), pm_gmac_capture(), scalar() (+14 more)

### Community 483 - "tests/cat_linenum_host"
Cohesion: 0.12
Nodes (20): case_n(), case_n_stdin(), catf_set(), check(), count_numbers(), fake_api_init(), fk_kprintf(), fk_mem_alloc() (+12 more)

### Community 484 - "tests/fep_standard_host"
Cohesion: 0.09
Nodes (18): console_write(), dev_blk_read_lba(), dev_blk_write_lba(), dev_find(), fd_redirect_ifmt(), fd_redirect_read(), fd_redirect_write(), kmemcpy() (+10 more)

### Community 485 - "tests/geforce_module_host"
Cohesion: 0.07
Nodes (10): failing_probe_decls(), fill(), fixture_probe_decls(), geforce_backend_bind(), palette(), probe(), production_bind(), query() (+2 more)

### Community 486 - "tests/hostsvc_fd_host"
Cohesion: 0.09
Nodes (20): check(), console_write(), create(), create_excl(), fd_redirect_ifmt(), fd_redirect_read(), fd_redirect_write(), kstrlcpy() (+12 more)

### Community 487 - "tests/ne2000_dma_host"
Cohesion: 0.13
Nodes (26): ne2k_init(), ne2k_irq_enable(), cpu_delay_us(), device_init(), device_pio(), device_read(), device_setup(), expect() (+18 more)

### Community 489 - "tests/test_serialfs #2"
Cohesion: 0.12
Nodes (20): build_target(), c_build(), case_server(), const_check(), cross_check(), flags_for(), frames_of(), gate_build() (+12 more)

### Community 490 - "tests/trident_save_host"
Cohesion: 0.15
Nodes (24): emit(), equal(), invoke(), production(), reset(), scalar_fake(), sequences(), _start() (+16 more)

### Community 491 - "tests/cache"
Cohesion: 0.22
Nodes (28): arena(), big_bits_round_trip(), bitmap(), cache(), capacity_64k_and_128k(), cell_clip_matches_unclipped_drawing(), cell_copy_matches_placement(), cell_fit_boundaries() (+20 more)

### Community 492 - "tests/dep_dir_b_timer_host"
Cohesion: 0.12
Nodes (24): sysclk_detected(), call(), check(), finish(), irq_restore(), irq_save(), pit_init(), platform_timer_sample() (+16 more)

### Community 493 - "tests/kcg_boot_host"
Cohesion: 0.08
Nodes (19): kcg_init(), kcg_load_font(), wrap_kcg_init(), wrap_kcg_load_font(), wrap_kcg_set_scale(), pc98_boot_font(), platform_kcg_init(), platform_kcg_load_font() (+11 more)

### Community 494 - "tests/b8_hostdrv_host"
Cohesion: 0.16
Nodes (27): hdrv_unlink(), b8_hostdrv_main(), case_delete_entries(), case_get_file_size_directory(), case_no_truncate_on_open_failure(), case_open_failure_not_notfound(), case_other_entries(), case_status_table() (+19 more)

### Community 495 - "app/view_tiles"
Cohesion: 0.21
Nodes (29): bframe(), big_castle(), big_cave(), big_chest(), big_circle(), big_clear(), big_den(), big_gate() (+21 more)

### Community 496 - "tests/gfx_boot_host"
Cohesion: 0.12
Nodes (23): check(), check_planar_selftest(), cirrus_identify(), count(), die(), gfx_region(), host_alloc(), host_bda() (+15 more)

### Community 497 - "gf/runner"
Cohesion: 0.15
Nodes (22): gf_op_time_run(), a2_write(), gf_init_a2(), gf_init_prepare(), gf_init_run(), gf_init_run_one(), gf_init_run_script(), gf_init_script_execute() (+14 more)

### Community 498 - "tests/cd_read_host #3"
Cohesion: 0.11
Nodes (29): inp(), inpw(), outp(), outpw(), atapi_shim_inp(), atapi_shim_inpw(), atapi_shim_outp(), atapi_shim_outpw() (+21 more)

### Community 499 - "powermac/log"
Cohesion: 0.10
Nodes (23): pm_escc_drain(), pm_escc_write(), add_sat(), pm_log_add_sink(), pm_log_escc_init(), pm_log_fatal_hex(), pm_log_get_stats(), pm_log_write() (+15 more)

### Community 500 - "tests/gfx2_surface_host"
Cohesion: 0.12
Nodes (26): _start(), test(), gfx2_gart_quarantine(), alloc(), created(), gfx2_adapter_get(), gfx2_fence_done(), gfx2_gart_alloc() (+18 more)

### Community 501 - "tests/test_fdc_track"
Cohesion: 0.08
Nodes (16): build_target(), check_irq_entry(), check_read_stream_shape(), d88_to_raw(), fat12_extract(), chain_bytes(), nxt(), fat12_layout() (+8 more)

### Community 502 - "trident/vram"
Cohesion: 0.18
Nodes (27): tri_accel_init(), tri_cursor_init(), client_init(), tri_allocation_shape(), tri_block_link(), tri_common_alignment(), tri_find_space(), tri_mode_format() (+19 more)

### Community 503 - "src/hostsvc"
Cohesion: 0.07
Nodes (25): h00_fold_open_err_table(), CLIP_MAX, FILE_ERR_INVAL, FILE_ERR_ISDIR, file_source(), FileSrc, fold_open_err(), HOST_EABORT (+17 more)

### Community 504 - "gf/fifo"
Cohesion: 0.18
Nodes (25): drain(), failed_init(), gf3_fini(), gf3_init(), owns_objects(), release(), resume(), capture() (+17 more)

### Community 505 - "tests/trim_flow_host"
Cohesion: 0.17
Nodes (26): wrap_mem_map(), appslot_trim_request_as(), back_hook(), c_hook(), check_order(), crc(), deliver(), event() (+18 more)

### Community 506 - "tests/kbd_inject_host"
Cohesion: 0.20
Nodes (23): kbd_inject(), appslot_get(), become_reader(), case_byte_order(), case_discard(), case_fullscreen_destination(), case_guirunkey(), case_overflow() (+15 more)

### Community 507 - "tools/move_docs"
Cohesion: 0.12
Nodes (18): die(), git(), main(), norm(), plan(), read_moves(), repair_labels(), on() (+10 more)

### Community 508 - "tests/debug_probe_host"
Cohesion: 0.13
Nodes (23): concrete_arm(), concrete_register(), concrete_stop(), module_record_get(), module_ref_acquire(), module_ref_release(), arm(), attachment() (+15 more)

### Community 510 - "tests/gfx2_alpha_host"
Cohesion: 0.12
Nodes (22): gfx2_div255(), gfx2_fmt_info(), gfx2_from_argb(), gfx2_px_read(), gfx2_px_write(), gfx2_to_argb(), gfxsync_read_rows(), gfxsync_write_rows() (+14 more)

### Community 511 - "tests/gfx2_fence_host"
Cohesion: 0.12
Nodes (23): draw(), emit(), failure(), flush(), gfx2_adapter_get(), gfx2_dirty_add(), gfx2_surf_state_set(), gfx2_sync_get() (+15 more)

### Community 512 - "tests/gfx2_submit_host"
Cohesion: 0.12
Nodes (19): emit(), gfx2_adapter_get(), gfx2_cpu_begin(), gfx2_cpu_end(), gfx2_fmt_info(), gfx2_surf_get(), gfx2_sw_readback(), gfx2_sync_get() (+11 more)

### Community 513 - "tests/sh_launch_host"
Cohesion: 0.19
Nodes (23): build_api(), case_cmdline_too_long(), case_done(), case_failed_nomem(), case_failed_not_found(), case_full(), case_req_nogui(), case_req_other() (+15 more)

### Community 514 - "tests/vfs_kind_host"
Cohesion: 0.10
Nodes (17): check(), console_write(), drv_get_file_size(), drv_list_dir(), drv_stat(), drv_write_file(), fd_redirect_ifmt(), fd_redirect_read() (+9 more)

### Community 515 - "src/doc"
Cohesion: 0.10
Nodes (7): DOC, doc(), Doc, .NEW, VRow, .EMPTY, DOC

### Community 516 - "tables/colr #2"
Cohesion: 0.06
Nodes (30): CompositeMode, Clear, Color, ColorBurn, ColorDodge, Darken, Destination, DestinationAtop (+22 more)

### Community 517 - "raster/raster"
Cohesion: 0.14
Nodes (23): draw_color_bands(), draw_solid(), hsv_to_rgb(), main(), make_cool_palette(), make_gradient_table(), make_rainbow_palette(), make_warm_palette() (+15 more)

### Community 518 - "display/ddc"
Cohesion: 0.18
Nodes (21): display(), gf_v2_edid(), gf_v2_enum_conn(), gf_v2_enum_head(), gf_v2_enum_mode(), gf_v2_route_index(), paired(), ddc_bit() (+13 more)

### Community 519 - "src/stream"
Cohesion: 0.11
Nodes (11): Model, Report, Terminal, Terminal<'a>, Decoded, Decoder, all_two_byte_inputs_match_independent_lossy_utf8_oracle(), decode_chunks() (+3 more)

### Community 520 - "kdebug/trident_command_test"
Cohesion: 0.24
Nodes (26): client_rows(), cpu32(), cpu32_cases(), cpu32_pci_write(), cpu32_sequence(), cursor(), dpms(), drawable() (+18 more)

### Community 521 - "tests/gf_accel_host #3"
Cohesion: 0.07
Nodes (30): gf3_host_op, H_ALLOC, H_CLOCK, H_COPY, H_DEL, H_DMA_NEW, H_EMIT, H_FAULT (+22 more)

### Community 522 - "tests/gfx2_lock_host"
Cohesion: 0.12
Nodes (25): direct(), gfx2_adapter_get(), gfx2_cpu_begin(), gfx2_cpu_end(), gfx2_fence_done(), gfx2_gart_pfns(), gfx2_surf_get(), gfx2_surf_state_set() (+17 more)

### Community 523 - "tests/gfx2_v1_bridge_host"
Cohesion: 0.11
Nodes (21): fill_hook(), gfx2_adapter_get(), gfx2_cpu_begin(), gfx2_cpu_end(), gfx2_fence_done(), gfx2_gart_pfns(), gfx2_surf_quiesce(), gfx2_sync_get() (+13 more)

### Community 524 - "tests/test_vmkernel_lz4"
Cohesion: 0.14
Nodes (18): build(), case_boundary(), case_limit_names(), case_oversize(), check_image(), decode(), extract_asm(), Fail (+10 more)

### Community 525 - "tests/trident_emergency_host"
Cohesion: 0.09
Nodes (11): emit(), exec_font_guard_fault(), exec_gfx_return_emergency(), font_guard_fault(), kprintf(), restore(), sput_hex32(), sqlite_engine_fault() (+3 more)

### Community 526 - "tests/trident_route_host"
Cohesion: 0.23
Nodes (26): emit(), equal(), event(), ext(), failures(), indexed(), invoke(), oracle() (+18 more)

### Community 527 - "src/lib #8"
Cohesion: 0.20
Nodes (3): doc(), Editor, fmt_i32()

### Community 528 - "src/lib #9"
Cohesion: 0.15
Nodes (30): reset(), set_clip_put(), set_file(), set_get(), set_print(), get(), h01_copy_name_drops_control_bytes(), h01b_copy_name_empty_or_control_only_is_rejected() (+22 more)

### Community 529 - "kdebug/kdebug_window"
Cohesion: 0.13
Nodes (21): access_cfg(), pci_hwb_read(), pci_hwb_write(), valid(), gfinit_bus_init(), hwb_access(), hwb_block(), hwb_io() (+13 more)

### Community 530 - "tests/trident_device_fake"
Cohesion: 0.15
Nodes (20): trident_identify(), trident_pci_validate(), equal(), selftest(), access(), inactive_vga(), port8(), read_reg() (+12 more)

### Community 531 - "trident/video"
Cohesion: 0.19
Nodes (25): tri_video_poll(), tri_video_put(), tri_video_query(), tri_video_set_key(), tri_video_stop(), tri_video_surface_alloc(), tri_video_surface_display(), tri_video_surface_free() (+17 more)

### Community 532 - "src/vec2"
Cohesion: 0.11
Nodes (7): rs_vec2_dot(), test_add_sub(), test_dot(), test_length(), test_normalize_zero(), Vec2, .ZERO

### Community 533 - "tools/uimock_map"
Cohesion: 0.11
Nodes (18): castle(), cave(), chest(), circle_mark(), collect_office(), draw_bottom(), draw_cell(), draw_char() (+10 more)

### Community 534 - "gf/volt"
Cohesion: 0.30
Nodes (25): done(), entry_uv(), entry_vid(), gf5_gpio_find(), gf5_gpio_get(), gf5_gpio_set(), gf5_volt_check_id(), gf5_volt_get() (+17 more)

### Community 535 - "host/textcore_tests"
Cohesion: 0.10
Nodes (12): A, I, is_boundary(), TbModel, TEXT_CAP, textbox_arrows_land_on_boundaries(), textbox_backspace_removes_a_whole_kanji(), textbox_delete_removes_a_whole_kanji() (+4 more)

### Community 536 - "tests/test_np21w_gui_mcp"
Cohesion: 0.09
Nodes (4): Transport, Clock, Http, McpTests

### Community 537 - "tests/stat_cmd_host"
Cohesion: 0.16
Nodes (20): case_dev(), case_fields(), case_multi(), case_notfound(), case_usage(), check(), fake_err(), fake_ok() (+12 more)

### Community 539 - "tests/test_doc_delink_archive"
Cohesion: 0.18
Nodes (20): suite(), cli(), git(), put(), read(), test_archive_is_never_opened(), test_dry_run_paths_idempotence_and_graph(), test_errors_and_archive_path_filter() (+12 more)

### Community 540 - "tests/test_guest_tests"
Cohesion: 0.27
Nodes (25): blk(), check(), done(), drive(), entries(), eq(), run_self(), t_aggregate() (+17 more)

### Community 541 - "tests/sqlite_engine_host"
Cohesion: 0.09
Nodes (14): double_fault_test(), sqlite_engine_run(), sqlite_quiesce(), exec_font_guard_fault(), font_guard_fault(), appslot_get(), callback(), die() (+6 more)

### Community 542 - "tests/access_walk_host"
Cohesion: 0.12
Nodes (16): appslot_get(), check(), code_protection_tests(), die(), exec_tramp_page_addr(), gfx2_head_restore_v1(), gfx_policy_blocks_map(), host_start() (+8 more)

### Community 543 - "shell/cmd_peek"
Cohesion: 0.13
Nodes (17): vk32_boot(), vk32_file_crc(), vk32_strerror(), osri_load_checked(), read_part(), crc32_calc(), crc32_core_final(), crc32_core_update() (+9 more)

### Community 544 - "block/fdc"
Cohesion: 0.11
Nodes (18): fdc_buf_setup(), fdc_bump_gen(), fdc_dma_buf(), fdc_dma_phys(), fdc_forget_all(), fdc_forget_cyl(), fdc_get_last_init_status(), fdc_init() (+10 more)

### Community 545 - "geforce/gf_v2_display"
Cohesion: 0.17
Nodes (18): display(), gf_v2_cursor_image(), gf_v2_cursor_move(), gf_v2_cursor_show(), gf_v2_dpms(), gf_v2_flip(), gf_v2_flip_done(), gf_v2_gamma() (+10 more)

### Community 546 - "tests/fep_store_info_host"
Cohesion: 0.15
Nodes (23): call(), check(), check_caller_write_range(), copy_to_caller(), kmemcpy(), kstrcmp(), kstrlen(), kstrncpy() (+15 more)

### Community 547 - "tools/uimock_map2"
Cohesion: 0.11
Nodes (15): castle(), cave(), chest(), circle_mark(), den(), draw_cell(), draw_char(), house() (+7 more)

### Community 548 - "tests/gf4_b10_host"
Cohesion: 0.21
Nodes (21): fp_calc(), fp_check(), fp_deadline(), fp_image(), fp_index(), fp_load(), fp_read(), fp_write() (+13 more)

### Community 549 - "tests/kstring_c_host"
Cohesion: 0.23
Nodes (24): areas_agree(), case_alias_identity(), case_memcmp(), case_memcpy(), case_memset(), case_strcmp(), case_strcpy(), case_strlen() (+16 more)

### Community 550 - "tests/raw_wait_late_stop_host"
Cohesion: 0.09
Nodes (11): exec_reclaim_resources(), fd_redirect_clear_state(), fd_redirect_restore(), fd_redirect_save(), inject(), paging_kernel_pd_phys(), paging_load_cr3(), paging_set_page() (+3 more)

### Community 551 - "tools/fetch_fonts"
Cohesion: 0.14
Nodes (16): accepted_recorded(), ask_license(), confirm_license(), download(), env_accepts(), extract(), fonts_ok(), read_license() (+8 more)

### Community 552 - "dep_dir_c/manifest #2"
Cohesion: 0.15
Nodes (28): previous_traces, previous_traces, previous_traces, previous_traces, alias-first, alias-middle, bios-1, bios-2032 (+20 more)

### Community 553 - "tests/geforce_fgpoll_host"
Cohesion: 0.09
Nodes (11): appslot_get(), clock64(), deadline_cases(), finish(), gf_production_return(), host_halt(), kprintf(), poll_first() (+3 more)

### Community 554 - "tests/gf_init_contract_host"
Cohesion: 0.18
Nodes (24): cell(), clock_us(), context(), decode(), exec_point(), gf_init_commit(), gf_init_contract_test(), gf_init_save() (+16 more)

### Community 555 - "tests/test_gserial_host"
Cohesion: 0.13
Nodes (4): FakeClock, FakePort, GserialTests, reply()

### Community 556 - "src/model"
Cohesion: 0.12
Nodes (17): BLANK, Cell, Continuation, Single, Wide, Error, Full, InsufficientStorage (+9 more)

### Community 557 - "tools/osri_enc2_spec"
Cohesion: 0.22
Nodes (11): assign(), master(), objective(), centroid(), distance(), out_count(), window(), build_tree() (+3 more)

### Community 558 - "tests/gfx2_i2_core_host"
Cohesion: 0.09
Nodes (9): appslot_get(), display_palette4_write(), display_planar16(), display_planar_plane(), display_text_hide(), gfx_clear_planar_pages(), gfx_return_emergency(), serial_diag_u32() (+1 more)

### Community 559 - "tests/ime_display_host"
Cohesion: 0.12
Nodes (19): ime_boot_abort(), fep_boot_config_get(), fep_boot_user_path_get(), fep_store_close(), fep_store_learn(), fep_store_open(), geometry_case(), geometry_clear() (+11 more)

### Community 560 - "tests/rework"
Cohesion: 0.13
Nodes (18): accept_i32_max_pixel(), accept_i32_min_pixel(), crop_bottom_pixel(), crop_left_half(), crop_local_row_offset_and_top(), crop_negative_origin(), crop_partial_saved_shortage(), crop_right_half_from_halo() (+10 more)

### Community 561 - "sdk/os32x_hdr"
Cohesion: 0.10
Nodes (8): check_note(), build_header(), check_raw_matches_elf(), effective_min_api(), Elf32, HeaderError, parse_header(), read_kapi_layout()

### Community 562 - "tests/dep_dir_d_splash_host"
Cohesion: 0.15
Nodes (21): begin(), bytes(), event(), finish(), gfx_add_dirty_rect(), gfx_clear_planar_pages(), gfx_get_backend_pref(), gfx_init() (+13 more)

### Community 563 - "tools/pc98pt"
Cohesion: 0.14
Nodes (14): classify_pt_layout(), plan_migrate_pt(), chs_to_lba(), entry_at(), entry_empty(), entry_range(), find_os32(), _geom_ok() (+6 more)

### Community 564 - "shell/hwb_log"
Cohesion: 0.26
Nodes (23): _start(), address(), ch(), dec(), destination(), elapsed(), emit(), hex() (+15 more)

### Community 565 - "edit/command"
Cohesion: 0.21
Nodes (24): cmd_block_delete(), cmd_block_end(), cmd_block_hide(), cmd_block_start(), cmd_bottom(), cmd_filer(), cmd_goto_line(), cmd_help() (+16 more)

### Community 566 - "tests/raw_wait_guest_host"
Cohesion: 0.10
Nodes (10): alloc(), child(), db_bytes(), output(), reset(), say(), _start(), stat() (+2 more)

### Community 567 - "gf/perf"
Cohesion: 0.22
Nodes (20): gf5_above_ceiling(), gf5_plan_ceiling(), gf5_plan_domain_ceiling(), gf5_stop_request(), entry_read(), entry_valid(), finish(), gf5_heat_step() (+12 more)

### Community 568 - "powermac/mpic"
Cohesion: 0.18
Nodes (22): ipi_handler(), platform_boot_irq_controller(), platform_irq_claim(), platform_irq_complete(), pm_mpic_bus_read(), pm_mpic_bus_write(), pm_mpic_external(), pm_mpic_init() (+14 more)

### Community 569 - "tests/gfx2_v1_shim_host"
Cohesion: 0.14
Nodes (16): dpms(), flip(), gamma(), gfx2_caps_from_v1(), gfx2_v1_enum_conn(), gfx2_v1_enum_head(), gfx2_v1_enum_mode(), image() (+8 more)

### Community 570 - "tests/hostsvc_sfs_lan_host"
Cohesion: 0.12
Nodes (16): sfs_lan_init(), sfs_lan_set_peer(), clock_now(), frame(), kmemcpy(), net_mac(), net_nic_poll_once(), net_tx() (+8 more)

### Community 571 - "tests/m3_lease_host"
Cohesion: 0.15
Nodes (18): appslot_at(), die(), event(), gfx2_head_restore_v1(), host_lease_alloc(), host_lease_free(), host_pte_clear(), host_surface_pointer() (+10 more)

### Community 572 - "tests/pgalloc_range_host"
Cohesion: 0.21
Nodes (20): basic(), boundaries(), claim1(), device_window_ram(), exhaustive(), fail(), finish(), fragmentation() (+12 more)

### Community 574 - "tests/test_emu_playbook #2"
Cohesion: 0.11
Nodes (4): FakeDriver, LiveAdapterTests, key(), timeout()

### Community 575 - "tests/test_np21w_ini_live #4"
Cohesion: 0.13
Nodes (3): launch_text(), Workflow, hook()

### Community 576 - "src/trim"
Cohesion: 0.11
Nodes (19): dbgdesc_fixture(), dump(), dump_with(), emit_resident(), MEMSTAT_SIZE, OWNER_PAGES, word(), deliver() (+11 more)

### Community 577 - "cfg/cfg_backend"
Cohesion: 0.08
Nodes (4): be_read(), be_stat(), be_tick(), cfg_backend_platform()

### Community 578 - "shell/cmd_hdprep"
Cohesion: 0.14
Nodes (16): cmd_hdprep(), hp_amode_name(), hp_ask_yes(), hp_dump(), hp_geom_from_kapi(), hp_incomplete(), hp_memeq(), hp_refuse() (+8 more)

### Community 579 - "ppc/kboot_capture_fake"
Cohesion: 0.12
Nodes (16): ppc_cell_ptr(), fake_ci(), emit(), entry(), ofw_call(), ofw_finddevice(), ofw_getprop(), ofw_init() (+8 more)

### Community 580 - "tests/vfs_set_mtime_host"
Cohesion: 0.13
Nodes (13): check(), drv_set_mtime_fn(), drv_stat(), kmemset(), kstrcmp(), kstrlen(), kstrncat(), kstrncpy() (+5 more)

### Community 581 - "src/clipboard"
Cohesion: 0.14
Nodes (21): attach_transform(), attach_transform_keeps_utf8_bytes_and_stops_at_dst_end(), attach_transform_maps_lf_to_cr_keeps_tab_drops_other_controls(), cells_to_text(), CLIP_MAX, copy_all_blank_yields_nothing(), copy_drops_trailing_blank_rows_but_keeps_interior_blank_lines(), copy_replaces_nul_and_esc_with_space() (+13 more)

### Community 582 - "src/lib #10"
Cohesion: 0.14
Nodes (9): fmt_num(), Lease, LEASE_HI_COUNT, LEASE_HI_FIRST, lease_index(), LEASE_LO_COUNT, LEASE_LO_FIRST, main() (+1 more)

### Community 583 - "kdebug/gfinit_plan_c"
Cohesion: 0.22
Nodes (20): gfinit_mode_pll_ok(), c_append(), c_emit(), c_fp_on(), c_key(), c_layout(), c_load_row(), c_load_section() (+12 more)

### Community 584 - "kdebug/gf5_test"
Cohesion: 0.26
Nodes (21): clock_record(), diagnostic_wait(), different_performance(), extent_record(), finish(), gf5_test(), info(), number() (+13 more)

### Community 585 - "ppc/lan_p20_test"
Cohesion: 0.16
Nodes (18): add_sat(), close_drop(), close_slot(), discard(), log_write(), pm_lan_log_complete(), pm_lan_log_peek(), pm_lan_log_register() (+10 more)

### Community 587 - "tests/ide_pio_host"
Cohesion: 0.20
Nodes (19): ide_read_sectors(), advertised(), boundaries(), configure(), errors(), inp(), inpw(), kprintf() (+11 more)

### Community 588 - "dep_dir_c/manifest #3"
Cohesion: 0.08
Nodes (25): traces, alias-first, alias-middle, bios-1, bios-2032, bios-256, bios-4000, bios-5 (+17 more)

### Community 589 - "tests/gfx2_display_host"
Cohesion: 0.13
Nodes (18): done(), flip(), gfx2_adapter_get(), gfx2_pal_set(), gfx2_scan_ref_get(), gfx2_scan_ref_put(), gfx2_scan_ref_swap(), gfx2_surf_get() (+10 more)

### Community 590 - "tests/gfx2_rh"
Cohesion: 0.19
Nodes (15): gfxsync_read_rows(), gfxsync_write_rows(), _start(), stop(), exercise(), fail(), get(), initial() (+7 more)

### Community 591 - "tests/test_doc_graph"
Cohesion: 0.18
Nodes (20): suite(), cli(), counts(), only(), put(), test_allowed_canonical_links(), test_archive_forbidden_from_every_current_layer_and_index(), test_archive_history_is_forbidden() (+12 more)

### Community 592 - "tests/test_h5_shapes"
Cohesion: 0.15
Nodes (19): built(), cases(), compile_guest(), env(), extract(), interactive(), line(), main() (+11 more)

### Community 593 - "tables/morx"
Cohesion: 0.10
Nodes (13): ChainsIter<'a>, ContextualSubtable<'a>, Coverage, InsertionSubtable<'a>, LigatureSubtable<'a>, Subtable, SubtableKind, Contextual (+5 more)

### Community 594 - "vdpview/vdpview"
Cohesion: 0.20
Nodes (19): buffer_shift_h(), buffer_shift_v(), decode_packbits(), display_dump(), draw_block_fb(), draw_column(), draw_full(), draw_row() (+11 more)

### Community 595 - "tests/x11_park_host"
Cohesion: 0.12
Nodes (15): appslot_at(), arch_fp_restore(), arch_fp_save(), caller_access_leave(), caller_access_save(), exec_heap_restore_state(), exec_heap_save_state(), paging_kernel_pd_phys() (+7 more)

### Community 596 - "tests/h5_shapes_host"
Cohesion: 0.12
Nodes (8): sqlite_module_init(), alloc_api(), host_main(), io(), print_api(), read_api(), starts(), stat_api()

### Community 597 - "gf/feature5_service"
Cohesion: 0.22
Nodes (21): gf5_plan_invalidate(), gf5_stopping(), gf5_rc_map(), gf5_service_init(), gf5_service_poll(), service_check(), service_complete(), service_deadline() (+13 more)

### Community 598 - "display/access"
Cohesion: 0.14
Nodes (13): gf_display_alias(), gf_display_read(), gf_display_value(), gf_display_write(), baseline_point(), gf_display_baseline(), gf_display_blank(), check_saved() (+5 more)

### Community 599 - "tests/gfinit_budget_host"
Cohesion: 0.12
Nodes (19): gfinit_bus_local_cleanup(), gfinit_plan_c_control(), recover_gf_screen_safe(), budget_cases(), budget_cell_read(), budget_cell_write(), budget_clock(), budget_local() (+11 more)

### Community 600 - "ppc/lan_p12_fake"
Cohesion: 0.13
Nodes (16): pm_dma_bus(), pm_gmac_dma_init(), pm_gmac_dma_sync(), check(), event(), main(), ppc_dcbf_line(), ppc_dcbi_line() (+8 more)

### Community 601 - "host/libos32host"
Cohesion: 0.29
Nodes (18): h_first_uint(), host_clip_get(), host_clip_put(), host_get(), host_print_stream(), host_print_text(), host_time(), print_close_job() (+10 more)

### Community 602 - "tests/test_np21w_symbols"
Cohesion: 0.16
Nodes (4): elf_image(), frame(), IdentityTests, mutate()

### Community 603 - "tools/serialfs_host #2"
Cohesion: 0.10
Nodes (3): Demux, PortReceiver, serve_line()

### Community 604 - "tests/gfinit_host"
Cohesion: 0.25
Nodes (22): benchmark(), capture_fixture(), capture_tests(), contract_access(), contract_arm(), contract_bind(), contract_setup(), copy_order_tests() (+14 more)

### Community 605 - "src/lib #11"
Cohesion: 0.20
Nodes (17): gdi_stops_both_renderers(), Buf, check_both_gfx(), cstr(), dec(), draw_clip_demo(), draw_edge_clip_checks(), draw_style_samples() (+9 more)

### Community 606 - "clang_ast/type_occurrences"
Cohesion: 0.10
Nodes (3): TypeAction, TypeConsumer, TypeVisitor

### Community 607 - "tests/ring3_str_host"
Cohesion: 0.25
Nodes (19): ring3_pde_walkable_ok(), ring3_pte_writable_ok(), ring3_range_overlaps(), ring3_user_str(), case_max_len(), case_no_scratch(), case_overwrite(), case_route() (+11 more)

### Community 608 - "tests/sep5_provider_host"
Cohesion: 0.18
Nodes (16): display_provider_ledger(), display_provider_select(), gfx_cirrus_probed(), event(), finish(), init_c(), kprintf(), pick() (+8 more)

### Community 609 - "tests/lease_host"
Cohesion: 0.18
Nodes (17): die(), event(), gfx2_head_restore_v1(), host_lease_alloc(), host_lease_free(), host_pte_clear(), host_surface_pointer(), host_sync() (+9 more)

### Community 610 - "sqlite3/sqlite3 #3"
Cohesion: 0.09
Nodes (21): sqlite3_rtree_geometry, aParam, nParam, pContext, pUser, sqlite3_rtree_query_info, aCoord, anQueue (+13 more)

### Community 611 - "about/Cargo"
Cohesion: 0.14
Nodes (23): about, alloc_demo, edit_gui, filer, font_test, gdi_test, gshell, gui_bench (+15 more)

### Community 612 - "tools/rshell_serial #2"
Cohesion: 0.10
Nodes (4): AidebugPort, read_size(), SharedClientTests, failed()

### Community 613 - "tests/unicode_lookup_host"
Cohesion: 0.19
Nodes (17): unicode_file_load(), V2P(), unicode_file_lookup(), compare(), die(), harness_main(), kana_only(), kprintf() (+9 more)

### Community 614 - "tests/trident_sync_fake"
Cohesion: 0.13
Nodes (13): cpu_loops_per_tick(), gfxsync_gap_loops(), read_hook(), run(), _start(), gfx2_adapter_unregister(), gfx2_sync_publish(), gfxsync_bind() (+5 more)

### Community 615 - "tests/kpeek_cli_host"
Cohesion: 0.21
Nodes (17): case_args(), case_errors(), case_format(), case_golden(), case_lan(), case_split(), expect_arg(), expect_ok() (+9 more)

### Community 616 - "tests/test_docs_status"
Cohesion: 0.28
Nodes (19): case_archive_ignored(), case_first_line_only(), case_head_limit(), case_longest_word(), case_main_rc(), case_not_boundary(), case_old_line_ignored(), case_outside_vocab() (+11 more)

### Community 617 - "tests/test_guest_acceptance #2"
Cohesion: 0.12
Nodes (8): ExtentTests, get(), oracle(), ext(), pair(), r(), rd(), w()

### Community 619 - "tests/trim_fixtures_host"
Cohesion: 0.11
Nodes (12): contains(), fail(), finish(), gui_mock(), key(), map_mock(), os32_gui_retry_enable(), os32_gui_retry_stats() (+4 more)

### Community 620 - "edit/sys_file"
Cohesion: 0.16
Nodes (16): filer_ls_cb(), vz_filer_handle_key(), vz_open_filer(), vz_load_file(), vz_close(), vz_get_size(), vz_ls(), vz_open() (+8 more)

### Community 621 - "kernel/mtrr_wc"
Cohesion: 0.28
Nodes (20): wc_begin(), wc_cli(), wc_cpuid(), wc_cpuid_present(), wc_end(), wc_flush_tlb(), wc_pair_matches(), wc_rdmsr() (+12 more)

### Community 622 - "cirrus/wab_cirrus"
Cohesion: 0.31
Nodes (18): blt_go(), blt_setup(), cr_r(), cr_w(), gr_r(), gr_w(), sr_r(), sr_w() (+10 more)

### Community 623 - "geforce/gf_v2_ops"
Cohesion: 0.16
Nodes (18): alloc(), caps(), check(), emit(), fence_emit(), fence_state(), fence_wait(), gart_bind() (+10 more)

### Community 624 - "time/clock_service"
Cohesion: 0.12
Nodes (14): get_fattime(), wrap_rtc_read(), wrap_sys_time(), sys_time(), clock_empty_date(), clock_empty_legacy(), clock_read_date(), clock_read_legacy() (+6 more)

### Community 625 - "tools/uimock_block"
Cohesion: 0.17
Nodes (19): block(), build(), command_menu(), draw_char(), draw_mass(), grass_speck(), o_castle(), o_cave() (+11 more)

### Community 626 - "tests/vmkernel_lz4_host"
Cohesion: 0.15
Nodes (10): lz4_decode(), harness_main(), rd32(), sys3(), sys_exit(), write_all(), lz4_encode(), lz4_hash4() (+2 more)

### Community 627 - "none/legacy_guest"
Cohesion: 0.09
Nodes (3): legacy_call_dispatch(), legacy_kbd_push(), v86_gdc_capture()

### Community 628 - "tools/mk_blank_nhd"
Cohesion: 0.13
Nodes (12): capacity(), create(), cylinders(), header(), main(), NhdError, load(), main() (+4 more)

### Community 629 - "ppc/kboot_hpt_host"
Cohesion: 0.21
Nodes (19): ppc_hpt_clear(), collisions(), exit_code(), failure(), faults(), frame(), insertion(), main() (+11 more)

### Community 630 - "tests/kapi_print_host"
Cohesion: 0.20
Nodes (13): arch_kapi_va_next(), caller_copy_tests(), console_write(), dispatch_print(), emit(), equal(), forward_print(), invoke() (+5 more)

### Community 631 - "console/con_sink"
Cohesion: 0.28
Nodes (17): con_sink_disable(), con_sink_enable(), con_sink_lock(), con_sink_owner_exit(), con_sink_push_clear(), con_sink_push_cursor(), con_sink_push_exit(), con_sink_push_print() (+9 more)

### Community 632 - "tests/lzss_pkg_host"
Cohesion: 0.17
Nodes (14): die(), f_alloc(), f_close(), f_open(), f_print(), f_read(), f_seek(), f_write() (+6 more)

### Community 633 - "tests/gfx2_i3_host"
Cohesion: 0.14
Nodes (11): cpu_loops_per_tick(), draw(), flip(), flip_done(), read32(), ring3_user_range_ok(), ring3_user_range_writable(), set_mode() (+3 more)

### Community 634 - "tools/img2osri"
Cohesion: 0.17
Nodes (15): convert(), distances(), expand(), main(), obs_optimize_row(), obs_palette_history(), obs_quantized_colors(), obs_statistics() (+7 more)

### Community 635 - "ppc/mmu_flow_fake"
Cohesion: 0.25
Nodes (17): failure(), find(), finish(), mmu_main(), opened(), phandle(), ppc_load8(), ppc_stop() (+9 more)

### Community 636 - "tests/gfinit_c_only_host"
Cohesion: 0.14
Nodes (3): GF_C_ENTRY(), verify_record, GF_C_ENTRY()

### Community 637 - "tests/test_hostsvc_image"
Cohesion: 0.13
Nodes (7): Fixture, constants(), load(), main(), test(), opened(), sfs_is_dropped_but_eof_passes()

### Community 638 - "tests/test_kapi_out"
Cohesion: 0.13
Nodes (8): case_interpretation(), case_real(), case_refuse(), case_refuse_quiet(), gen(), mutate(), wrapper(), run()

### Community 640 - "tests/trident_cpu32_host"
Cohesion: 0.16
Nodes (13): bench_read(), check_group(), copy(), fill(), pci_read(), pci_write(), pgalloc_alloc_phys(), pgalloc_free_n_owner() (+5 more)

### Community 641 - "tests/trident_pc98init_host"
Cohesion: 0.28
Nodes (17): check_gate(), compare(), emit(), equal(), oracle(), selftest(), setup(), _start() (+9 more)

### Community 642 - "cmds/less"
Cohesion: 0.25
Nodes (19): clamp_top(), clear_status(), find_backward(), find_forward(), goto_bottom(), goto_top(), line_down(), line_up() (+11 more)

### Community 643 - "cff/dict"
Cohesion: 0.16
Nodes (12): DictionaryParser, DictionaryParser<'a>, END_OF_FLOAT_FLAG, FLOAT_STACK_LEN, is_dict_one_byte_op(), Operator, parse_dict_number(), parse_float() (+4 more)

### Community 644 - "tables/os2"
Cohesion: 0.10
Nodes (18): CAP_HEIGHT_OFFSET, char_range_index(), SELECTION_OFFSET, TYPE_OFFSET, TYPO_ASCENDER_OFFSET, TYPO_DESCENDER_OFFSET, TYPO_LINE_GAP_OFFSET, UNICODE_RANGES_OFFSET (+10 more)

### Community 645 - "src/cache #2"
Cohesion: 0.20
Nodes (6): api(), block(), Cache, raw_map(), Stat, hook()

### Community 646 - "src/input #2"
Cohesion: 0.14
Nodes (19): Action, Move, None, Quit, Select, fixed_keys_and_keyup_ignored(), key(), nav() (+11 more)

### Community 647 - "src/cache #3"
Cohesion: 0.10
Nodes (20): ALIGN, ARENA, BLOCK_BYTES, CUR, EXEC_BASE, EXEC_SIZE, FLAGS, FREE_MAGIC (+12 more)

### Community 648 - "ppc/lan_p19_fake"
Cohesion: 0.18
Nodes (14): gem_rx(), get64(), put64(), inject(), inject_try(), observe_buffer(), observe_desc(), out() (+6 more)

### Community 649 - "trident/accel"
Cohesion: 0.32
Nodes (15): ge_command(), ge_dim(), ge_poll(), ge_rect(), ge_replicate(), ge_xy(), tri_clear_sync(), tri_copy() (+7 more)

### Community 650 - "tests/multiapp_impl_host #2"
Cohesion: 0.13
Nodes (19): appslot_state(), appslot_resume_commit(), case_fullscreen_resume_destination(), case_redirect_context(), case_yield_and_kill_chain(), host_reset_files(), kstrncpy(), ma_irq_abort_request() (+11 more)

### Community 651 - "tests/gfx_policy_host"
Cohesion: 0.18
Nodes (11): direct_hooks(), fill(), finish(), get(), kprintf(), mono(), put(), rop() (+3 more)

### Community 652 - "tests/hostsvc_init_host"
Cohesion: 0.14
Nodes (11): kmalloc(), kmemcpy(), kmemset(), kprintf(), lan_module_load(), mailbox_at_link_start(), net_mac(), net_start() (+3 more)

### Community 653 - "gf/clock"
Cohesion: 0.32
Nodes (17): classified(), clock_io(), coefficient(), gf5_clock_calc(), gf5_clock_prog(), gf5_clock_raw_restore(), gf5_clock_raw_save(), gf5_clock_read() (+9 more)

### Community 654 - "gf/grctx"
Cohesion: 0.30
Nodes (18): construct_general(), construct_shader(), construct_state3d(), construct_state3d_2(), construct_state3d_3(), _cp_bra(), cp_ctx(), cp_lsr() (+10 more)

### Community 655 - "module/module_image"
Cohesion: 0.26
Nodes (13): check_name(), component_policy(), get16(), in_text(), module_image_header(), module_image_relocate(), module_image_validate(), overlap() (+5 more)

### Community 656 - "tests/klibc_test"
Cohesion: 0.22
Nodes (15): kstrncat(), strcspn(), strspn(), check(), main(), my_fabs(), test_div64(), test_fabs() (+7 more)

### Community 659 - "ppc/flow_fake"
Cohesion: 0.26
Nodes (16): failure(), find(), finish(), flow_main(), opened(), phandle(), ppc_load8(), ppc_stop() (+8 more)

### Community 660 - "ppc/lan_p22_fixture"
Cohesion: 0.22
Nodes (16): clock_cb(), copy(), fail(), kmemset(), memcpy(), memset(), out(), peek() (+8 more)

### Community 661 - "tests/cd_pkg_host"
Cohesion: 0.18
Nodes (13): build(), data_byte(), f_kprintf(), f_mem_alloc(), f_open(), f_open_fail(), f_read(), f_write() (+5 more)

### Community 662 - "tests/gfinit_exec_host"
Cohesion: 0.17
Nodes (14): adapter_cases(), exec_bus_read(), exec_bus_write(), exec_clock(), exec_delay(), exec_log_has(), exec_log_put(), exec_log_wait() (+6 more)

### Community 663 - "tests/gfx2_modeset_host"
Cohesion: 0.14
Nodes (10): conn(), gfx2_adapter_get(), gfx2_scan_ref_swap(), gfx2_surf_get(), gfx2_surf_quiesce(), gfx_screen_owner(), mode(), set() (+2 more)

### Community 664 - "tests/layout_cycle_host"
Cohesion: 0.15
Nodes (12): alloc(), exec(), finish(), memset(), observed_main(), print(), _start(), stat() (+4 more)

### Community 665 - "tests/test_emu_playbook #3"
Cohesion: 0.17
Nodes (6): ReviewTests, request(), response(), urlopen(), KapiCli, state()

### Community 666 - "tests/test_rshell_hwb"
Cohesion: 0.17
Nodes (12): check(), exercise(), attempt(), fields_exercise(), file_exercise(), attempt(), load(), main() (+4 more)

### Community 667 - "tests/test_src_layout"
Cohesion: 0.25
Nodes (3): command(), LayoutTools, load()

### Community 668 - "tests/fdc_seek_host"
Cohesion: 0.23
Nodes (14): fdc_buf_layout(), fdc_classify_seek_end(), fdc_crosses_bank(), fdc_rw_timeout_ticks(), fdc_sis_result_bytes(), main(), other_drive(), real_hw_story() (+6 more)

### Community 669 - "gem/gem_phy"
Cohesion: 0.22
Nodes (14): frame(), gem_mif_init(), gem_phy_step(), next(), rd(), supported(), ticks(), wr() (+6 more)

### Community 670 - "kdebug/kpeek"
Cohesion: 0.20
Nodes (13): sfs_dead_name(), sfs_lan_peer_mac(), sio_now(), sio_put(), kpeek_call(), kpeek_lan_probe(), kpeek_lan_send(), kpeek_load() (+5 more)

### Community 671 - "tools/uimock #2"
Cohesion: 0.16
Nodes (4): build(), mu_button(), mu_window(), Screen

### Community 672 - "tools/uimock_hud_scroll"
Cohesion: 0.17
Nodes (11): assets(), draw_bottom_idle(), draw_field(), terrain_at(), draw_mass_cell(), draw_panel(), draw_terrain(), hp_color() (+3 more)

### Community 673 - "sync/a0_weak"
Cohesion: 0.22
Nodes (13): gfxsync_bind(), gfxsync_cfg_check(), gfxsync_flush(), gfxsync_gap_loops(), gfxsync_mode(), gfxsync_override(), gfxsync_override_clear(), gfxsync_profile() (+5 more)

### Community 674 - "build-setup/action"
Cohesion: 0.11
Nodes (18): description, APT_CACHE_HIT, APT_PACKAGES, SEGMENT_DOWNLOAD_TIMEOUT_MINS, name, runs, steps, using (+10 more)

### Community 675 - "tests/gf_prim_bench_host"
Cohesion: 0.20
Nodes (12): copy(), expected_item(), fill(), hit(), mono(), pgalloc_alloc_phys(), pgalloc_free_n_owner(), read_bar() (+4 more)

### Community 676 - "tests/gfx2_exec_host"
Cohesion: 0.13
Nodes (7): exec_owner_diag(), ledger_owner_pages(), ledger_owner_retire(), ledger_reclaim_owner(), paging_addrspace_free_user_range(), run(), _start()

### Community 677 - "ppc/lan_probe"
Cohesion: 0.19
Nodes (16): take(), allocate_ports(), capture(), execute(), response(), cells(), children(), choose_node() (+8 more)

### Community 678 - "tests/gf1_dispatch_host"
Cohesion: 0.14
Nodes (7): command_link(), gf_inst_alloc(), gf_inst_read32(), gf_inst_write32(), prepare_cases(), request(), _start()

### Community 679 - "tests/gfx2_i2_host"
Cohesion: 0.15
Nodes (10): draw(), mark(), progress(), ring3_user_range_ok(), ring3_user_range_writable(), _start(), wait(), probe() (+2 more)

### Community 680 - "tests/host_cmd_host"
Cohesion: 0.18
Nodes (13): check(), file_exists(), file_size(), has(), main(), reset_out(), rm(), run() (+5 more)

### Community 681 - "tests/irq_controller_host"
Cohesion: 0.15
Nodes (9): event(), expect(), handler(), isr_unexpected_report(), outp(), platform_irq_claim(), platform_irq_complete(), platform_irq_reserved() (+1 more)

### Community 682 - "tests/lspci_cmd_host"
Cohesion: 0.21
Nodes (14): expect_text(), h_kprintf(), h_pci_bind_info(), h_pci_cfg_read32(), h_pci_count(), h_pci_get(), lspci_badargs(), lspci_noarg() (+6 more)

### Community 686 - "tests/tri_burst2_host"
Cohesion: 0.19
Nodes (14): access(), file(), has(), output(), point(), survives(), value(), hwb_probe_execute() (+6 more)

### Community 687 - "src/inject"
Cohesion: 0.13
Nodes (14): BYTE_BS, BYTE_ENTER, BYTE_TAB, Bytes, .EMPTY, from_key(), from_text(), key_injects_only_control_keys_on_press() (+6 more)

### Community 688 - "tests/host_sdk_rs_host"
Cohesion: 0.15
Nodes (7): body(), contracts(), read(), setup(), State, STATE, status()

### Community 689 - "block/fdc_track"
Cohesion: 0.25
Nodes (15): fdc_sec_find(), fdc_sec_put(), fdc_track_eot(), fdc_track_fits(), fdc_track_hit(), fdc_track_init(), fdc_track_init_sector(), fdc_track_is_bad() (+7 more)

### Community 690 - "tests/ne2000_ring_test"
Cohesion: 0.29
Nodes (13): ne2k_ring_advance(), ne2k_ring_bnry_for(), ne2k_ring_check(), ne2k_ring_decode_hdr(), ne2k_ring_first_chunk(), ne2k_ring_pages_for(), expect(), hdr_np21w() (+5 more)

### Community 691 - "v2/enum"
Cohesion: 0.21
Nodes (9): gfx2_adapter_info(), gfx2_connector_edid(), gfx2_connector_info(), gfx2_head_info(), gfx2_mode_count(), gfx2_mode_get(), gfx2_v1_enum_conn(), gfx2_v1_enum_head() (+1 more)

### Community 692 - "shell/kbd_watch"
Cohesion: 0.33
Nodes (14): case_watch_fmt(), case_watch_lost(), kbdstat_watch(), kbdw_begin(), kbdw_ch(), kbdw_dec(), kbdw_fmt_ent(), kbdw_fmt_lost() (+6 more)

### Community 693 - "tests/trident_surface_lifetime_host"
Cohesion: 0.18
Nodes (8): trident_resources_get(), life_read(), life_shape(), pci_cfg_read16(), pci_cfg_read32(), pci_cfg_read8(), run(), sys_time_now()

### Community 694 - "mm/pgalloc #2"
Cohesion: 0.11
Nodes (18): ledger_surface, backend, backing, cache, closing, first, format, gen (+10 more)

### Community 695 - "tests/gfx2_core_fake"
Cohesion: 0.13
Nodes (12): _start(), test_core_calls(), test_provider(), test_registry(), test_sync_caller(), gfx2_adapter_register(), gfx2_head_detach(), gfx2_head_restore_v1() (+4 more)

### Community 696 - "tests/gfx_text_host"
Cohesion: 0.18
Nodes (11): ank(), finish(), gfx_draw_font(), gfx_fill_rect(), gfx_pixel(), kanji(), _start(), submit() (+3 more)

### Community 697 - "tests/hostsvc_sdk_host"
Cohesion: 0.21
Nodes (13): alloc(), close_call(), eq(), notice(), open_call(), read_call(), reset(), _start() (+5 more)

### Community 698 - "tests/hwb_probe_engine"
Cohesion: 0.20
Nodes (13): hwb_engine_call(), hwb_engine_epoch(), hwb_engine_hit(), hwb_engine_long_name(), hwb_engine_prints(), hwb_engine_reset(), hwb_engine_used(), configure() (+5 more)

### Community 699 - "tests/pcm_wav_host"
Cohesion: 0.20
Nodes (11): put32(), run(), setup(), _start(), main(), read_at(), status(), wav_id() (+3 more)

### Community 700 - "tests/trident_restore_host"
Cohesion: 0.31
Nodes (14): copy(), emit(), equal(), pair(), scenario(), _start(), tests(), trident_ar_write() (+6 more)

### Community 701 - "src/lerp"
Cohesion: 0.18
Nodes (17): ease_bounce(), ease_in_cubic(), ease_in_out_cubic(), ease_in_out_quad(), ease_in_quad(), ease_out_cubic(), ease_out_quad(), fix16_lerp() (+9 more)

### Community 702 - "edit/text_block"
Cohesion: 0.26
Nodes (15): cmd_block_copy(), extract_rect_text(), find_line_col(), get_col_from_index(), get_line_from_index(), te_block_copy(), te_block_copy_rect(), te_block_delete() (+7 more)

### Community 703 - "edit/core"
Cohesion: 0.15
Nodes (9): vz_alloc(), vz_init(), macro_init(), macro_start(), execute_high_level_command(), main(), vz_run_loop(), vz_getch() (+1 more)

### Community 704 - "tests/kbd_status_host"
Cohesion: 0.21
Nodes (11): kbd_irq_handler(), kbd_status_classify(), kbd_status_reflects(), case_data(), case_empty(), case_error(), case_overrun(), case_reflect() (+3 more)

### Community 705 - "ggg/feature_variations"
Cohesion: 0.15
Nodes (7): Condition, Format1, ConditionSet<'a>, FeatureTableSubstitution<'a>, FeatureVariations<'a>, FeatureIndex, VariationIndex

### Community 706 - "serialfs/sfs_lan"
Cohesion: 0.23
Nodes (11): lan_can_wait(), lan_get(), lan_idle(), lan_now(), lan_put(), lan_put_until(), lan_receive(), mac_equal() (+3 more)

### Community 707 - "v2/backend_reg"
Cohesion: 0.21
Nodes (11): adapter_by_v1(), adapter_heap_init(), be_check(), gfx2_adapter_activate(), gfx2_adapter_gen(), gfx2_adapter_get(), gfx2_adapter_register(), gfx2_adapter_unregister() (+3 more)

### Community 708 - "tests/gfx2_check_op_host"
Cohesion: 0.18
Nodes (9): check(), gfx2_check_exec(), gfx2_check_op(), shadow(), visible(), gfx2_adapter_get(), gfx2_sync_get(), probe() (+1 more)

### Community 709 - "kdebug/recover #4"
Cohesion: 0.12
Nodes (17): recover_plan, arg_count, args, capture_count, captures, ctx_count, dep_count, deps (+9 more)

### Community 710 - "tools/gen_font16"
Cohesion: 0.18
Nodes (9): ank_to_unicode(), build_ank(), build_kanji(), jis_to_unicode(), main(), pack_1bpp(), preview(), render_fitted() (+1 more)

### Community 711 - "tests/test_np21w_ctl #3"
Cohesion: 0.19
Nodes (5): ini_media(), is_win_abs(), win_sep(), Base, IniParse

### Community 712 - "ppc/kboot_cpu_host"
Cohesion: 0.26
Nodes (15): check(), event(), mark(), ppc_bat_set(), ppc_cpu_test_spr(), ppc_cpu_test_sr(), ppc_cpu_test_stream_stop(), ppc_isync() (+7 more)

### Community 713 - "tests/dep_dir_b_pic_host"
Cohesion: 0.24
Nodes (14): check(), event(), finish(), _halt(), inp(), input(), io_wait(), io_wait_n() (+6 more)

### Community 714 - "tests/exec_image_host #2"
Cohesion: 0.14
Nodes (12): argv_cases(), image_range_tests(), kprintf(), observed_decode(), say(), shell_print(), shell_print_dec(), _start() (+4 more)

### Community 715 - "tests/gf4_platform_host"
Cohesion: 0.17
Nodes (8): irq_unregister(), kprintf(), pci_cfg_read16(), pci_cfg_read8(), pci_cfg_write16(), pgalloc_alloc_phys(), pgalloc_free_n_owner(), sys_time_now()

### Community 716 - "tests/gf_v2_display_host"
Cohesion: 0.20
Nodes (10): flip(), gf4_flip_complete(), gf4_palette_set(), gf_v2_deadline(), gf_v2_route_index(), gf_v2_start(), kprintf(), set_mode() (+2 more)

### Community 717 - "tests/gfx_cursor_host"
Cohesion: 0.14
Nodes (6): EVENTS, gfx_direct, GUI_COLOR_TEXT, GUI_COLOR_WINDOW, GuiState, queue_present()

### Community 718 - "tests/hsync_fep_protect_host"
Cohesion: 0.22
Nodes (11): eq(), fake_stat(), os32_ls(), _start(), str_ncat(), str_ncpy(), test(), check() (+3 more)

### Community 719 - "tests/test_gfx_boot"
Cohesion: 0.18
Nodes (11): build(), gfx_text(), GfxBoot, load(), _make(), test(), mutate(), run_case() (+3 more)

### Community 720 - "tests/test_serialfs #3"
Cohesion: 0.14
Nodes (7): case_aidebug_port(), case_demux(), case_ntpath(), case_paths(), check(), expect_err(), list_all()

### Community 721 - "tests/test_tar_cmd"
Cohesion: 0.31
Nodes (13): build_target(), case_create_readable_by_python(), case_directory_recursion(), case_extract_python_ustar(), case_list_python_ustar(), case_missing_input(), case_name_exactly_100(), case_reject_long_name() (+5 more)

### Community 722 - "src/fixture"
Cohesion: 0.19
Nodes (13): call_open(), call_read_at(), FIXTURE_PATH, gshell_fixture_gen, gshell_fixture_id, gshell_fixture_rc, gshell_fixture_state, Line (+5 more)

### Community 723 - "src/trig"
Cohesion: 0.18
Nodes (15): deg_to_idx(), icos(), isin(), ISIN_ENTRIES, ISIN_SCALE, rad256_to_idx(), rs_deg_to_idx(), rs_icos() (+7 more)

### Community 724 - "tests/ring3_marker"
Cohesion: 0.23
Nodes (9): main(), _start(), _start(), _start(), r3_arm(), r3_call(), r3_exit(), r3_marker() (+1 more)

### Community 725 - "tests/display_cleanup_host"
Cohesion: 0.13
Nodes (4): outp(), outpw(), tv_restore(), v86_bios_real_lowmem()

### Community 726 - "ppc/lan_p15_fake"
Cohesion: 0.19
Nodes (10): gem_ring_prepare(), gem_ring_publish(), put32(), put64(), fail(), output(), rd(), _start() (+2 more)

### Community 727 - "tests/trident_gap_host"
Cohesion: 0.17
Nodes (10): trident_regs_bind(), draw(), pgalloc_alloc_phys(), pgalloc_free_n_owner(), readback(), same(), serial_puts_polled(), _start() (+2 more)

### Community 728 - "exec/launch"
Cohesion: 0.20
Nodes (14): id_ok(), launch_cancel(), launch_chain(), launch_child(), launch_owner_exit(), launch_poll(), launch_req(), row_by_token() (+6 more)

### Community 729 - "host/host_call"
Cohesion: 0.28
Nodes (11): call_buffer(), call_expired(), call_wait(), host_call(), host_exchange(), list_body(), list_id(), list_uint() (+3 more)

### Community 730 - "display/flip"
Cohesion: 0.42
Nodes (12): commit(), counter(), diagnostic(), gf4_flip(), gf4_flip_complete(), gf4_vblank_wait(), sample(), stamp() (+4 more)

### Community 731 - "display/hw_state_crtc"
Cohesion: 0.49
Nodes (13): cr_list(), fields(), gate(), gf4_hw_crtc_save(), idx_rd(), idx_wr(), rd(), save_ext() (+5 more)

### Community 732 - "tests/trim_back"
Cohesion: 0.23
Nodes (9): check_hook(), data_crc(), main(), prepare(), regrow(), run_gui(), serve(), snapshot() (+1 more)

### Community 733 - "tests/pci_hwb_host"
Cohesion: 0.20
Nodes (11): inp(), inpd(), inpw(), outp(), outpd(), outpw(), read_data(), reset() (+3 more)

### Community 734 - "powermac/kboot_console"
Cohesion: 0.22
Nodes (7): console_hw_cursor_enable(), console_write(), kprintf_attr_to_pc98(), shell_print(), shell_print_dec(), shell_print_hex32(), shell_print_utf8()

### Community 735 - "powermac/pci_config"
Cohesion: 0.31
Nodes (14): master(), address(), le_read(), le_swap(), le_write(), pm_gmac_early_quiesce(), pm_gmac_quiesce_setup(), pm_pci_mmio_read32() (+6 more)

### Community 736 - "tools/gfinit_mode_tables"
Cohesion: 0.19
Nodes (11): initializers(), add(), cr(), literal(), live(), mm(), sr(), state() (+3 more)

### Community 737 - "ppc/lan_p24d_test"
Cohesion: 0.17
Nodes (9): die(), platform_clock_read_us(), pm_lan_copy8(), ppc_irq_restore(), ppc_irq_save(), ppc_pd_lookup_read(), ppc_root_current(), _start() (+1 more)

### Community 738 - "tests/gf4_c2linit_host"
Cohesion: 0.38
Nodes (11): c2_code(), c2_controls(), c2_deadlines(), c2_errors(), c2_io(), c2_opcode_budgets(), c2_put(), c2_reset() (+3 more)

### Community 739 - "tests/gf_accel_b7_host"
Cohesion: 0.38
Nodes (13): chunk(), failures(), gart(), gart_range(), normal(), packet(), setup(), split_packets() (+5 more)

### Community 740 - "pcm_hostshim/io"
Cohesion: 0.14
Nodes (6): pcm_shim_inp(), pcm_shim_outp(), inp(), inpw(), outp(), outpw()

### Community 741 - "cmap/format14"
Cohesion: 0.14
Nodes (7): GlyphVariationResult, Found, UseDefault, Subtable14<'a>, UnicodeRangeRecord, .SIZE, Subtable<'a>

### Community 742 - "tables/feat"
Cohesion: 0.15
Nodes (5): FeatureName, FeatureNames<'a>, FeatureNamesIter<'a>, SettingName, .SIZE

### Community 743 - "kdebug/recover #5"
Cohesion: 0.13
Nodes (15): recover_plan_node, arg_count, arg_first, ctx, expr_id, flags, head, main_id (+7 more)

### Community 744 - "kdebug/recover #6"
Cohesion: 0.13
Nodes (15): recover_rule, flags, guard_count, guards, id, key, main_id, pc (+7 more)

### Community 745 - "mm/pgalloc #3"
Cohesion: 0.13
Nodes (15): ledger_resource, bar, bdf, boot_gen, bus, decode_end, decode_first, device (+7 more)

### Community 746 - "ppc/lan_p21_fixture"
Cohesion: 0.28
Nodes (9): get_be(), pm_lan_decode(), pm_lan_encode(), put_be(), fail(), kmemset(), memset(), out() (+1 more)

### Community 747 - "src/ls"
Cohesion: 0.16
Nodes (10): LsEntry, LsPacket, OS32_ERR_INVAL, os32_ls(), OS32_LS_BATCH, OS32_LS_NAME_SIZE, Api, api() (+2 more)

### Community 748 - "tools/host_linux"
Cohesion: 0.22
Nodes (5): clip_start(), _command(), print_start(), _Proc, ServiceUnavailable

### Community 749 - "ppc/test_lan_p22d_host"
Cohesion: 0.21
Nodes (10): ppc_trap_onfault_clear(), ppc_trap_onfault_data(), fatal(), main(), mark(), ppc_root_current(), ppc_trap_fatal_sink(), ppc_trap_msr() (+2 more)

### Community 750 - "tests/dep_dir_a_l09_host"
Cohesion: 0.21
Nodes (11): die(), fail(), info(), kmemcpy(), np2_check_hostdrv(), np2_get_clock(), np2_get_cpu(), np2_get_version() (+3 more)

### Community 751 - "tests/host_lib_host #2"
Cohesion: 0.23
Nodes (12): abort_src(), buf_src(), cap_sink(), fk_close_fn(), fk_get_tick(), fk_open_fn(), fk_read_fn(), fk_status_fn() (+4 more)

### Community 752 - "tests/test_nhd_deploy_failure"
Cohesion: 0.16
Nodes (3): FakeCompleted, SyncFailure, fake()

### Community 753 - "tests/test_np21w_ctl #4"
Cohesion: 0.24
Nodes (5): _load(), mutate(), run(), Scripts, _suite_failures()

### Community 755 - "tests/test_serialfs #4"
Cohesion: 0.14
Nodes (3): case_read_size(), reply(), FakeSerial

### Community 756 - "tests/widget_scroll_host #2"
Cohesion: 0.32
Nodes (13): check_pixels(), Editor, editor_pixels(), filer_pixels(), filer_two_downs_pixels(), focus_frame_absolute_parity(), list_pixels(), paint() (+5 more)

### Community 757 - "tables/loca"
Cohesion: 0.16
Nodes (8): IndexToLocationFormat, Long, Short, Table, Table, Table<'a>, Long, Short

### Community 758 - "tables/os2 #2"
Cohesion: 0.15
Nodes (11): Weight, Black, Bold, ExtraBold, ExtraLight, Light, Medium, Normal (+3 more)

### Community 759 - "rt/dbgserial"
Cohesion: 0.27
Nodes (10): _dbg_check_pages(), _dbg_is_blacklisted(), dbg_memdump(), dbg_memdumpn(), _dbg_put_uint(), dbg_serial_hexdump(), dbg_serial_init(), dbg_serial_init_baud() (+2 more)

### Community 760 - "tests/allocator_bench"
Cohesion: 0.30
Nodes (11): child(), holes(), main(), now(), observe_map(), observe_unmap(), parent(), row() (+3 more)

### Community 761 - "ppc/mmu_run"
Cohesion: 0.30
Nodes (11): api_rejection(), build(), detector(), host(), main(), mutate(), qemu(), static() (+3 more)

### Community 762 - "x86/exec_shlib"
Cohesion: 0.18
Nodes (11): exec_shlib_addrspace_attach(), exec_shlib_addrspace_detach(), exec_shlib_data_pages(), exec_shlib_loaded(), exec_shlib_read_page(), shlib_addrspace_attach(), shlib_addrspace_detach(), shlib_data_pages() (+3 more)

### Community 763 - "video/gf_accept_cpu"
Cohesion: 0.41
Nodes (12): arch_video_cache_inspect(), cache_begin(), cache_emit(), cache_mode(), cache_number(), cache_word(), cpu_record(), cpuid() (+4 more)

### Community 764 - "tests/sep5_display_host"
Cohesion: 0.29
Nodes (9): color(), entry(), expect(), finish(), port_read(), port_write(), scroll_cases(), scroll_trace() (+1 more)

### Community 765 - "lib/divdi3"
Cohesion: 0.26
Nodes (11): __divdi3(), divide_unsigned(), __divmoddi4(), __moddi3(), __udivdi3(), __udivmoddi4(), __umoddi3(), __udivdi3() (+3 more)

### Community 766 - "tests/hwdev_trace_image"
Cohesion: 0.20
Nodes (5): get_probe_decls(), fail(), hwdev_image_symbol(), hwdev_trace_load(), read_all()

### Community 767 - "kdebug/recover #7"
Cohesion: 0.14
Nodes (14): recover_value, allowed, allowed_count, bits, expr_id, mask, max, min (+6 more)

### Community 768 - "tests/shutdown_probe_host"
Cohesion: 0.18
Nodes (8): surface_query_authorize(), memcmp(), memset(), probe_identity(), probe_message_is(), probe_print(), probe_query(), run()

### Community 769 - "src/status"
Cohesion: 0.26
Nodes (8): Line, LINE_COUNT, lines(), live_lines(), live_status_reports_drops_and_reader_rejection(), live_status_reports_injection_failures(), SinkStatus, stopped_status_distinguishes_bytes_pending_and_finish()

### Community 773 - "tests/fep_budget_linux"
Cohesion: 0.22
Nodes (8): call3(), close(), getpid(), kill(), lseek(), open(), read(), write()

### Community 774 - "tests/fep_conn_host"
Cohesion: 0.22
Nodes (8): expect(), os32_sqlite_close(), os32_sqlite_finalize(), os32_sqlite_outer_group_begin_close(), os32_sqlite_outer_group_finish(), setup(), _start(), test()

### Community 775 - "tests/test_trim_flow"
Cohesion: 0.21
Nodes (7): run(), run(), build_fixture(), build_nano(), execute(), main(), nano_builder()

### Community 776 - "tests/gf5_gpio_diag_host"
Cohesion: 0.31
Nodes (9): arg(), check_wiring(), find(), get(), quit(), rejected(), request(), set() (+1 more)

### Community 778 - "tests/tri_burst6_host"
Cohesion: 0.38
Nodes (12): access(), check_depth(), count_for(), file(), has(), output(), point(), seed_for() (+4 more)

### Community 780 - "src/random"
Cohesion: 0.20
Nodes (6): GLOBAL_RNG, Rng, rs_rng_fix16(), rs_rng_seed(), test_deterministic(), test_range()

### Community 781 - "tests/restest"
Cohesion: 0.37
Nodes (12): check(), main(), str_eq(), test_all(), test_fd_leak(), test_pipe_leak(), test_redirect_leak(), usage() (+4 more)

### Community 782 - "tests/x87_host"
Cohesion: 0.22
Nodes (8): arch_boot_after_paging(), x87_probe(), control(), get_control(), pop_one(), push_one(), run(), _start()

### Community 783 - "kernel/video_wc"
Cohesion: 0.35
Nodes (9): arch_video_wc_eligible(), arch_video_wc_start(), bar0_uc(), gf_wc_cpuid(), gf_wc_flags(), gf_wc_msr(), gf_wc_set_flags(), mapped_uc() (+1 more)

### Community 784 - "tests/exec_entry_host"
Cohesion: 0.27
Nodes (9): child(), fail(), observe_user(), ring3_resume(), say(), _start(), store(), tss_get_esp0() (+1 more)

### Community 785 - "cirrus/wab_glue_xe10"
Cohesion: 0.32
Nodes (9): ctrl_in(), ctrl_out(), xe10_in(), xe10_init(), xe10_linear_enable(), xe10_map(), xe10_mmio_enable(), xe10_out() (+1 more)

### Community 786 - "tests/sqlite_memsys5_linux"
Cohesion: 0.17
Nodes (3): _exit(), kill(), lseek()

### Community 787 - "tests/fep_module_host"
Cohesion: 0.21
Nodes (8): fep_start(), fep_store_live_connections(), ime_current_store(), mock_connections(), mock_init(), module_exports(), _start(), test()

### Community 788 - "snd/libos32snd"
Cohesion: 0.17
Nodes (5): game_sound_init(), libos32snd_init(), snd_bgm_is_playing(), snd_bgm_wait(), snd_se_play_custom()

### Community 789 - "v2/a0_weak #3"
Cohesion: 0.17
Nodes (10): gfx2_connector_edid(), gfx2_gamma(), gfx2_pal_set_v1(), gfx2_present(), gfx2_px_read(), gfx2_px_write(), gfx2_surf_new(), gfx2_surface_create() (+2 more)

### Community 790 - "v2/format"
Cohesion: 0.38
Nodes (10): channel(), clamp8(), expand(), gfx2_div255(), gfx2_fmt_info(), gfx2_from_argb(), gfx2_px_read(), gfx2_px_write() (+2 more)

### Community 792 - "gf/ramclock"
Cohesion: 0.53
Nodes (10): clock_now(), delay(), finish(), first(), gf5_ram_prog(), mask(), rd(), sr_write() (+2 more)

### Community 794 - "tests/gf_surface_case_host"
Cohesion: 0.26
Nodes (7): clock_now(), download(), ifc(), map(), move(), scale(), upload()

### Community 795 - "tests/gf_v2_readback_host"
Cohesion: 0.23
Nodes (7): clock_us(), finish(), gf2_emit(), gf2_fence_emit(), gf2_fence_wait(), gf3_ifc(), _start()

### Community 796 - "tests/gfx2_ckey_host"
Cohesion: 0.29
Nodes (9): gfx2_fmt_info(), gfx2_from_argb(), gfx2_px_read(), gfx2_px_write(), gfx2_to_argb(), gfxsync_read_rows(), gfxsync_write_rows(), _start() (+1 more)

### Community 797 - "tests/gfx2_enum_host"
Cohesion: 0.22
Nodes (7): conn(), edid(), gfx2_adapter_get(), head(), main(), mode(), _start()

### Community 798 - "tests/recover_tables_host"
Cohesion: 0.42
Nodes (10): rt_bounds(), recover_profile, rt_array(), RT_DUMP(), rt_flush(), rt_key(), rt_number(), rt_reference() (+2 more)

### Community 799 - "tests/test_bootlog"
Cohesion: 0.19
Nodes (7): check_wiring(), check_write_contract(), fn_body(), host_build(), mutate(), apply(), run_cases()

### Community 801 - "tests/test_e9_observation"
Cohesion: 0.29
Nodes (9): body(), client_marker(), lockwrite(), main(), marker(), reuse(), reuse_parent(), run() (+1 more)

### Community 804 - "tests/trident_gap_shell_host"
Cohesion: 0.19
Nodes (6): dispatch(), print(), _start(), test(), shell_cmd_tri(), tri_number()

### Community 805 - "cmds/hclip"
Cohesion: 0.24
Nodes (9): do_get(), do_put(), hclip_sink(), io_close(), io_open_read(), io_read(), io_write(), main() (+1 more)

### Community 806 - "tables/os2 #4"
Cohesion: 0.15
Nodes (10): Width, Condensed, Expanded, ExtraCondensed, ExtraExpanded, Normal, SemiCondensed, SemiExpanded (+2 more)

### Community 807 - "tests/e2test"
Cohesion: 0.45
Nodes (11): check_file_size(), fill_pattern(), main(), read_file(), test1_new_280k(), test2_overwrite_280k(), test3_new_372k(), test4_chunked_372k() (+3 more)

### Community 808 - "tests/clip"
Cohesion: 0.33
Nodes (9): empty_touching_inverted_and_integer_endpoints(), grid(), grid_edges_and_empty_intersections_are_bounded(), grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow(), intersection_clips_each_edge_and_negative_origins(), nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns(), partial_kanji_reads_both_halves_but_never_expands_pixel_permission(), rect() (+1 more)

### Community 809 - "input/kbd_dlog"
Cohesion: 0.26
Nodes (6): kbd_dlog_copy(), kbd_dlog_push(), kbd_dlog_reset(), case_ring_basic(), case_ring_wrap(), push_n()

### Community 810 - "v2/a0_weak #4"
Cohesion: 0.17
Nodes (8): gfx2_adapter_register(), gfx2_adapter_unregister(), gfx2_connector_info(), gfx2_head_info(), gfx2_mode_get(), gfx2_v1_enum_conn(), gfx2_v1_enum_head(), gfx2_v1_enum_mode()

### Community 811 - "tests/cpu_calibrate_host"
Cohesion: 0.38
Nodes (8): cpu_calibrate_compute(), cpu_calibrate_enough(), delay_scale(), fast_cpu(), give_up(), main(), slow_cpu(), spin()

### Community 812 - "tests/paging_gfx2_host"
Cohesion: 0.23
Nodes (6): die(), paging_current_cr3(), paging_load_cr3(), pgalloc_alloc_phys(), pgalloc_free_n_owner(), _start()

### Community 813 - "powermac/display"
Cohesion: 0.18
Nodes (3): display_palette4_write(), display_planar_plane(), gfx_clear_planar_pages()

### Community 814 - "powermac/display_provider"
Cohesion: 0.21
Nodes (4): display_provider_display_first(), display_provider_display_records(), display_provider_ledger(), display_provider_select()

### Community 815 - "ppc/lan_p13_fake"
Cohesion: 0.27
Nodes (8): pm_clock_read(), pm_clock_write(), pm_gmac_clock(), output(), pm_clock_read(), pm_clock_write(), _start(), test()

### Community 816 - "powermac/lan_monitor"
Cohesion: 0.39
Nodes (8): contains(), equal(), execute(), get(), pm_lan_copy8(), pm_lan_monitor(), put(), same_request()

### Community 817 - "powermac/mpic_tree"
Cohesion: 0.56
Nodes (9): address(), cell_at(), contains(), macio_bar(), number(), parent_address(), pm_mpic_resolve(), property() (+1 more)

### Community 818 - "tests/public_surface_host"
Cohesion: 0.26
Nodes (6): gfx_sf_backend(), host_lease_copyin(), host_lease_copyout(), ring3_user_ranges_writable(), system_surface_source(), writable()

### Community 819 - "tools/guest_tests #2"
Cohesion: 0.20
Nodes (3): GuestUnreachable, Np21Guest, cmd()

### Community 820 - "tools/osri_enc2_row"
Cohesion: 0.45
Nodes (4): plan(), RowSearch, partials(), check_rows()

### Community 821 - "tests/fep_boundary_runtime"
Cohesion: 0.27
Nodes (8): exit(), fixture_format(), fixture_print(), fprintf(), malloc(), printf(), snprintf(), _start()

### Community 822 - "tests/gf_command_b0_host"
Cohesion: 0.42
Nodes (10): done(), expect(), fc(), fifo_layout(), fifo_pause(), fifo_stop(), _start(), gf2_fifo_start() (+2 more)

### Community 823 - "tests/gfinit_owner_host"
Cohesion: 0.24
Nodes (5): command_selection_test(), foundation_selection_test(), production_gate_test(), _start(), gfinit_call()

### Community 824 - "tests/gfx2_caps_host"
Cohesion: 0.23
Nodes (8): gfx2_caps_refresh(), backend_caps(), gfx2_adapter_get(), gfx2_sync_get(), gfxsync_profile(), kmemcpy(), probe(), _start()

### Community 825 - "tests/gfx2_owner_release_host"
Cohesion: 0.29
Nodes (9): cases(), gfx2_adapter_get(), gfx2_fence_done(), gfx2_head_restore_v1(), gfx2_surf_quiesce(), gfx2_surf_state_set(), reset(), _start() (+1 more)

### Community 826 - "tests/hostsvc_e82557_host"
Cohesion: 0.21
Nodes (6): e82557_get_mac(), e82557_recv(), e82557_rx_free(), e82557_send(), run(), _start()

### Community 827 - "tests/nic_trace_host"
Cohesion: 0.27
Nodes (7): equal(), kdi_get_tick_count(), kprintf(), kutoa_dec(), _start(), vfs_read(), vfs_write()

### Community 828 - "tests/recover_wire_host"
Cohesion: 0.21
Nodes (8): inp(), irq_restore(), irq_save(), outp(), recover_test_inp(), recover_test_outp(), _start(), wire_tests()

### Community 829 - "tests/sbrk_crt_host"
Cohesion: 0.24
Nodes (8): fake_map(), fake_unmap(), fixture_start(), memset(), report(), reset(), run(), os32_resident_sbrk_stats()

### Community 831 - "tests/trident_core_host"
Cohesion: 0.20
Nodes (4): tri_configure(), tri_init(), tri_prepare(), tri_query()

### Community 832 - "cmds/lpr"
Cohesion: 0.26
Nodes (7): io_close(), io_open_read(), io_read(), lpr_src(), main(), make_name(), usage()

### Community 833 - "cmds/more"
Cohesion: 0.35
Nodes (10): clear_prompt(), display_page(), find_next(), main(), print_line(), read_search_pattern(), show_prompt(), split_lines() (+2 more)

### Community 834 - "ggg/lookup"
Cohesion: 0.17
Nodes (3): Lookup<'a>, LookupFlags, .SIZE

### Community 835 - "src/sqrt"
Cohesion: 0.29
Nodes (11): fast_distance(), fast_distance_sq(), fix16_sqrt(), isqrt(), rs_fast_distance(), rs_fast_distance_sq(), rs_fix16_sqrt(), rs_isqrt() (+3 more)

### Community 836 - "edit/text_search"
Cohesion: 0.45
Nodes (9): cmd_search_next(), match_forward(), match_forward_ci(), te_replace(), te_search_backward(), te_search_backward_ci(), te_search_forward(), te_search_forward_ci() (+1 more)

### Community 837 - "ppc/exec_surface"
Cohesion: 0.25
Nodes (5): exec_surface_at(), exec_surface_backend(), exec_surface_bridge_access(), exec_surface_bridge_begin(), exec_surface_bridge_end()

### Community 838 - "ppc/kboot_clock_fake"
Cohesion: 0.31
Nodes (8): pm_clock_bind(), check_tb_read(), expect(), quit(), _start(), tb_lower(), tb_upper(), platform_clock_read_us()

### Community 839 - "sync/sync_write"
Cohesion: 0.53
Nodes (7): gfxsync_flush(), gfxsync_read_rows(), gfxsync_row_read(), gfxsync_row_write(), gfxsync_write_rows(), pixel_bytes(), rows_check()

### Community 840 - "tests/host_exec_context"
Cohesion: 0.35
Nodes (8): host_arch_priv_restore(), host_arch_priv_save(), host_arch_syscall_result(), host_arch_syscall_slot(), host_arch_user_restart(), host_arch_user_result(), host_arch_user_resume(), host_arch_user_save()

### Community 841 - "kdebug/recover #8"
Cohesion: 0.18
Nodes (11): recover_auth_phase, REC_MODE_C, REC_PREPARE, REC_QUIESCE, REC_RESTORE, REC_ROUTE, REC_RUN, REC_SCREEN (+3 more)

### Community 842 - "kdebug/recover #9"
Cohesion: 0.18
Nodes (11): recover_reason, REC_ABORT, REC_BUS, REC_CONFIG, REC_CONTRACT, REC_LOG, REC_NORMAL, REC_OPERATION (+3 more)

### Community 843 - "tests/vk32_host"
Cohesion: 0.36
Nodes (6): harness_main(), put32(), rd32(), sys3(), sys_exit(), write_all()

### Community 844 - "mm/pgalloc #4"
Cohesion: 0.18
Nodes (10): ledger_device_map, end, first, owner, rid, ledger_span, end, first (+2 more)

### Community 845 - "tests/test_np21w_ctl #6"
Cohesion: 0.27
Nodes (3): parse_probe(), ProbeError, ProbeBroken

### Community 846 - "ppc/test_user_trap_host"
Cohesion: 0.24
Nodes (5): main(), ppc_root_current(), ppc_trap_fatal_sink(), ppc_trap_msr(), _start()

### Community 847 - "tests/gf_copy_case_host"
Cohesion: 0.25
Nodes (5): blit(), clock_now(), map(), _start(), upload()

### Community 848 - "tests/gfx2_line_host"
Cohesion: 0.35
Nodes (7): gfx2_fmt_info(), gfx2_px_read(), gfx2_px_write(), gfxsync_read_rows(), gfxsync_write_rows(), run(), _start()

### Community 849 - "tests/gfx2_rop4_host"
Cohesion: 0.35
Nodes (7): gfx2_fmt_info(), gfx2_px_read(), gfx2_px_write(), gfxsync_read_rows(), gfxsync_write_rows(), probe(), _start()

### Community 850 - "tests/gfx2_sw_rop_host"
Cohesion: 0.35
Nodes (7): gfx2_fmt_info(), gfx2_px_read(), gfx2_px_write(), gfxsync_read_rows(), gfxsync_write_rows(), _start(), test()

### Community 851 - "tests/recover_gf_inventory"
Cohesion: 0.18
Nodes (10): gf_class, REC_GF_COPY, REC_GF_PLL, REC_GF_PLL_UNUSED, REC_GF_RULE, REC_GF_TEMP_SAVED, gf_classification, effect (+2 more)

### Community 852 - "tests/recover_tables_oracle"
Cohesion: 0.36
Nodes (9): command_off(), cr11_unlock(), gf_copy(), gf_copy_nv(), gf_rmw(), gf_shift(), P2V_BOOT(), pmc_enable() (+1 more)

### Community 855 - "tests/test_host_agent #5"
Cohesion: 0.24
Nodes (4): n2fix_b7_real_clip_get_sizes(), n2fix_b7_real_large_clip_get(), n3fix_wait_all_timeout_raises(), RealB64Spawn

### Community 859 - "tests/tri_burst5_host"
Cohesion: 0.42
Nodes (9): access(), check_depth(), file(), has(), output(), point(), seed_for(), survives() (+1 more)

### Community 860 - "ggg/lookup #2"
Cohesion: 0.24
Nodes (4): LookupSubtables<'a>, LookupSubtablesIter, LookupSubtablesIter<'a, T>, parse_extension_lookup()

### Community 861 - "src/doc #2"
Cohesion: 0.18
Nodes (10): ERR_IO, LINE_CAP, MAX_LINES, MAX_VIEW_ROWS, O_CREAT, O_RDONLY, O_TRUNC, O_WRONLY (+2 more)

### Community 862 - "src/lib #12"
Cohesion: 0.25
Nodes (11): set_clip_get(), clip_get(), h06_clip_get_cap_exceeded_written_lt_total(), h07_clip_get_stops_at_nul(), h08_clip_get_utf8_boundary(), h09_clip_get_cap0_or_null_is_einval(), h10_clip_get_svc_passthrough_and_error(), h20b_clip_get_counts_past_cap_across_chunks() (+3 more)

### Community 863 - "ppc/exec_font_lease"
Cohesion: 0.33
Nodes (4): exec_font_lease_shell_close(), exec_font_lease_shell_open(), exec_font_lease_shell_read_at(), exec_font_lease_shell_validate()

### Community 865 - "v2/gart"
Cohesion: 0.24
Nodes (8): ledger_transfer_pinned(), pgalloc_pin(), pgalloc_pinned(), pgalloc_unpin(), gfx2_gart_alloc(), gfx2_gart_free(), gfx2_gart_pfns(), gfx2_gart_quarantine()

### Community 866 - "workflows/build"
Cohesion: 0.24
Nodes (9): concurrency, concurrency, concurrency, cancel-in-progress, group, name, permissions, actions (+1 more)

### Community 867 - "mm/pgalloc #5"
Cohesion: 0.20
Nodes (8): ledger_owner, alloc_irq, free_irq, id, kind, name, pad, pages

### Community 868 - "mm/pgalloc #6"
Cohesion: 0.20
Nodes (10): ledger_region, cache, end, first, flags, owner, pad, res_mask (+2 more)

### Community 869 - "pc98/gfx_selector"
Cohesion: 0.27
Nodes (3): display_provider_ledger(), display_provider_select(), gfx_probe_backend()

### Community 870 - "tools/check_gui_proto"
Cohesion: 0.36
Nodes (6): c_eval(), layout(), main(), parse_c(), parse_rs(), strip_comments()

### Community 871 - "ppc/kboot_mmu_guest"
Cohesion: 0.53
Nodes (8): check(), expect(), msr(), pm_mmu_start(), ppc_boot_load_root(), ppc_exception(), read_alias(), say()

### Community 872 - "tests/gf_v2_enum_host"
Cohesion: 0.29
Nodes (5): gf4_ddc_probe(), gf_v2_deadline(), read_edid(), _start(), test()

### Community 873 - "tests/gfx2_hostdata_host"
Cohesion: 0.33
Nodes (6): gfx2_fmt_info(), gfx2_px_read(), gfx2_px_write(), gfxsync_write_rows(), probe(), _start()

### Community 874 - "host_arch/arch_io"
Cohesion: 0.31
Nodes (6): _disable(), _enable(), _halt(), _idle(), irq_save(), _stop()

### Community 875 - "kernel_shell_arch/arch_io"
Cohesion: 0.31
Nodes (6): _disable(), _enable(), _halt(), _idle(), irq_save(), _stop()

### Community 876 - "tests/ls_client_host"
Cohesion: 0.24
Nodes (5): window(), cb(), finish(), nested_cb(), _start()

### Community 877 - "tests/test_mk_settings_db #4"
Cohesion: 0.29
Nodes (3): BuildWiring, mk_prereqs(), read_mk()

### Community 878 - "tests/tri_burst4_host"
Cohesion: 0.42
Nodes (8): access(), check_depth(), file(), has(), output(), point(), survives(), value()

### Community 879 - "cmds/v86"
Cohesion: 0.40
Nodes (7): do_gcap(), gcap_exit_name(), gcap_layout_name(), gcap_phase_name(), gcap_status_name(), main(), usage()

### Community 880 - "cmds/wget"
Cohesion: 0.31
Nodes (6): io_close(), io_open_write(), io_write(), main(), usage(), wget_sink()

### Community 881 - "tables/ankr"
Cohesion: 0.20
Nodes (5): Point, .SIZE, Stream<'a>, StreamExt, Table<'a>

### Community 882 - "src/clip"
Cohesion: 0.33
Nodes (7): Clip, ClipError, InvalidGrid, InvalidRect, Overflow, Grid, Rect

### Community 884 - "src/atan2"
Cohesion: 0.36
Nodes (9): angle_between(), CORDIC_ATAN, iatan2(), rs_angle_between(), rs_iatan2(), test_angle_between(), test_iatan2_axes(), test_iatan2_diagonal() (+1 more)

### Community 885 - "tests/faulttest"
Cohesion: 0.47
Nodes (8): do_fault(), do_kloop(), do_loop(), main(), parse_uint(), spin(), streq(), usage()

### Community 886 - "tests/gui_busy"
Cohesion: 0.47
Nodes (6): arg_is_nokapi(), calibrate_chunk(), crunch(), main(), make_window(), report()

### Community 887 - "tests/pcm_test"
Cohesion: 0.47
Nodes (5): fill_chunk(), main(), run_stream(), show_status(), sine_init()

### Community 888 - "geforce/gf_v2_life"
Cohesion: 0.33
Nodes (5): gf_v2_deadline(), gf_v2_disp(), gf_v2_start(), gf_v2_stop(), gf3_range_query_set()

### Community 889 - "kdebug/recover #10"
Cohesion: 0.22
Nodes (9): recover_identity, bar0, bdf, boot, device, image_version, kdebug_version, revision (+1 more)

### Community 890 - "kdebug/recover #11"
Cohesion: 0.22
Nodes (9): recover_state, REC_ARMED, REC_FAILED, REC_IDLE, REC_PAUSED, REC_RESTORING, REC_RUNNING, REC_SNAPSHOTTING (+1 more)

### Community 891 - "kdebug/recover #12"
Cohesion: 0.22
Nodes (9): recover_target, bar, head, index, offset, port, read_safe, space (+1 more)

### Community 892 - "kdebug/recover #13"
Cohesion: 0.22
Nodes (9): recover_window, failed, flags, opaque, recover_window_ops, owned, pages, raw (+1 more)

### Community 894 - "ppc/test_lan_p8_fake"
Cohesion: 0.56
Nodes (7): emit(), event(), pm_pci_mmio_read32(), pm_pci_mmio_write32(), ppc_irq_restore(), ppc_irq_save(), reg_for()

### Community 895 - "hostdrv_hostshim/io"
Cohesion: 0.28
Nodes (6): hdrv_shim_inp(), hdrv_shim_outp(), inp(), inpw(), outp(), outpw()

### Community 896 - "dep_dir_c/manifest #4"
Cohesion: 0.22
Nodes (9): include/memmap.h, mm/memory_boot.c, mm/pgalloc.h, pin_metadata_update, base_sha, evidence, harness_sha256, oracle_source_sha256 (+1 more)

### Community 897 - "tests/gf_foundation_width_host"
Cohesion: 0.47
Nodes (6): __asan_loadN_noabort(), __asan_storeN_noabort(), foundation_width_cases(), width_begin(), width_end(), width_record()

### Community 898 - "tests/gf_v2_rop3_host"
Cohesion: 0.31
Nodes (4): common(), die(), gf2_emit(), _start()

### Community 899 - "tests/gfx2_copy_host"
Cohesion: 0.36
Nodes (5): gfx2_fmt_info(), gfxsync_read_rows(), gfxsync_write_rows(), probe(), _start()

### Community 900 - "tests/gfx2_mono_host"
Cohesion: 0.33
Nodes (5): finish(), gfx2_fmt_info(), gfx2_px_write(), gfxsync_write_rows(), _start()

### Community 901 - "tests/gfx2_sw_fill_host"
Cohesion: 0.33
Nodes (5): gfx2_fmt_info(), gfx2_px_write(), gfxsync_write_rows(), _start(), test()

### Community 904 - "tests/tri_burst3_host"
Cohesion: 0.39
Nodes (7): access(), file(), has(), output(), point(), survives(), value()

### Community 905 - "tests/x87_lifecycle_host #2"
Cohesion: 0.22
Nodes (8): expect(), kprintf(), resident_main(), say(), shell_print(), shell_print_dec(), _start(), user_main()

### Community 906 - "userland/deploy"
Cohesion: 0.22
Nodes (8): exclude, guest, host, host_only, tags, type, filesystem, files

### Community 907 - "src/paths"
Cohesion: 0.22
Nodes (8): FONT_DIR, FONT_JA, FONT_JA_C, FONT_JA_G16, FONT_JA_G16_C, FONT_LATIN, FONT_LATIN_C, font_dir!

### Community 911 - "e82557/e82557_desc #2"
Cohesion: 0.25
Nodes (7): e82557_rx, cons, dev, ready, e82557_stop, reset_seq, start_seq

### Community 912 - "hostdrv/hostdrvfs #2"
Cohesion: 0.29
Nodes (5): hdrv_list_emit(), session_set_path(), setup_create(), kutf16le_to_utf8(), kutf8_to_utf16le()

### Community 913 - "workflows/build #2"
Cohesion: 0.43
Nodes (8): steps, steps, id, if, name, run, timeout-minutes, uses

### Community 914 - "workflows/build #3"
Cohesion: 0.25
Nodes (8): with, fetch-depth, if-no-files-found, key, name, path, restore-keys, retention-days

### Community 915 - "kdebug/recover #14"
Cohesion: 0.25
Nodes (8): recover_reg, key, kind, read_safe, restore_rule_id, verify_mask, width, write_mask

### Community 916 - "tests/pit_clock_host"
Cohesion: 0.54
Nodes (7): pit_compute(), both_clocks(), main(), no_overflow(), real_hw_story(), reject_clock(), reject_hz()

### Community 917 - "g16/scan_ui_chars"
Cohesion: 0.39
Nodes (5): full_path(), literals(), main(), render(), scan()

### Community 919 - "tests/gf_v2_fence_host"
Cohesion: 0.32
Nodes (4): die(), gf2_fence_emit(), gf2_fence_wait(), _start()

### Community 920 - "tests/gfx2_readback_host"
Cohesion: 0.32
Nodes (4): gfx2_fmt_info(), gfxsync_read_rows(), _start(), test()

### Community 921 - "tests/gfx2_sysmem_host"
Cohesion: 0.39
Nodes (6): kmemset(), pgalloc_alloc_n_owner(), pgalloc_arena_end(), pgalloc_free_n_owner(), _start(), test()

### Community 924 - "tests/test_ext2_write_batch"
Cohesion: 0.39
Nodes (5): media_check(), claim(), inode_at(), u32(), visit()

### Community 929 - "tables/morx #2"
Cohesion: 0.25
Nodes (3): Chains<'a>, Subtables<'a>, Table<'a>

### Community 930 - "apps/package_defs"
Cohesion: 0.38
Nodes (6): guest, host, full, files, normal, files

### Community 933 - "tests/multiapp_impl_host #3"
Cohesion: 0.33
Nodes (7): appslot_kill_check(), host_vfs_close_owned(), ma_kill(), ma_kill_chain(), ma_reclaim_res(), vfs_close(), appslot_gfx_owner_exit()

### Community 934 - "v2/sw_rop4"
Cohesion: 0.62
Nodes (4): gfx2_sw_rop4(), rop3(), row_read(), row_write()

### Community 935 - "workflows/build #4"
Cohesion: 0.29
Nodes (7): runs-on, strategy, timeout-minutes, check-fast, shard, fail-fast, matrix

### Community 936 - "workflows/build #5"
Cohesion: 0.29
Nodes (7): CI_WORKFLOW, GH_TOKEN, OS32_ACCEPT_IPA_LICENSE, OS32_CHECK_CACHE, OS32_CLANG_CROSS_FREE, SEGMENT_DOWNLOAD_TIMEOUT_MINS, env

### Community 937 - "kdebug/recover #15"
Cohesion: 0.29
Nodes (7): recover_bus_access, bar, dir_substep, key, offset, space, width

### Community 938 - "kdebug/recover #16"
Cohesion: 0.29
Nodes (7): recover_guard, expected, key, mask, recover_guard_when, REC_AFTER, REC_BEFORE

### Community 939 - "kdebug/recover #17"
Cohesion: 0.29
Nodes (7): recover_opcode_layout, argc, dir_substep, flags, kinds, opcode, substep

### Community 940 - "kdebug/recover #18"
Cohesion: 0.29
Nodes (7): recover_static_work, bytes, points, s0_bytes, s0_points, s0_wait_ms, wait_ms

### Community 941 - "kdebug/recover #19"
Cohesion: 0.29
Nodes (7): recover_token, identity, plan_hash, profile_crc, profile_version, session, snapshot_crc

### Community 942 - "mm/pgalloc #7"
Cohesion: 0.29
Nodes (7): pgalloc_layout, capacity, kind, metadata, metadata_first, workspace_end, workspace_first

### Community 943 - "tests/splash_wait_host"
Cohesion: 0.33
Nodes (3): finish(), _start(), sys_time_now()

### Community 944 - "ci/build_cross"
Cohesion: 0.52
Nodes (6): fail(), fetch(), log(), PATH, build_cross.sh script, usage()

### Community 946 - "tests/gui_reattach_host #2"
Cohesion: 0.29
Nodes (6): gfx_flush(), gfx_present(), gui_request(), libos32gfx_attach_checked(), libos32gfx_check(), libos32gfx_take_regen()

### Community 947 - "pegc_hostshim/io"
Cohesion: 0.33
Nodes (4): inp(), io_wait(), outp(), pegc_shim_outp()

### Community 948 - "tests/test_harness_tools"
Cohesion: 0.43
Nodes (5): changes_case(), load(), main(), records_case(), write()

### Community 950 - "cmds/sort"
Cohesion: 0.48
Nodes (5): compare_lines(), main(), parse_lines(), shell_sort(), str_to_long()

### Community 951 - "src/recip"
Cohesion: 0.48
Nodes (6): fast_div(), RECIP_TABLE, rs_fast_div(), test_fast_div_basic(), test_fast_div_boundary(), test_fast_div_fallback()

### Community 952 - "tests/gfx2_i3"
Cohesion: 0.52
Nodes (3): exercise(), fail(), main()

### Community 953 - "tests/mouse_test"
Cohesion: 0.48
Nodes (4): btn_str(), itoa_pad(), main(), tv_print()

### Community 954 - "tests/pegcchk"
Cohesion: 0.52
Nodes (4): draw_pattern(), hline(), main(), vline()

### Community 955 - "apps/deploy"
Cohesion: 0.33
Nodes (5): guest, host, tags, filesystem, files

### Community 958 - "sqlite3/sqlite3 #4"
Cohesion: 0.67
Nodes (4): winceLockFile(), winceUnlockFile(), winLockFile(), winUnlockFile()

### Community 960 - ".mcp"
Cohesion: 0.47
Nodes (5): FLM_MODEL, python3, autoplay, emu_agent, np21w

### Community 961 - "game/deploy"
Cohesion: 0.33
Nodes (5): guest, host, tags, filesystem, files

### Community 962 - "v2/owner_release"
Cohesion: 0.47
Nodes (3): gfx2_owner_release(), gfx2_surf_quarantine(), head_owned()

### Community 963 - "workflows/build #6"
Cohesion: 0.33
Nodes (6): on, push, workflow_dispatch, branches, paths-ignore, tags

### Community 964 - "kdebug/recover #20"
Cohesion: 0.33
Nodes (6): recover_capture, consumer_node, producer_node, read_ordinal, slot, target

### Community 965 - "kdebug/recover #21"
Cohesion: 0.33
Nodes (6): recover_local_step, dir, key, role, value, width

### Community 966 - "kdebug/recover #22"
Cohesion: 0.33
Nodes (6): recover_write, key, rule_id, value, verify_mask, width

### Community 967 - "src/generations"
Cohesion: 0.33
Nodes (5): OS32_KAPI_ABI_GENERATION, OS32_MEMORY_LAYOUT_GENERATION, OS32_SHLIB_PROTOCOL, OS32X_HDR_VERSION, OS32X_MIN_API

### Community 970 - "ppc/kboot_pdwalk_host"
Cohesion: 0.47
Nodes (4): exit_code(), main(), read_word(), _start()

### Community 971 - "tests/gf1_engine_stop_host"
Cohesion: 0.47
Nodes (4): read_reg(), _start(), test(), write_reg()

### Community 972 - "tests/gf_v2_alloc_host"
Cohesion: 0.40
Nodes (3): gf_vram_reserve(), _start(), test()

### Community 973 - "tests/gfx2_heap_host"
Cohesion: 0.53
Nodes (5): gfx2_heap_init(), probe(), same(), save(), _start()

### Community 975 - "tests/test_gfinit_mode"
Cohesion: 0.53
Nodes (5): expected_trace(), cr(), m(), sr(), w()

### Community 976 - "tests/test_host_agent #6"
Cohesion: 0.40
Nodes (3): n3fix_get_child_real_http(), do_GET(), _send()

### Community 979 - "tables/os2 #6"
Cohesion: 0.33
Nodes (5): Permissions, Editable, Installable, PreviewAndPrint, Restricted

### Community 980 - "tests/gfx2_i1"
Cohesion: 0.93
Nodes (4): child(), exercise(), failed(), main()

### Community 983 - "v2/vram_heap"
Cohesion: 0.60
Nodes (3): gfx2_heap_alloc(), gfx2_heap_free(), gfx2_heap_init()

### Community 985 - "kdebug/recover #23"
Cohesion: 0.40
Nodes (5): recover_transport, frame_us, max_polls, opaque, uart

### Community 986 - "sqlite3/sqlite3 #5"
Cohesion: 0.40
Nodes (5): sqlite3_index_constraint, iColumn, iTermOffset, op, usable

### Community 990 - "tools/jobs"
Cohesion: 0.70
Nodes (4): record(), jobs.sh script, usage(), valid_label()

### Community 992 - "ppc/build_cross"
Cohesion: 0.60
Nodes (4): fetch(), log(), PATH, build_cross.sh script

### Community 994 - "tests/widget_scroll_host #3"
Cohesion: 0.40
Nodes (5): GuiErr, .FULL, .INVAL, .NOSYS, .STALE

### Community 997 - "tables/os2 #7"
Cohesion: 0.40
Nodes (4): Style, Italic, Normal, Oblique

### Community 1001 - "e82557/e82557_desc #3"
Cohesion: 0.50
Nodes (4): e82557_wait, E82557_DONE, E82557_TIMEOUT, E82557_WAIT

### Community 1003 - "workflows/build #7"
Cohesion: 0.50
Nodes (4): runs-on, timeout-minutes, jobs, artifacts

### Community 1004 - "tools/create_fat12_d88"
Cohesion: 0.50
Nodes (3): MTOOLS_FAT_COMPATIBILITY, MTOOLS_SKIP_CHECK, create_fat12_d88.sh script

### Community 1005 - "ppc/known_header_dependencies"
Cohesion: 0.50
Nodes (3): dependencies, source_profile, target

### Community 1006 - "tools/preflight"
Cohesion: 0.83
Nodes (3): ng(), ok(), preflight.sh script

### Community 1022 - "gui/types #2"
Cohesion: 0.67
Nodes (3): FontId, .ANK, .KANJI

### Community 1026 - "tests/gui_reattach_host #4"
Cohesion: 0.67
Nodes (3): present(), return_gate_and_painter(), os32gui_shlib_init()

## Knowledge Gaps
- **1305 isolated node(s):** `FLM_MODEL`, `reset_seq`, `start_seq`, `E82557_WAIT`, `E82557_DONE` (+1300 more)
  These have ≤1 connection - possible missing edges. (Counts symbols only; 12968 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **348 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `KernelAPI` connect `src/lib #3` to `tests/trim_focus_host`, `tests/gui_reattach_host #4`, `src/taskbar`, `src/lib #10`, `Rust libos32gui B`, `Rust Filer & About Apps`, `src/lib #4`, `Rust GUI Stub & Demo`, `src/guest`, `Rust Filer`, `src/lib #6`, `tests/gui_trim_host`, `Rust os32api SDK`, `src/lib #11`, `src/lib #2`?**
  _High betweenness centrality (0.022) - this node is a cross-community bridge._
- **What connects `FLM_MODEL`, `reset_seq`, `start_seq` to the rest of the system?**
  _1305 weakly-connected nodes found - possible missing edges._
- **Should `Host Test Harness (host32)` be split into smaller, more focused modules?**
  _Cohesion score 0.005201190322163281 - nodes in this community are weakly interconnected._
- **Why does `Stream` connect `Font Library A` to `tables/gsub`, `cff/dict`, `tables/name`, `tables/os2`, `tables/colr`, `tables/gpos`, `ggg/mod`, `cff/cff2`, `cff/cff1`, `src/aat`, `src/lib`, `tables/gvar`, `tables/loca`, `tables/kern`, `tables/glyf`, `tables/math #2`?**
  _High betweenness centrality (0.020) - this node is a cross-community bridge._
- **Should `SQLite Core API` be split into smaller, more focused modules?**
  _Cohesion score 0.004232689603431087 - nodes in this community are weakly interconnected._
- **Why does `LazyArray16` connect `Font Library A` to `tables/gsub`, `tables/name`, `tables/colr`, `tables/feat`, `tables/gpos`, `ggg/mod`, `tables/math`, `cff/cff1`, `src/aat`, `src/lib`, `tables/gvar`, `tables/loca`, `tables/kern`, `src/parser`, `tables/glyf`, `tables/math #2`?**
  _High betweenness centrality (0.019) - this node is a cross-community bridge._
- **Should `SQLite Internals A` be split into smaller, more focused modules?**
  _Cohesion score 0.012387555797249225 - nodes in this community are weakly interconnected._