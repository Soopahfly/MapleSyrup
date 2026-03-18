/*
 * Minimal DHCP server for lwIP in AP mode.
 * Derived from the Raspberry Pi pico-examples access_point DHCP server,
 * originally from MicroPython (MIT licence, Damien P. George 2018-2019).
 *
 * Handles DISCOVER → OFFER and REQUEST → ACK for up to DHCPS_MAX_IP clients.
 * Assigns addresses gateway+DHCPS_BASE_IP .. gateway+DHCPS_BASE_IP+DHCPS_MAX_IP-1.
 */

#include "dhcpserver.h"

#include <string.h>
#include "pico/stdlib.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"

// ── DHCP constants ────────────────────────────────────────────────────────────
#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68

#define DHCP_OP_REQUEST     1
#define DHCP_OP_REPLY       2
#define DHCP_MAGIC_COOKIE   0x63825363

// DHCP message types
#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPACK         5
#define DHCPNACK        6

// DHCP options
#define OPT_SUBNET_MASK     1
#define OPT_ROUTER          3
#define OPT_DNS             6
#define OPT_REQUESTED_IP    50
#define OPT_LEASE_TIME      51
#define OPT_MSG_TYPE        53
#define OPT_SERVER_ID       54
#define OPT_CLIENT_ID       61
#define OPT_END             255

#define LEASE_TIME_S        (24 * 60 * 60)  // 24 hours

// ── DHCP message structure (RFC 2131) ─────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[308];
} dhcp_msg_t;

// ── Option helpers ─────────────────────────────────────────────────────────────
static uint8_t *opt_put(uint8_t *p, uint8_t code, uint8_t len, const void *val) {
    *p++ = code;
    *p++ = len;
    memcpy(p, val, len);
    return p + len;
}

static uint8_t *opt_put_u32(uint8_t *p, uint8_t code, uint32_t val) {
    uint32_t v = lwip_htonl(val);
    return opt_put(p, code, 4, &v);
}

static uint8_t *opt_put_u8(uint8_t *p, uint8_t code, uint8_t val) {
    return opt_put(p, code, 1, &val);
}

// ── Find DHCP option ──────────────────────────────────────────────────────────
static uint8_t opt_find(const uint8_t *opts, uint8_t code, uint8_t *out, size_t out_len) {
    while (*opts != OPT_END && opts < opts + 308) {
        uint8_t c = *opts++;
        uint8_t l = *opts++;
        if (c == code) {
            memcpy(out, opts, l < out_len ? l : out_len);
            return l;
        }
        opts += l;
    }
    return 0;
}

// ── Send DHCP reply ───────────────────────────────────────────────────────────
static void dhcp_send(dhcp_server_t *s, dhcp_msg_t *msg, uint8_t msg_type,
                      const uint8_t *offer_ip) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(dhcp_msg_t), PBUF_RAM);
    if (!p) return;

    dhcp_msg_t *reply = (dhcp_msg_t *)p->payload;
    memset(reply, 0, sizeof(*reply));

    reply->op    = DHCP_OP_REPLY;
    reply->htype = 1;   // Ethernet
    reply->hlen  = 6;
    reply->xid   = msg->xid;
    memcpy(reply->yiaddr, offer_ip, 4);
    memcpy(reply->siaddr, &s->ip,   4);
    memcpy(reply->chaddr, msg->chaddr, 6);
    reply->magic = lwip_htonl(DHCP_MAGIC_COOKIE);

    uint8_t *opt = reply->options;
    opt = opt_put_u8(opt, OPT_MSG_TYPE, msg_type);
    opt = opt_put(opt, OPT_SERVER_ID,  4, &s->ip);
    opt = opt_put(opt, OPT_SUBNET_MASK, 4, &s->nm);
    opt = opt_put(opt, OPT_ROUTER,     4, &s->ip);
    opt = opt_put(opt, OPT_DNS,        4, &s->ip);
    opt = opt_put_u32(opt, OPT_LEASE_TIME, LEASE_TIME_S);
    *opt++ = OPT_END;

    // Broadcast reply
    ip_addr_t bcast;
    IP4_ADDR(&bcast, 255, 255, 255, 255);

    struct udp_pcb *pcb = s->udp;
    udp_sendto_if(pcb, p, &bcast, DHCP_CLIENT_PORT, netif_default);
    pbuf_free(p);
}

// ── Find or allocate a lease ──────────────────────────────────────────────────
static int dhcp_alloc_lease(dhcp_server_t *s, const uint8_t *mac,
                             uint8_t *ip_out) {
    // First pass: find existing lease for this MAC
    for (int i = 0; i < DHCPS_MAX_IP; i++) {
        if (memcmp(s->lease[i].mac, mac, 6) == 0) {
            ip_out[0] = ip4_addr1(ip_2_ip4(&s->ip));
            ip_out[1] = ip4_addr2(ip_2_ip4(&s->ip));
            ip_out[2] = ip4_addr3(ip_2_ip4(&s->ip));
            ip_out[3] = ip4_addr4(ip_2_ip4(&s->ip)) + DHCPS_BASE_IP + i;
            return i;
        }
    }
    // Second pass: find free (zero MAC) slot
    static const uint8_t zero_mac[6] = {0};
    for (int i = 0; i < DHCPS_MAX_IP; i++) {
        if (memcmp(s->lease[i].mac, zero_mac, 6) == 0) {
            memcpy(s->lease[i].mac, mac, 6);
            ip_out[0] = ip4_addr1(ip_2_ip4(&s->ip));
            ip_out[1] = ip4_addr2(ip_2_ip4(&s->ip));
            ip_out[2] = ip4_addr3(ip_2_ip4(&s->ip));
            ip_out[3] = ip4_addr4(ip_2_ip4(&s->ip)) + DHCPS_BASE_IP + i;
            return i;
        }
    }
    return -1;
}

// ── UDP receive callback ───────────────────────────────────────────────────────
static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)addr; (void)port;
    dhcp_server_t *s = (dhcp_server_t *)arg;

    if (p->tot_len < sizeof(dhcp_msg_t)) {
        pbuf_free(p);
        return;
    }

    dhcp_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    pbuf_copy_partial(p, &msg, sizeof(msg), 0);
    pbuf_free(p);

    if (msg.op != DHCP_OP_REQUEST) return;
    if (lwip_ntohl(msg.magic) != DHCP_MAGIC_COOKIE) return;

    uint8_t msg_type = 0;
    opt_find(msg.options, OPT_MSG_TYPE, &msg_type, 1);

    uint8_t offer_ip[4];
    int slot = dhcp_alloc_lease(s, msg.chaddr, offer_ip);
    if (slot < 0) return;

    if (msg_type == DHCPDISCOVER) {
        dhcp_send(s, &msg, DHCPOFFER, offer_ip);
    } else if (msg_type == DHCPREQUEST) {
        s->lease[slot].expiry = (uint16_t)(LEASE_TIME_S / 3600);
        dhcp_send(s, &msg, DHCPACK, offer_ip);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void dhcp_server_init(dhcp_server_t *d, ip_addr_t *ip, ip_addr_t *nm) {
    memset(d, 0, sizeof(*d));
    ip_addr_copy(d->ip, *ip);
    ip_addr_copy(d->nm, *nm);

    d->udp = udp_new();
    if (!d->udp) return;
    udp_bind(d->udp, IP_ADDR_ANY, DHCP_SERVER_PORT);
    udp_recv(d->udp, dhcp_recv, d);
}

void dhcp_server_deinit(dhcp_server_t *d) {
    if (d->udp) {
        udp_remove(d->udp);
        d->udp = NULL;
    }
}
