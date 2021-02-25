#include "ether_layer.h"
#include "physical_layer.h"
#include <linux/if_arp.h>
#include <stdio.h>
#include <string.h>

// ARP packet
typedef struct __attribute__((__packed__)) arp_packet {
    struct arphdr hdr;
    struct ether_addr ar_sha;
    in_addr_t ar_sip;
    struct ether_addr ar_tha;
    in_addr_t ar_tip;
} arp_packet_t;

// ARP table
typedef struct arp_entry {
    in_addr_t ip;
    int if_idx;
    struct ether_addr mac;
} arp_entry_t;

#define ARP_TABLE_CAPACITY 1024

struct {
    arp_entry_t entries[ARP_TABLE_CAPACITY];
    int size;
} arp_table;

static int arp_find_entry(in_addr_t ip, int if_idx) {
    int i;
    for (i = 0; i < arp_table.size; i++) {
        if (arp_table.entries[i].ip == ip && arp_table.entries[i].if_idx == if_idx) {
            break;
        }
    }
    return i;
}

static RC arp_insert_entry(in_addr_t ip, int if_idx, const struct ether_addr *mac) {
    int pos = arp_find_entry(ip, if_idx);
    if (pos == arp_table.size) {
        // Entry not found, need to insert new entry
        if (arp_table.size >= ARP_TABLE_CAPACITY) {
            fprintf(stderr, "ARP Table overflow\n");
            return OVERFLOW_ERROR;
        }
        arp_table.size++;
    }
    arp_entry_t *entry = &arp_table.entries[pos];
    entry->ip = ip;
    entry->if_idx = if_idx;
    memcpy(&entry->mac, mac, sizeof(struct ether_addr));
    return 0;
}

void print_arp_table() {
    printf("================== ARP TABLE ==================\n");
    char separator[] = "+-----------------+-------------------+-------+";
    printf("%s\n", separator);
    printf("| %15s | %17s | %5s |\n", "IP", "MAC", "IF");
    printf("%s\n", separator);
    for (int i = 0; i < arp_table.size; i++) {
        arp_entry_t *entry = &arp_table.entries[i];
        printf("| %15s | %17s | %5s |\n",
               ip2str(entry->ip), mac2str((uint8_t *) &entry->mac), if_names[entry->if_idx]);
    }
    printf("%s\n", separator);
}

static void send_l3_packet(const uint8_t *l3_packet, size_t l3_len, int if_idx,
                           const struct ether_addr *dst_mac, uint16_t ether_type) {
    static uint8_t packet[BUFSIZ];
    size_t len = sizeof(struct ether_header) + l3_len;
    memcpy(packet + sizeof(struct ether_header), l3_packet, l3_len);
    struct ether_header *ether_hdr = (struct ether_header *) packet;
    memcpy(ether_hdr->ether_dhost, dst_mac, sizeof(struct ether_addr));
    memcpy(ether_hdr->ether_shost, &if_macs[if_idx], sizeof(struct ether_addr));
    ether_hdr->ether_type = ether_type;
    send_packet(packet, len, if_idx);
}

static void send_arp_reply(int if_idx, in_addr_t query_ip, const struct ether_addr *ans_mac,
                           in_addr_t dst_ip, const struct ether_addr *dst_mac) {
    arp_packet_t arp_pkt;
    arp_pkt.hdr = (struct arphdr) {
            .ar_hrd = htons(ARPHRD_ETHER),
            .ar_pro = htons(ETHERTYPE_IP),
            .ar_hln = sizeof(struct ether_addr),
            .ar_pln = sizeof(in_addr_t),
            .ar_op = htons(ARPOP_REPLY),
    };
    memcpy(&arp_pkt.ar_sha, ans_mac, sizeof(struct ether_addr));
    arp_pkt.ar_sip = query_ip;
    memcpy(&arp_pkt.ar_tha, dst_mac, sizeof(struct ether_addr));
    arp_pkt.ar_tip = dst_ip;

    send_l3_packet((uint8_t *) &arp_pkt, sizeof(arp_pkt), if_idx, dst_mac, htons(ETHERTYPE_ARP));
}

void send_arp_request(int if_idx, in_addr_t ip) {
    arp_packet_t arp_pkt;
    arp_pkt.hdr = (struct arphdr) {
            .ar_hrd = htons(ARPHRD_ETHER),
            .ar_pro = htons(ETHERTYPE_IP),
            .ar_hln = sizeof(struct ether_addr),
            .ar_pln = sizeof(in_addr_t),
            .ar_op = htons(ARPOP_REQUEST)
    };
    memcpy(&arp_pkt.ar_sha, &if_macs[if_idx], sizeof(struct ether_addr));
    arp_pkt.ar_sip = if_ips[if_idx];
    memset(&arp_pkt.ar_tha, 0, sizeof(struct ether_addr));
    arp_pkt.ar_tip = ip;

    send_l3_packet((uint8_t *) &arp_pkt, sizeof(arp_pkt), if_idx, &BROADCAST_MAC, htons(ETHERTYPE_ARP));
}

RC ether_init() {
    RC rc;
    for (int i = 0; i < NUM_IF; i++) {
        rc = arp_insert_entry(if_ips[i], i, &if_macs[i]);
        if (rc) { return rc; }
    }
    return 0;
}

static inline bool is_multicast_mac(const struct ether_addr *mac_) {
    // Multicast MAC address of multicast IP is: 01:00:5e:00:00:00 | (IP & 0x007fffff)
    char *mac = (char *) mac_;
    return mac[0] == 0x01 && mac[1] == 0x00 && mac[2] == 0x5e && (mac[3] >> 7) == 0x00;
}

static inline bool is_broadcast_mac(const struct ether_addr *mac) {
    return memcmp(mac, &BROADCAST_MAC, sizeof(struct ether_addr)) == 0;
}

RC arp_get_mac(in_addr_t ip, int if_idx, struct ether_addr *out_mac) {
    if (IN_MULTICAST(ntohl(ip))) {
        // Multicast IP: 224.0.0.0 to 239.255.255.255 (e0.00.00.00 to ef.ff.ff.ff)
        // Corresponding MAC: 01:00:5e:00:00:00 | (IP & 0x007fffff)
        char multicast_mac[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x00};
        *(in_addr_t *) &multicast_mac[2] |= ip & htonl(0x007fffff);
        memcpy(out_mac, multicast_mac, sizeof(struct ether_addr));
    } else {
        int pos = arp_find_entry(ip, if_idx);
        if (pos == arp_table.size) {
            printf("Sending ARP request to %s via %s\n", ip2str(ip), if_names[if_idx]);
            send_arp_request(if_idx, ip);
            return UNKNOWN_MAC_ADDR;
        } else {
            memcpy(out_mac, &arp_table.entries[pos].mac, sizeof(struct ether_addr));
        }
    }
    return 0;
}

size_t recv_ip_packet(int timeout_sec, uint8_t *ip_packet, int *out_if_idx,
                      struct ether_addr *out_src_mac, struct ether_addr *out_dst_mac) {
    while (1) {
        uint8_t packet[BUFSIZ];
        int if_idx;
        uint32_t recv_len = recv_packet(timeout_sec, packet, &if_idx);
        if (recv_len == 0) {
            return 0;
        }
        if (recv_len >= sizeof(struct ether_header)) {
            // Handle ethernet protocol
            struct ether_header *eth_hdr = (struct ether_header *) packet;
            // Check dst mac address
            int dst_mac_is_me = memcmp(eth_hdr->ether_dhost, &if_macs[if_idx], sizeof(struct ether_addr)) == 0;
            if (!dst_mac_is_me && !is_multicast_mac((struct ether_addr *) eth_hdr->ether_dhost) &&
                !is_broadcast_mac((struct ether_addr *) eth_hdr->ether_dhost)) {
                // Target MAC is not broadcast / multicast / router's MAC address
                fprintf(stderr, "Dest MAC address %s is not broadcast or multicast or router's address\n",
                        mac2str(eth_hdr->ether_dhost));
                continue;
            }
            // Handle ip/arp protocol
            if (eth_hdr->ether_type == htons(ETHERTYPE_IP)) {
                // Got ip packet
                size_t ip_len = recv_len - sizeof(struct ether_header);
                memcpy(ip_packet, packet + sizeof(struct ether_header), ip_len);
                *out_if_idx = if_idx;
                memcpy(out_src_mac, eth_hdr->ether_shost, sizeof(struct ether_addr));
                memcpy(out_dst_mac, eth_hdr->ether_dhost, sizeof(struct ether_addr));
                return ip_len;
            } else if (eth_hdr->ether_type == htons(ETHERTYPE_ARP)) {
                uint8_t *arp_packet = packet + sizeof(struct ether_header);
                size_t arp_len = recv_len - sizeof(struct ether_header);
                if (arp_len != sizeof(arp_packet_t)) {
                    fprintf(stderr, "Broken ARP packet\n");
                    continue;
                }
                arp_packet_t *arp_pkt = (arp_packet_t *) arp_packet;
                struct ether_addr src_mac, dst_mac;
                in_addr_t src_ip, dst_ip;
                memcpy(&src_mac, &arp_pkt->ar_sha, sizeof(struct ether_addr));
                src_ip = arp_pkt->ar_sip;
                memcpy(&dst_mac, &arp_pkt->ar_tha, sizeof(struct ether_addr));
                dst_ip = arp_pkt->ar_tip;

                if (arp_pkt->hdr.ar_op == htons(ARPOP_REPLY)) {
                    // ARP reply, learn it
                    arp_insert_entry(src_ip, if_idx, &src_mac);
                    printf("Learned ARP: %s at %s from %s\n",
                           mac2str((uint8_t *) &src_mac), ip2str(src_ip), if_names[if_idx]);
                } else if (arp_pkt->hdr.ar_op == htons(ARPOP_REQUEST)) {
                    // ARP request
                    int my_if;
                    for (my_if = 0; my_if < NUM_IF; my_if++) {
                        if (if_ips[my_if] == dst_ip) {
                            break;
                        }
                    }
                    if (my_if < NUM_IF) {
                        // request my IP, send ARP reply
                        printf("Sending ARP reply: %s is at %s\n",
                               ip2str(if_ips[my_if]), mac2str((uint8_t *) &if_macs[my_if]));
                        send_arp_reply(if_idx, if_ips[my_if], &if_macs[my_if], src_ip, &src_mac);
                    } else {
                        fprintf(stderr, "Unknown MAC address of %s\n", ip2str(dst_ip));
                    }
                } else {
                    fprintf(stderr, "Unsupported ARP Type\n");
                }
            } else {
                fprintf(stderr, "Unsupported ethernet type: %04x\n", ntohs(eth_hdr->ether_type));
            }
        } else {
            fprintf(stderr, "Broken ethernet packet\n");
        }
    }
}

void send_ip_packet(const uint8_t *ip_packet, size_t ip_len, int if_idx, const struct ether_addr *dst_mac) {
    send_l3_packet(ip_packet, ip_len, if_idx, dst_mac, htons(ETHERTYPE_IP));
}
