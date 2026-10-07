/* Value enumeration packet, KAPI v70; names are NUL-terminated. */
#ifndef OS32_PUBLIC_LS_H
#define OS32_PUBLIC_LS_H
#define OS32_LS_BATCH 14
#define OS32_LS_NAME_SIZE 256
typedef struct {
    char name[OS32_LS_NAME_SIZE];
    unsigned long size;
    unsigned char type;
    unsigned char reserved[3];
} OS32_LsEntry;
typedef struct {
    unsigned long count;
    int result;
    unsigned long done;
    OS32_LsEntry entries[OS32_LS_BATCH];
} OS32_LsPacket;
#endif
