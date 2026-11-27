"""Build ATA diagnostic media from the current verification tree."""
from pathlib import Path
import hashlib
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
    args = ["make", "-j" + os.getenv("OS32_BUILD_JOBS", "8"), *PPC_MAKE,
            "IMAGE=kboot-sh", f"KB_OUT={directory}", f"KB_SH_OUT={user}"]
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
    subprocess.run(args, check=True)
    iso = directory / "boot.iso"
    return iso, dict(make=args, fixture=copies, **{
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (iso, directory / "PPCKERN.ELF")})
