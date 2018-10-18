#ifndef _MTNF_FIREWALL_H
#define _MTNF_FIREWALL_H

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_hash.h>
#include <rte_lpm.h>

#include "mtnf_help.h"

#define FIREWALL_HASH_ENTRIES 1024
#define BIG_PRIME 10000019

struct bf_node {
    uint32_t ip_src, ip_dst;
    uint16_t port_src, port_dst;
    uint8_t proto, action;
};

struct hash_node {
    uint32_t ip_src, ip_dst;
    uint16_t port_src, port_dst;
    uint8_t proto, action;
    bool is_valid;
    struct hash_node* next;
};

struct firewall_statistics {
    struct hash_node hash_map[BIG_PRIME];
};

/* register tenant state */
uint32_t
mtnf_firewall_register(void);

/* init tenant state */
void
mtnf_firewall_init(void *state);

/* handle tenant packets */
uint16_t
mtnf_firewall_handler(struct rte_mbuf *pkt[], uint16_t num, void *state);

#endif