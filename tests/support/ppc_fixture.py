"""Build mac99 diagnostic media from the current verification tree."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

from tests.support.media import PPC_MAKE

FIXTURE = Path("tests/support/fixtures/ppc_ata")
ATA = Path("sys/dev/ata")
PM = Path("sys/platform/powermac")


def edit(source, old, new):
    if source.count(old) != 1:
        raise OSError(f"expected one instrumentation site for {old!r}")
    return source.replace(old, new)


def diagnostic_media(case):
    directory = Path(os.environ["OS32_VERIFY_OUT"]).resolve() / case
    directory.mkdir()
    user = directory / "user"
    staging = directory / "tmp"
    staging.mkdir()
    unicode_bin = directory / "unicode.bin"
    manifest = json.loads(Path("build/ppc-ram-root.json").read_text())
    manifest["/sys/unicode.bin"] = str(unicode_bin)
    template = directory / "root-template.json"
    template.write_text(json.dumps(manifest))
    args = ["make", "-j" + os.getenv("OS32_BUILD_JOBS", "8"), *PPC_MAKE,
            "IMAGE=kboot-sh", f"KB_OUT={directory}", f"KB_SH_OUT={user}",
            f"BUILD_OUT={directory / 'build'}", f"KB_BODY_OUT={directory.parent / 'sqlite-body'}",
            f"ROOT_MANIFEST={template}", f"PPC_UNICODE={unicode_bin}"]
    copies = {}

    def substitute(slot, original, filename, source):
        target = directory / filename
        target.write_text(source)
        copies[filename] = hashlib.sha256(target.read_bytes()).hexdigest()
        args.extend([f"{slot}_SRC={original}", f"{slot}_FILE={target}"])

    driver = ATA / "ide.c"
    source = driver.read_text()
    if case == "read":
        site = "static int ide_wait_bsy(u32 limit_ms)"
        source = edit(source, site, "static unsigned probing, polls;\n" + site)
        source = edit(source, "u8 st = ide_bus_alt();",
                      "u8 st = probing ? (++polls == 1 ? IDE_ST_BSY : 0) : ide_bus_alt();")
        source += r"""
void ata_wait_probe(void)
{
    probing = 1;
    int rc = ide_wait_bsy(IDE_IO_TIMEOUT_MS);
    probing = 0;
    kprintf(7, "ATA wait reads=%u rc=%d\r\n", polls, rc);
}
"""
        boot = PM / "ide_boot.c"
        report = (FIXTURE / "read.inc").read_text()
        adapter = edit(boot.read_text(), "void pm_ide_boot", report + "\nvoid pm_ide_boot")
        adapter = edit(adapter, "    ide_init();", r'''    ata_wait_probe();
    kprintf(7, "ATA pa=%08x\r\n", ata_first_pa);
    ide_init();
    ata_read_report();''')
        tree = PM / "ide_tree.c"
        walk = "walk(ofw_child(root), root, 0, 0, &left, &bank, pa);"
        capture = edit(tree.read_text(), "void pm_ide_capture", "u32 ata_first_pa;\nvoid pm_ide_capture")
        substitute("KB_MUT_THIRD", tree, "tree.c", edit(capture, walk, walk + "\n    ata_first_pa = pa[0];"))
    else:
        pio = (ATA / "ide_pio.inc").read_text()
        site = "    ide_bus_out(IDE_REG_STATUS, multiple"
        target = directory / "ide_pio.inc"
        target.write_text(edit(pio, site, "    if (src) ++ata_commands;\n" + site))
        copies[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        source = "unsigned ata_commands;\n" + source
        boot = PM / "ppc_kernel.c"
        report = (FIXTURE / "write.inc").read_text()
        adapter = edit(boot.read_text(), "void platform_boot_run", report + "\nvoid platform_boot_run")
        adapter = edit(adapter, "        pm_exec_boot();", r'''        pm_exec_boot();
        kprintf(7, "ATA shell=%d\r\n", exec_run("/sys/shell.bin"));
        kprintf(7, "ATA done\r\n");
        for (;;) { /* Leave the disk at the diagnostic completion point. */ }''')
        device = Path("sys/dev/ide/device_ide.c")
        provider = edit(device.read_text(), "(void)ide_init();",
                        f"extern void ata_write_report(int);\n    ata_write_report({int(case == 'deny')});")
        for name in ("ide_write_sector", "ide_write_sectors"):
            site = "    return " + name + "("
            provider = edit(provider, site, "    ++ata_provider_calls;\n" + site)
        substitute("KB_MUT_THIRD", device, "device.c", "extern unsigned ata_provider_calls;\n" + provider)
        main = Path("userland/shell/main.c")
        site = "int main(int argc, char **argv, KernelAPI *api)\n{"
        substitute("PPC_RES_MUT", main, "launcher.c", edit(main.read_text(), site,
                   site + "\n    api->ide_init();\n    return 0;"))
        if case == "deny":
            args.append(f"KB_ROOT_BINS={user}/atadeny.bin")
    substitute("KB_MUT", driver, "ide-meter.c", source)
    substitute("KB_MUT_EXTRA", boot, "boot.c", adapter)
    args.extend(["all", "iso"])
    subprocess.run(args, check=True, env={**os.environ, "TMPDIR": str(staging)})
    iso = directory / "boot.iso"
    return iso, dict(make=args, fixture=copies, **{
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (iso, directory / "PPCKERN.ELF")})


def clock_media():
    """Rebuild core with both observation sites from this candidate."""
    directory = Path(os.environ["OS32_VERIFY_OUT"]).resolve() / "core"
    staging = directory / "tmp"
    staging.mkdir(parents=True)
    args = ["make", "-j" + os.getenv("OS32_BUILD_JOBS", "8"), *PPC_MAKE,
            "KBOOT_LINK=0", f"KB_OUT={directory}", f"KB_SH_OUT={directory / 'user'}",
            f"BUILD_OUT={directory / 'build'}"]
    hashes = {}
    for slot, original in (("KB_MUT", Path("sys/arch/ppc/kernel_dec.c")),
                           ("KB_MUT_EXTRA", PM / "ppc_kernel.c")):
        target = directory / original.name
        target.write_bytes(original.read_bytes())
        hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        args.extend([f"{slot}_SRC={original}", f"{slot}_FILE={target}"])
    args.extend(["all", "iso"])
    subprocess.run(args, check=True, env={**os.environ, "TMPDIR": str(staging)})
    iso = directory / "boot.iso"
    for path in (iso, directory / "PPCKERN.ELF"):
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return iso, dict(make=args, hashes=hashes)


def meter_rpc(meter):
    link = Path("sys/net/link.c")
    trace = Path("tests/support/fixtures/ppc_link/rpc.inc").read_text()
    source = edit(link.read_text(), "static struct link_handle link_h[LINK_HANDLES];",
                  "static struct link_handle link_h[LINK_HANDLES];\n" + trace)
    for marker, rid, field in (("e->w_due = 0;\n            e->last_tx_tick = link_platform_now();",
                                "e->rid", "wdata"),
                               ("e->rel_tick = link_platform_now();", "e->rel_rid", "release")):
        tick = "e->last_tx_tick" if field == "wdata" else "e->rel_tick"
        source = edit(source, marker, marker + f"\n                rpc_sent({rid}, {tick}, RPC_{field.upper()});")
    meter("KB_MUT", link, source)

    io = PM / "hostfs_link_io.c"
    source = edit(io.read_text(), "static int phase;", """static int phase;
extern void rpc_opened(int handle, u32 deadline);
extern void rpc_expired(int handle, u32 now);""")
    source = edit(source, "handle = rc;", "handle = rc;\n        rpc_opened(handle, until);")
    source = edit(source, "if ((i32)(pm_hfs_now(0) - until) >= 0) {",
                  """u32 now = pm_hfs_now(0);
    if ((i32)(now - until) >= 0) {
        if (handle >= 0) rpc_expired(handle, now);""")
    meter("KB_MUT_EXTRA", io, source)


def meter_capture(meter):
    sink = Path("sys/kern/console/con_sink.c")
    prefix, body = sink.read_text().split("int con_sink_capture(unsigned int op)")
    body, suffix = body.split("int con_sink_capture_active", 1)
    body = edit(body, "    con_sink_unlock(f);", """    if (op == CON_SINK_CAPTURE_FREEZE) p3_freeze_rc = rc;
    if (op == CON_SINK_CAPTURE_END && rc == 0) ++p3_end_count;
    con_sink_unlock(f);""")
    suffix = edit(suffix, "void con_sink_owner_exit(int id)", """volatile int p3_exit_reader, p3_exit_owner;
void con_sink_owner_exit(int id)""")
    suffix = edit(suffix, "    if (id == CON_SINK_NO_READER) return;", """    p3_exit_owner = id;
    p3_exit_reader = g_reader;
    if (id == CON_SINK_NO_READER) return;""")
    meter("KB_MUT", sink, prefix + "volatile int p3_freeze_rc;\nvolatile unsigned p3_end_count;\n"
          + "int con_sink_capture(unsigned int op)" + body + "int con_sink_capture_active" + suffix)
    heap = Path("sys/kern/mm/kmalloc.c")
    alloc = "    void *p = kheap_alloc(&kernel_heap, size);"
    meter("KB_MUT_EXTRA", heap, "volatile unsigned p3_alloc_failures;\n" +
          edit(heap.read_text(), alloc, alloc + "\n    if (size && !p) ++p3_alloc_failures;"))


def link_media(*, usb_trace=False, overflow=False, rpc_trace=False):
    directory = Path(os.environ["OS32_VERIFY_OUT"]).resolve() / "link-sh"
    directory.mkdir()
    user = directory / "user"
    temp = directory / "tmp"
    temp.mkdir()
    args = ["make", "-j" + os.getenv("OS32_BUILD_JOBS", "8"), *PPC_MAKE,
            "IMAGE=kboot-link-sh", "KBOOT_SH=1", "KBOOT_LINK=1", "KBOOT_GEM_RX_DIAG=1",
            f"KB_OUT={directory}", f"KB_SH_OUT={user}", f"BUILD_OUT={directory / 'build'}",
            f"KB_BODY_OUT={directory / 'sqlite-body'}"]
    if usb_trace:
        args.append("KBOOT_USB_KEY_TRACE=1")
    unicode_bin = directory / "unicode.bin"
    manifest = json.loads(Path("build/ppc-ram-root.json").read_text())
    manifest["/sys/unicode.bin"] = str(unicode_bin)
    template = directory / "root-template.json"
    template.write_text(json.dumps(manifest))
    sample = directory / "sample.txt"
    sample.write_text("ASCII ｶﾅ 日本語\n", encoding="utf-8")
    args.extend([f"ROOT_MANIFEST={template}", f"PPC_UNICODE={unicode_bin}",
                 f"KB_ROOT_BINS={user}/hello.bin {user}/fault.bin {sample}",
                 "KB_KEEP_RESET=-Wl,--wrap=arch_entry_enter,--wrap=ppc_trap_dispatch,--wrap=exec_run"])
    hashes = {}

    def meter(slot, path, content):
        target = directory / (path.stem + "-meter.c")
        target.write_text(content)
        hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        args.extend([f"{slot}_SRC={path}", f"{slot}_FILE={target}"])

    shell = Path("userland/shell/rshell_lan.c")
    stimuli = r'''        if (!strcmp(line, "p3-buffer")) {
            printf("buffered line\n");
        } else if (!strcmp(line, "p3-fault")) {
            __asm__ volatile("trap");
        } else if (!strcmp(line, "p3-output")) {
            const char message[] = "FD1 UTF8 \xe3\x81\x82\n";
            g_api->tvram_clear();
            g_api->sys_write(1, message, sizeof(message) - 1);
        } else accepting = rsh_lan_execute(line);'''
    if overflow:
        stimuli = stimuli.replace("} else accepting", r''' } else if (!strcmp(line, "p3-overflow")) {
            for (int i = 0; i < 110; ++i)
                g_api->kprintf(7, "%s", "0123456789012345678901234567890123456789"
                                       "0123456789012345678901234567890123456789\n");
        } else accepting''')
    meter("PPC_SH_MUT", shell, "#include <stdio.h>\n" +
          edit(shell.read_text(), "        accepting = rsh_lan_execute(line);", stimuli))
    if rpc_trace:
        meter_rpc(meter)
    else:
        meter_capture(meter)
    execute = Path("sys/kern/exec/exec.c")
    flow = Path("tests/support/fixtures/ppc_link/flow.inc")
    meter("KB_MUT_THIRD", execute, execute.read_text() + "\n" + flow.read_text())
    args += ["all", "iso"]
    subprocess.run(args, check=True, env={**os.environ, "TMPDIR": str(temp)})
    iso = directory / "boot.iso"
    return iso, dict(make=args, fixture=hashes, **{
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (iso, directory / "PPCKERN.ELF")})


HWB = Path("tests/support/fixtures/ppc_hwb")


def hwb_media():
    directory = Path(os.environ["OS32_VERIFY_OUT"]).resolve() / "ppc_hwb"
    directory.mkdir()
    manifest = json.loads(Path("build/ppc-ram-root.json").read_text())
    scripts = Path("userland/diag/hw")
    manifest["/hw/ohci.hwb"] = str(scripts / "mac99/ohci.hwb")
    manifest["/hw/wait.hwb"] = str(scripts / "np21w_wait.hwb")
    poll = directory / "poll.hwb"
    poll.write_text("device d = id 106b:003f\npoll 32 pci d 0 0xffff 0 10000000 1000 -> x\nend\n")
    manifest["/hw/poll.hwb"] = str(poll)
    for via in ("lan", "usb"):
        for op in ("wait", "poll"):
            script = directory / f"{via}{op}.sh"
            script.write_text(f"hw load /hw/{op}.hwb\necho loaded-{via}-{op} $?\n")
            manifest[f"/hw/{script.name}"] = str(script)
    manifest["/sys/unicode.bin"] = str(directory / "unicode.bin")
    template = directory / "template.json"
    template.write_text(json.dumps(manifest))
    cfg = directory / "system.cfg"
    cfg.write_text("KDEBUG=1\n")

    source = Path("sys/kern/exec/exec.c")
    abort = source.with_name("exec_abort.inc").read_text()
    instrumented = directory / "exec_abort.inc"
    site = "    return !!pending;"
    instrumented.write_text("volatile u32 hw_points[9];\n" +
                            edit(abort, site, (HWB / "points.inc").read_text() + site))
    executor = directory / "exec-meter.c"
    executor.write_text(edit(source.read_text(), '#include "exec_abort.inc"',
                             f'#include "{instrumented}"'))
    pci = PM / "pci_backend.c"
    meter = directory / "pci-meter.c"
    site = "{ return pm_pci_access(&root, bdf << 8, offset, width, 0, value); }"
    meter.write_text("volatile unsigned hw_pci_reads;\n" + edit(pci.read_text(), site, """{
    u32 rc = pm_pci_access(&root, bdf << 8, offset, width, 0, value);
    if (!rc && offset == 0 && width == 4 && *value == 0x003f106b)
        ++hw_pci_reads;
    return rc;
}"""))
    args = ["make", "-j" + os.getenv("OS32_BUILD_JOBS", "8"), *PPC_MAKE,
            "IMAGE=kboot-link-sh", "FEATURE_KDEBUG=1", f"KB_OUT={directory}",
            f"KB_SH_OUT={directory}/user", f"BUILD_OUT={directory}/build",
            f"KB_BODY_OUT={directory}/sqlite-body", f"PPC_UNICODE={directory}/unicode.bin",
            f"ROOT_MANIFEST={template}", f"PPC_SYSTEM_CFG={cfg}",
            f"KB_MUT_EXTRA_SRC={source}", f"KB_MUT_EXTRA_FILE={executor}",
            f"KB_MUT_THIRD_SRC={pci}", f"KB_MUT_THIRD_FILE={meter}", "all", "iso"]
    subprocess.run(args, check=True)
    iso = directory / "boot.iso"
    files = [iso, directory / "PPCKERN.ELF", executor, instrumented, meter, template, cfg,
             *(directory.glob("*.sh")), poll, HWB / "points.inc"]
    return iso, dict(make=args, hashes={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                        for p in files})


def hfs_cancel_media():
    directory = Path(os.environ["OS32_VERIFY_OUT"]).resolve() / "hfs-cancel"
    directory.mkdir()
    transport = PM / "hostfs_link_io.c"
    source = transport.read_text()
    fixture = Path("tests/support/fixtures/ppc_link/hfs_cancel.inc").read_text()
    site = "static int phase;"
    # The probe shares the translation unit solely to observe state at return.
    helpers, probe = fixture.split("static void probe_cancel", 1)
    source = edit(source, site, site + "\n" + helpers)
    for old, new in (
        ("return link_platform_now();", "return clock_now;"),
        ("idle_wait_step();", "++clock_now; pm_hfs_link_poll();"),
        ("platform_host_open(request, pos, 0)", "((void)request, probe_open())"),
        ("platform_host_write(handle, tx + written, tx_len - written, 0)",
         "probe_write(tx + written, tx_len - written)"),
        ("platform_host_status(handle, &status, &length, 0)",
         "(status = 200, length = reply_len, 0)"),
        ("platform_host_read_stage(handle, length - received, &data, 0)",
         "(data = reply, (i32)reply_len)"),
        ("platform_host_close(handle, 0)", "probe_close(handle, 0)"),
        ("    if (same_frame(buf, n)) return result;",
         "    if (n >= SFS_HDR_LEN && buf[SFS_OFF_TYPE] == SFS_T_HELLO) ++hello_puts;\n"
         "    if (same_frame(buf, n)) return result;"),
    ):
        source = edit(source, old, new)
    boot = PM / "ppc_kernel.c"
    entry = edit(boot.read_text(), "        pm_exec_boot();",
                 '        extern const char *hfs_cancel_probe(void);\n'
                 '        kprintf(7, "HFS result=%s\\r\\n", hfs_cancel_probe());\n'
                 '        pm_exec_boot();')
    args = ["make", "-j" + os.getenv("OS32_BUILD_JOBS", "8"), *PPC_MAKE,
            "IMAGE=kboot-link-sh", "KBOOT_SH=1", "KBOOT_LINK=1",
            f"KB_OUT={directory}", f"KB_SH_OUT={directory / 'user'}",
            f"BUILD_OUT={directory / 'build'}"]
    for slot, path, content in (("KB_MUT", transport, source + "\nstatic void probe_cancel" + probe),
                                ("KB_MUT_EXTRA", boot, entry)):
        target = directory / path.name
        target.write_text(content)
        args.extend([f"{slot}_SRC={path}", f"{slot}_FILE={target}"])
    subprocess.run([*args, "all", "iso"], check=True)
    return directory / "boot.iso"
