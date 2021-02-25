#pragma once

#include "error.h"
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdio.h>

// Interface config
#define MAX_IF 16

extern int NUM_IF;
extern char *if_names[MAX_IF];
extern in_addr_t if_ips[MAX_IF];
extern in_addr_t if_masks[MAX_IF];
extern struct ether_addr if_macs[MAX_IF];

// Config init
RC config_init(const char *config_path);

void config_destroy();

static inline char *mac2str(uint8_t mac[6]) {
    static char s[18];
    sprintf(s, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return s;
}

static inline char *ip2str(in_addr_t ip) {
    struct in_addr addr = {ip};
    return inet_ntoa(addr);
}
