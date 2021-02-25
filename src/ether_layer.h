#pragma once

#include "error.h"
#include <net/ethernet.h>
#include <arpa/inet.h>

static const struct ether_addr BROADCAST_MAC = {"\xff\xff\xff\xff\xff\xff"};

RC arp_get_mac(in_addr_t ip, int if_idx, struct ether_addr *mac);

void print_arp_table();

RC ether_init();

size_t recv_ip_packet(int timeout_sec, uint8_t *ip_packet, int *out_if_idx,
                      struct ether_addr *out_src_mac, struct ether_addr *out_dst_mac);

void send_ip_packet(const uint8_t *ip_packet, size_t ip_len, int if_idx, const struct ether_addr *dst_mac);
