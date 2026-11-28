"""Exercise the production shell loader in QEMU user mode on both architectures."""
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, str(Path.cwd()))
from tests.support.product import recipes

# User mode cannot touch hardware. Unlisted dependencies trap when called, so
# the fixture cannot silently accept a new side effect in the loader.
NO_OP = set("""
_enable _irq_enabled appslot_gui_draw_leave arch_entry_prepare arch_exec_publish arch_fp_init
arch_fp_restore arch_fp_save arch_priv_restore arch_priv_save
in2_on_focus caller_access_invalidate caller_access_leave caller_access_restore
caller_access_save con_sink_is_enabled con_sink_owner_exit db_cleanup_owned
display_provider_owner_exit exec_db_cleanup_owned exec_font_guard_active
exec_font_lease_close_owned exec_font_lease_close_shell exec_gfx_owner_exit
exec_gfx_return_emergency exec_gui_owner_exit exec_heap_init_at exec_heap_restore_state
exec_host_owner_exit exec_ime_owner_exit exec_legacy_abort_begin exec_legacy_cleanup
exec_legacy_closing exec_legacy_end exec_legacy_lowmap_busy exec_legacy_pinned exec_setjmp
exec_surface_policy_reeval font_lease_close_owned font_lease_close_shell gf_display_owner_exit
gfx_policy_reeval gfx_return_emergency gui_owner_exit host_owner_exit ime_owner_exit
in2_reclaim input_discard_stop install_reclaim_owner io_kapi_owner_exit io_syscall_resume
io_syscall_suspend kapi_strings_cleanup_owned kbd_inject_owner_exit launch_owner_exit
legacy_abort_begin legacy_cleanup legacy_closing legacy_end legacy_lowmap_busy legacy_pinned
paging_current_cr3 paging_kernel_pd_phys paging_load_cr3 pipe_free_owned
io_policy_owner_exit in2_excl_drop_raw input_legacy_provider appmem_init paging_addrspace_map_user_range_phys paging_addrspace_protect_code_ro
kselftest_run_audit serial_puts_polled gfx2_owner_release exec_selftest_run_audit exec_shlib_data_pages shlib_data_pages exec_shlib_addrspace_attach
shlib_addrspace_attach ledger_owner_pages lease_revoke_all exec_gfx_owner_release gf_owner_release
exec_shlib_addrspace_detach shlib_addrspace_detach paging_addrspace_free_user_range
paging_addrspace_destroy ledger_reclaim_owner ledger_owner_retire kfree exec_serial_puts_polled
sfs_persistent_scope_owner_exit shell_svc_font_guard_run shell_svc_font_source_close
shell_svc_font_source_open shell_svc_font_source_read_at shell_svc_font_source_validate
shm_free_owned snd2_chip_reclaim snd2_reclaim vfs_close vfs_close_owned vfs_open vfs_seek
""".split())


def run(config, folder):
    ppc = config == "link-sh"
    prefix = "powerpc" if ppc else "i386"
    cross = os.environ["PPC_CROSS_DIR" if ppc else "CROSS_DIR"]
    tool = Path(cross) / "bin"
    cc = tool / (prefix + "-elf-gcc")
    build_recipes = recipes(config)[0].values()
    recipe = next(cmd for source, cmd in build_recipes
                  if source == "sys/kern/exec/exec.c")
    fixture = Path("tests/support/fixtures/input_policy")
    flags = ["-I" + str(fixture), "-I.", "-ffunction-sections", "-fdata-sections",
             "-ffreestanding", "-fno-pie", "-fno-builtin", "-std=gnu11", "-O1", "-D__cdecl="]
    flags.extend(arg for arg in recipe if arg.startswith(("-I", "-D")))
    if ppc:
        flags.extend(["-msdata=none", "-msoft-float"])

    sources = [fixture / "loader.c", fixture / "jump.S", fixture / "entry.S",
               "sys/kern/exec/ring3_str.c", "sys/kern/av_focus.c", "sys/kern/init/shell_loop.c", "sys/kern/exec/appslot.c", "sys/kern/exec/os32x_hdr.c",
               "sys/kern/kstring_c.c", "sys/fs/fd_redirect.c", "sys/kern/exec/shell_service.c",
               "sys/input/input_context.c", "sys/input/input_interrupt.c", "sys/input/input_ime_gate.c",
               "sys/hal/input/input_work.c", "sys/input/input_delivery.c", "sys/input/input_policy.c", "sys/kapi/input_policy_provider.c",
               "sys/kern/exec/launch.c", "sys/kern/io_kapi.c", "sys/hal/input/input_service.c",
               "sys/hal/input/in2_excl.c", "sys/hal/input/in2_reclaim.c",
               "sys/hal/input/legacy_ring.c", "sys/hal/input/legacy_mode.c",
               "sys/hal/input/keymap_registry.c", "sys/hal/input/keymap_eval.c",
               "sys/input/keymaps/hid_jp.c", "sys/input/keymaps/pc98_jp.c"]
    generated, wrapper_recipe = next((source, cmd) for source, cmd in build_recipes
                                      if Path(source).name == "kapi_generated.c")
    wrapper = folder / (prefix + "-kapi.o")
    subprocess.run([cc, *flags, *(arg for arg in wrapper_recipe[1:] if arg.startswith(("-I", "-D"))),
                    "-c", generated, "-o", wrapper], check=True)
    # Compile the machine's actual launch table. PC-98 keeps its entry local.
    machine_source = ("sys/platform/powermac/shell_loop_ops.c" if ppc else
                      "sys/platform/pc98/kernel.c")
    machine_recipe = next(cmd for source, cmd in build_recipes if source == machine_source)
    machine = folder / (prefix + "-machine.o")
    machine_flags = [arg for arg in machine_recipe[1:] if arg.startswith(("-I", "-D", "-m"))]
    bridge = '#include "' + machine_source + '"\n'
    if ppc:
        bridge += 'void probe_machine(void) { shell_loop_run(&pm_shell_loop_ops); }\n'
    else:
        bridge += 'void probe_machine(void) { kernel_shell_loop(); }\n'
        bridge += 'void probe_fdd(void) { kernel_shell_fdd_fallback(0); }\n'
    subprocess.run([cc, "-I.", *machine_flags, "-O1", "-ffunction-sections", "-fdata-sections",
                    "-ffreestanding", "-x", "c", "-c",
                    "-", "-o", machine], input=bridge, text=True, check=True)
    objects = [wrapper, machine]
    for source in sources:
        obj = folder / (prefix + "-" + Path(source).stem + ".o")
        subprocess.run([cc, *flags, "-c", source, "-o", obj], check=True)
        objects.append(obj)

    combined = folder / (prefix + "-all.o")
    subprocess.run([tool / (prefix + "-elf-ld"), "-r", "--wrap=shell_loop_run", *objects, "-o", combined], check=True)
    undefined = subprocess.check_output([tool / (prefix + "-elf-nm"), "-u", combined], text=True)
    stubs = folder / (prefix + "-stubs.S")
    with stubs.open("w") as out:
        for line in undefined.splitlines():
            name = line.split()[-1]
            if name in ("setjmp", "longjmp") or name.startswith(("_restgpr", "_savegpr")):
                continue
            body = "trap" if ppc else "ud2"
            if name in NO_OP:
                body = "li 3,0\nblr" if ppc else "xor %eax,%eax\nret"
            out.write(f".global {name}\n{name}: {body}\n")

    exe = folder / (prefix + "-loader")
    subprocess.run([cc, "-nostdlib", "-static", "-Wl,--gc-sections",
                    "-Wl,--section-start=.probe_shell=0x400000", "-Wl,--undefined=shell_area",
                    combined, stubs, "-lc", "-lgcc", "-o", exe], check=True)
    return subprocess.run(["qemu-ppc" if ppc else "qemu-i386", exe]).returncode


if __name__ == "__main__":
    sys.exit(run(sys.argv[1], Path(sys.argv[2])))
