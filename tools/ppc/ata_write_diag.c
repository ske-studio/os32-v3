/* ATA_HAL_DESIGN §6-4. Embedded only in the acceptance kernel between
 * exec_init and shell_loop_run. The host checks the entire disposable disk. */
#include "arch_exec_kapi.h"
#include "kapi_table.h"
#include "ide_contract.h"
#include "kstring.h"

static void ata_write_report(int child)
{
    const KernelAPI *api = (const KernelAPI *)exec_kapi_table();
    IdeInfo id;
    static u8 payload[1025] __attribute__((aligned(2)));
    static u8 buffer[513] __attribute__((aligned(2)));
    if (api->ide_identify(0, &id) || kstrcmp(id.model, "QEMU HARDDISK") ||
        id.total_sectors != 2048) {
        kprintf(7, "ATA-TEST wrong disk\r\n");
        return;
    }
    if (child) {
        int status = api->exec_run("/sys/shell.bin");
        kprintf(7, "ATA-TEST child=%d\r\n", status);
        return;
    }
    for (unsigned i = 0; i < 1024; ++i)
        payload[1+i] = i * 37 + (i >> 8) + 13;
    int one = api->ide_write_sector(0, 32, payload + 1);
    int many = api->ide_write_sectors(0, 64, 2, payload + 1);
    int range = api->ide_write_sectors(0, 2047, 2, payload + 1);
    kprintf(7, "ATA-TEST writes=%d,%d range=%d\r\n", one, many, range);
    for (unsigned n = 0; n < 3; ++n) {
        int rc = api->ide_read_sector(0, n ? 63+n : 32, buffer + 1);
        int equal = !rc && !memcmp(buffer + 1, payload + 1 + (n == 2 ? 512 : 0), 512);
        kprintf(7, "ATA-TEST read=%u equal=%d\r\n", n, equal);
    }
}
