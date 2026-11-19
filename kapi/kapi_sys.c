/* ======================================================================== */
/*  kapi_sys.c — KernelAPI システム情報関数                                  */
/* ======================================================================== */
/*  中身は移った: kapi_sys_ver_info は kapi/kapi_sysinfo.c (sysinfo の S5)、  */
/*  kapi_pci_bind_info は kernel/dev/device_pci.c (PCI の共通化)。          */
/*  ファイルを消すかは次の段で決める (選択表・covers が名前を持つため)。     */

#include "os32_kapi_shared.h"
