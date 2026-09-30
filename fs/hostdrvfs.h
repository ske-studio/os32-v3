/* ======================================================================== */
/*  HOSTDRVFS.H — NP21/W HostDrv(NT) VFSドライバ                          */
/*                                                                          */
/*  NP21/Wエミュレータのホスト共有ドライブ(NT版)にアクセスするための        */
/*  VFSドライバ。I/Oポート 0x7EC/0x7EE を使用してエミュレータと通信する。   */
/* ======================================================================== */

#ifndef HOSTDRVFS_H
#define HOSTDRVFS_H

#include "vfs.h"

/* HostDrvFS初期化: VfsOps登録を行う */
void hostdrvfs_init(void);

/* HostDrv存在確認: エミュレータが対応していれば1を返す */
int hostdrvfs_detect(void);

/* VfsOps取得 (vfs_register_fs用) */
VfsOps *hostdrvfs_get_ops(void);

#endif /* HOSTDRVFS_H */
