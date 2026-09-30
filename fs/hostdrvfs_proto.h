/* ======================================================================== */
/*  HOSTDRVFS_PROTO.H — NP21/W HostDrv(NT) I/Oポートプロトコル定義          */
/*                                                                          */
/*  NP21/Wエミュレータの hostdrvnt 機能とOS32間で通信するための構造体・      */
/*  定数を定義。バイナリレイアウトは generic/hostdrvntdef.h と完全一致する。  */
/*                                                                          */
/*  参考: NP21/W generic/hostdrvntdef.h, generic/hostdrvnt.c                */
/* ======================================================================== */

#ifndef HOSTDRVFS_PROTO_H
#define HOSTDRVFS_PROTO_H

#include "types.h"

/* types.h に定義されていない追加型 */
typedef unsigned long long u64;
typedef signed char        s8;
typedef signed short       s16;

/* ===================================================================== */
/*  I/Oポートアドレス                                                     */
/* ===================================================================== */
#define HOSTDRV_IO_ADDR  0x7EC   /* データアドレス送信用 */
#define HOSTDRV_IO_CMD   0x7EE   /* コマンドシーケンス送信用 */

/* 存在確認用マジック値 */
#define HOSTDRV_DETECT_ADDR_VAL  98
#define HOSTDRV_DETECT_CMD_VAL   21

/* ===================================================================== */
/*  IRP メジャーファンクション定数                                        */
/* ===================================================================== */
#define NP2_IRP_MJ_CREATE                   0x00
#define NP2_IRP_MJ_CLOSE                    0x02
#define NP2_IRP_MJ_READ                     0x03
#define NP2_IRP_MJ_WRITE                    0x04
#define NP2_IRP_MJ_QUERY_INFORMATION        0x05
#define NP2_IRP_MJ_SET_INFORMATION          0x06
#define NP2_IRP_MJ_QUERY_VOLUME_INFORMATION 0x0A
#define NP2_IRP_MJ_DIRECTORY_CONTROL        0x0C
#define NP2_IRP_MJ_CLEANUP                  0x12

/* IRP マイナーファンクション */
#define NP2_IRP_MN_QUERY_DIRECTORY          0x01

/* ===================================================================== */
/*  NTSTATUSコード                                                        */
/* ===================================================================== */
#define NP2_STATUS_SUCCESS                  0x00000000UL
#define NP2_STATUS_BUFFER_OVERFLOW          0x80000005UL
#define NP2_STATUS_NO_MORE_FILES            0x80000006UL
#define NP2_STATUS_END_OF_FILE              0xC0000011UL
#define NP2_STATUS_OBJECT_NAME_NOT_FOUND    0xC0000034UL
#define NP2_STATUS_OBJECT_NAME_COLLISION    0xC0000035UL
#define NP2_STATUS_OBJECT_PATH_NOT_FOUND    0xC000003AUL
#define NP2_STATUS_ACCESS_DENIED            0xC0000022UL
#define NP2_STATUS_BUFFER_TOO_SMALL         0xC0000023UL
#define NP2_STATUS_NOT_A_DIRECTORY          0xC0000103UL
#define NP2_STATUS_FILE_IS_A_DIRECTORY      0xC00000BAUL
#define NP2_STATUS_INVALID_PARAMETER        0xC000000DUL
/* 票 B8 / Codex 実装レビュー P1-4 で足した分。IRP_MJ_CREATE が返す種類を
 * 「不存在」と「それ以外の失敗」に分けるために要る。値は NP21/W の
 * src/generic/hostdrvntdef.h と同じ (NT の NTSTATUS)。 */
#define NP2_STATUS_OBJECT_NAME_INVALID      0xC0000033UL
#define NP2_STATUS_SHARING_VIOLATION        0xC0000043UL
#define NP2_STATUS_MEDIA_WRITE_PROTECTED    0xC00000A2UL
#define NP2_STATUS_DIRECTORY_NOT_EMPTY      0xC0000101UL
#define NP2_STATUS_TOO_MANY_OPENED_FILES    0xC000011FUL
#define NP2_STATUS_CANNOT_DELETE            0xC0000121UL

/* ===================================================================== */
/*  アクセス権 (DesiredAccess)                                            */
/* ===================================================================== */
#define NP2_FILE_READ_DATA                  0x00000001UL
/* NP21/W側のバグで 0x02 は GENERIC_READ に化けるため、0x04 (FILE_APPEND_DATA) を使って GENERIC_WRITE を獲得する */
#define NP2_FILE_WRITE_DATA                 0x00000004UL
#define NP2_DELETE                          0x00010000UL
#define NP2_GENERIC_READ                    0x80000000UL
#define NP2_GENERIC_WRITE                   0x40000000UL

/* ===================================================================== */
/*  ファイルオープンモード (createDisposition)                             */
/* ===================================================================== */
#define NP2_FILE_SUPERSEDE                  0x00000000UL
#define NP2_FILE_OPEN                       0x00000001UL
#define NP2_FILE_CREATE                     0x00000002UL
#define NP2_FILE_OPEN_IF                    0x00000003UL
#define NP2_FILE_OVERWRITE                  0x00000004UL
#define NP2_FILE_OVERWRITE_IF               0x00000005UL

/* create.options フラグ */
#define NP2_FILE_DIRECTORY_FILE             0x00000001UL
#define NP2_FILE_NON_DIRECTORY_FILE         0x00000040UL
#define NP2_FILE_SYNCHRONOUS_IO_NONALERT    0x00000020UL

/* Information 戻り値 */
#define NP2_FILE_OPENED                     0x00000001UL
#define NP2_FILE_CREATED                    0x00000002UL

/* ファイル属性 */
#define NP2_FILE_ATTRIBUTE_READONLY          0x00000001UL
#define NP2_FILE_ATTRIBUTE_HIDDEN            0x00000002UL
#define NP2_FILE_ATTRIBUTE_SYSTEM            0x00000004UL
#define NP2_FILE_ATTRIBUTE_DIRECTORY         0x00000010UL
#define NP2_FILE_ATTRIBUTE_NORMAL            0x00000080UL

/* IRP flags */
#define NP2_SL_RESTART_SCAN                 0x01
#define NP2_SL_RETURN_SINGLE_ENTRY          0x02
#define NP2_SL_CASE_SENSITIVE               0x80

/* FO flags */
#define NP2_FO_SYNCHRONOUS_IO               0x00000002UL

/* FILE_INFORMATION_CLASS (使用するもののみ) */
#define NP2_FileDirectoryInformation         1
#define NP2_FileBothDirectoryInformation     3
#define NP2_FileBasicInformation             4
#define NP2_FileStandardInformation          5
#define NP2_FileNameInformation              9
#define NP2_FileRenameInformation           10
#define NP2_FileDispositionInformation      13
#define NP2_FilePositionInformation         14
#define NP2_FileAllInformation              18
#define NP2_FileAllocationInformation       19
#define NP2_FileEndOfFileInformation        20

/* FS_INFORMATION_CLASS */
#define NP2_FileFsVolumeInformation          1
#define NP2_FileFsSizeInformation            3
#define NP2_FileFsDeviceInformation          4
#define NP2_FileFsAttributeInformation       5
#define NP2_FileFsFullSizeInformation        7

/* ===================================================================== */
/*  パッキング済み構造体 (NP21/W hostdrvntdef.h とバイナリ互換)            */
/* ===================================================================== */

/* #pragma pack(push, 4) 相当 — GCCでは__attribute__((packed))を使用  */

/* UNICODE_STRING 互換 */
typedef struct {
    u16 Length;
    u16 MaximumLength;
    u32 Buffer;         /* ゲストメモリ上のUTF-16LEバッファアドレス */
} __attribute__((packed)) Np2UnicodeString;

/* IO_STACK_LOCATION 互換 (全パラメータunion含む) */
typedef struct {
    u8  majorFunction;
    u8  minorFunction;
    u8  flags;
    u8  control;
    union {
        /* IRP_MJ_READ / IRP_MJ_WRITE */
        struct {
            u32 length;
            u32 key;
            u64 byteOffset;
        } read;
        struct {
            u32 length;
            u32 key;
            u64 byteOffset;
        } write;
        /* IRP_MJ_CREATE */
        struct {
            u32 securityContext;
            u32 options;        /* 上位8bit=disposition, 下位24bit=options */
            u16 fileAttributes;
            u16 shareAccess;
            u32 eaLength;
        } create;
        /* IRP_MJ_QUERY_VOLUME_INFORMATION */
        struct {
            u32 length;
            u32 fsInformationClass;
        } queryVolume;
        /* IRP_MJ_DIRECTORY_CONTROL */
        struct {
            u32 Length;
            u32 FileName;             /* アドレス */
            u32 FileInformationClass;
            u32 FileIndex;
        } queryDirectory;
        /* IRP_MJ_QUERY_INFORMATION */
        struct {
            u32 Length;
            u32 FileInformationClass;
        } queryFile;
        /* IRP_MJ_SET_INFORMATION */
        struct {
            u32 Length;
            u32 FileInformationClass;
            u32 FileObject;
            union {
                struct {
                    u8 ReplaceIfExists;
                    u8 AdvanceOnly;
                };
                u32 ClusterCount;
                u32 DeleteHandle;
            };
        } setFile;
        /* 汎用 */
        struct {
            u32 argument1;
            u32 argument2;
            u32 argument3;
            u32 argument4;
        } others;
    } parameters;
    u32 deviceObject;
    u32 fileObject;       /* → Np2FileObject のゲストアドレス */
    u32 completionRoutine;
    u32 context;
} __attribute__((packed)) Np2IoStackLocation;

/* FILE_OBJECT 互換 */
typedef struct {
    s16 Type;
    s16 Size;
    u32 DeviceObject;
    u32 Vpb;
    u32 FsContext;          /* → Np2FsContext のゲストアドレス */
    u32 FsContext2;
    u32 SectionObjectPointer;
    u32 PrivateCacheMap;
    u32 FinalStatus;
    u32 RelatedFileObject;
    u8  LockOperation;
    u8  DeletePending;
    u8  ReadAccess;
    u8  WriteAccess;
    u8  DeleteAccess;
    u8  SharedRead;
    u8  SharedWrite;
    u8  SharedDelete;
    u32 Flags;
    Np2UnicodeString FileName;
    u64 CurrentByteOffset;
} __attribute__((packed)) Np2FileObject;

/* FSRTL_COMMON_FCB_HEADER 互換 (40バイト, ゼロ初期化して触らない) */
typedef struct {
    s16 NodeTypeCode;
    s16 NodeByteSize;
    u8  Flags;
    u8  IsFastIoPossible;
    u8  Flags2;
    u8  ReservedVersion;      /* Reserved:4 + Version:4 */
    u32 Resource;
    u32 PagingIoResource;
    u64 AllocationSize;
    u64 FileSize;
    u64 ValidDataLength;
} __attribute__((packed)) Np2FsrtlCommonFcbHeader;

/* FsContext 構造体 (OS32側で確保, エミュレータがfileIndexを書き込む)
 *
 * version=1: エミュレータは FsContext+0 からfileIndexを読み取る
 * version>=4: FsContext+40 (HOSTDRVNT_FSCONTEXT_USERDATA_OFFSET)
 *
 * OS32はversion=1を使うため、fileIndexを先頭に配置する。
 */
typedef struct {
    u32 fileIndex;                    /* エミュレータが管理するファイルID (offset 0) */
    u32 reserved[15];                 /* 予約 (64バイト以上確保) */
} __attribute__((packed)) Np2FsContext;

/* IO_STATUS_BLOCK 互換 */
typedef struct {
    u32 Status;
    u32 Information;
} __attribute__((packed)) Np2IoStatusBlock;

/* HOSTDRVNT_INVOKEINFO — エミュレータへ渡す通信構造体
 *
 * 注意: エミュレータはoffset 0のDWORDを NP2_IO_STACK_LOCATION への
 * ゲストアドレスとして扱う (ポインタ間接参照)。
 * stack はインラインではなく、別途確保した IO_STACK_LOCATION のアドレス。
 */
typedef struct {
    u32 stackAddr;                 /* 0x00 */
    u32 statusAddr;                /* 0x04 */
    u32 inBufferAddr;              /* 0x08 */
    u32 deviceFlags;               /* 0x0C */
    u32 outBufferAddr;             /* 0x10 */
    u32 sectionObjectPointerAddr;  /* 0x14 */
    u32 version;                   /* 0x18 */
} __attribute__((packed)) Np2InvokeInfo;

/* ===================================================================== */
/*  ディレクトリ列挙用構造体                                              */
/* ===================================================================== */

/* FILE_BOTH_DIR_INFORMATION 互換 (pack(4)) */
typedef struct {
    u32 NextEntryOffset;
    u32 FileIndex;
    u64 CreationTime;
    u64 LastAccessTime;
    u64 LastWriteTime;
    u64 ChangeTime;
    u64 EndOfFile;
    u64 AllocationSize;
    u32 FileAttributes;
    u32 FileNameLength;     /* バイト数 (NULL終端含まず) */
    u32 EaSize;
    s8  ShortNameLength;
    u8  _pad;               /* pack(4)アラインメント用パディング */
    u16 ShortName[12];
    u16 FileName[260];      /* UTF-16LE, MAX_PATH */
} __attribute__((packed)) Np2FileBothDirInfo;

/* FILE_DIRECTORY_INFORMATION 互換 */
typedef struct {
    u32 NextEntryOffset;
    u32 FileIndex;
    u64 CreationTime;
    u64 LastAccessTime;
    u64 LastWriteTime;
    u64 ChangeTime;
    u64 EndOfFile;
    u64 AllocationSize;
    u32 FileAttributes;
    u32 FileNameLength;     /* バイト数 */
    u16 FileName[260];      /* UTF-16LE */
} __attribute__((packed)) Np2FileDirInfo;

/* ===================================================================== */
/*  ファイル情報取得用構造体 (pack(8) に注意)                             */
/* ===================================================================== */

/* FILE_BASIC_INFORMATION (pack(8)) */
typedef struct {
    u64 CreationTime;
    u64 LastAccessTime;
    u64 LastWriteTime;
    u64 ChangeTime;
    u32 FileAttributes;
} __attribute__((packed)) Np2FileBasicInfo;

/* FILE_STANDARD_INFORMATION (pack(8)) */
typedef struct {
    u64 AllocationSize;
    u64 EndOfFile;
    u32 NumberOfLinks;
    u8  DeletePending;
    u8  Directory;
} __attribute__((packed)) Np2FileStandardInfo;

/* FILE_END_OF_FILE_INFORMATION (pack(4)) */
typedef struct {
    u64 EndOfFile;
} __attribute__((packed)) Np2FileEndOfFileInfo;

/* FILE_ALLOCATION_INFORMATION (pack(8)) */
typedef struct {
    u64 AllocationSize;
} __attribute__((packed)) Np2FileAllocationInfo;

/* FILE_DISPOSITION_INFORMATION (pack(8)) */
typedef struct {
    u8 DeleteFileOnClose;
} __attribute__((packed)) Np2FileDispositionInfo;

/* FILE_RENAME_INFORMATION (pack(8)) */
typedef struct {
    u8  ReplaceIfExists;
    u8  _pad[3];         /* pack(8) align */
    u32 RootDirectory;
    u32 FileNameLength;  /* バイト数 */
    u16 FileName[260];
} __attribute__((packed)) Np2FileRenameInfo;

/* IO_SECURITY_CONTEXT (CREATE時にポインタとして渡す) */
typedef struct {
    u32 SecurityQos;
    u32 AccessState;
    u32 DesiredAccess;  /* オフセット8: エミュレータが読み取る */
    u32 FullCreateOptions;
} __attribute__((packed)) Np2IoSecurityContext;

#endif /* HOSTDRVFS_PROTO_H */
