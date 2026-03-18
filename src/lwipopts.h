/*
 * lwipopts.h — lwIP configuration for MapleSyrup Pico W WiFi AP config mode
 *
 * Minimal options for a single TCP HTTP server + DHCP server.
 * Only active in config mode (WiFi AP); in normal mode CYW43 arch_poll still
 * initialises lwIP but no network interface is assigned, so these settings
 * have no runtime cost.
 */
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// No underlying OS
#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0

// Memory — keep small for a single HTTP client at a time
#define MEM_LIBC_MALLOC                 0
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        8000
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_ARP_QUEUE              10
#define PBUF_POOL_SIZE                  16

// Protocols
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_IPV4                       1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_DHCP                       1
#define LWIP_DNS                        1

// TCP tuning — one connection at a time, small buffers
#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_LISTEN_BACKLOG              1
#define LWIP_TCP_KEEPALIVE              1

// Netif callbacks
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_TX_SINGLE_PBUF       1

// Skip ARP probe for DHCP — not needed in AP mode
#define DHCP_DOES_ARP_CHECK             0
#define LWIP_DHCP_DOES_ACD_CHECK        0

// Checksum
#define LWIP_CHKSUM_ALGORITHM           3

// Turn off stats to save RAM
#define MEM_STATS                       0
#define SYS_STATS                       0
#define MEMP_STATS                      0
#define LINK_STATS                      0

#endif /* _LWIPOPTS_H */
