/* Ethernet wire constants shared by NICs and transports. */
#ifndef OS32_NET_ETH_H
#define OS32_NET_ETH_H
#define ETH_ALEN       6
#define ETH_HLEN       14
#define ETH_ZLEN       60
#define ETH_FRAME_MAX  1518 /* Includes the four-byte FCS. */
#define ETH_FCS_LEN    4
#define ETH_TYPE_OFF   12
#define OS32_ETHERTYPE 0x88B5
#endif
