/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_SDK_INCLUDE_OS32_OS32_HOST_NOTICE_H
#define OS32_SDK_INCLUDE_OS32_OS32_HOST_NOTICE_H

typedef struct HostNotice { unsigned int serial, dropped, host_generation;
 unsigned short service, kind, length, reserved; unsigned char data[236]; } HostNotice;

#endif
