#include "ether_layer.h"
#include "physical_layer.h"
#include "config.h"
#include "rip.h"
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/udp.h>
#include <string.h>
#include <stdlib.h>

#define SWAP(a, b) do { typeof(a) __tmp = a; (a) = (b); (b) = __tmp; } while (0)

// ===== ROUTE TABLE =====
typedef struct route_entry {
    in_addr_t dst_ip;   // Destination IP address
    in_addr_t mask;     // Prefix mask
    in_addr_t next_hop; // Next hop IP address (0 if direct)
    int if_idx;         // Forward port
    uint32_t metric;    // RIP metric
} route_entry_t;

#define ROUTE_TABLE_CAPACITY 65536
struct {
    route_entry_t entries[ROUTE_TABLE_CAPACITY];
    int size;
} route_table;

static int count_ones(in_addr_t mask) {
    int cnt = 0;
    while (mask) {
        cnt++;
        mask = mask & (mask - 1);
    }
    return cnt;
}

static route_entry_t *get_route(in_addr_t dst_ip) {
    route_entry_t *dst_route = NULL;
    int max_prefix_len = -1;
    for (int route_idx = 0; route_idx < route_table.size; route_idx++) {
        route_entry_t *route = &route_table.entries[route_idx];
        if (route->dst_ip == (dst_ip & route->mask)) {
            if (max_prefix_len < count_ones(route->mask)) {
                max_prefix_len = count_ones(route->mask);
                dst_route = route;
            }
        }
    }
    return dst_route;
}

static RC insert_route(in_addr_t dst_ip, in_addr_t mask, in_addr_t next_hop, int if_idx, uint32_t metric) {
    if (route_table.size >= ROUTE_TABLE_CAPACITY) {
        fprintf(stderr, "Route table overflow\n");
        return OVERFLOW_ERROR;
    }
    route_entry_t *entry = &route_table.entries[route_table.size++];
    *entry = (route_entry_t) {
            .dst_ip = dst_ip & mask,
            .mask = mask,
            .next_hop = next_hop,
            .if_idx = if_idx,
            .metric = metric,
    };
    return 0;
}

static inline RC erase_route(int pos) {
    if (pos >= route_table.size) {
        fprintf(stderr, "Route table out of range\n");
        return OUT_OF_RANGE_ERROR;
    }
    memmove(&route_table.entries[pos], &route_table.entries[pos + 1],
            (route_table.size - 1 - pos) * sizeof(route_entry_t));
    route_table.size--;
    return 0;
}

static void print_route_table() {
    printf("====================== ROUTE TABLE ======================\n");
    char separator[] = "+--------------------+-----------------+-------+--------+";
    printf("%s\n", separator);
    printf("| %18s | %15s | %5s | %6s |\n", "IP / MASK", "NEXT_HOP", "IF", "METRIC");
    printf("%s\n", separator);
    for (int i = 0; i < route_table.size; i++) {
        route_entry_t *route = &route_table.entries[i];
        char dst_ip[16], next_hop[16];
        strcpy(dst_ip, ip2str(route->dst_ip));
        strcpy(next_hop, ip2str(route->next_hop));
        printf("| %15s/%2d | %15s | %5s | %6u |\n",
               dst_ip, count_ones(route->mask), next_hop, if_names[route->if_idx], route->metric);
    }
    printf("%s\n", separator);
}

// ===== CHECKSUM =====
static uint16_t get_cksum16(const uint8_t *packet, size_t len) {
    uint32_t cksum = 0;
    size_t i;
    for (i = 0; i < len; i += 2) {
        cksum += *(uint16_t *) &packet[i];
    }
    if (i < len) {
        cksum += packet[i];
    }
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return (uint16_t) ~cksum;
}

// ===== IP =====
static inline void set_ip_checksum(uint8_t *ip_packet) {
    struct iphdr *ip_hdr = (struct iphdr *) ip_packet;
    size_t hdr_len = ip_hdr->ihl * 4;
    ip_hdr->check = 0;
    ip_hdr->check = get_cksum16(ip_packet, hdr_len);
}

static void ip_forward(uint8_t *ip_packet, size_t ip_len, const route_entry_t *route) {
    struct iphdr *ip_hdr = (struct iphdr *) ip_packet;
    int if_next = route->if_idx;
    in_addr_t next_hop = route->next_hop;
    if (next_hop == 0) {
        // Directly connected
        next_hop = ip_hdr->daddr;
    }
    struct ether_addr next_hop_mac;
    if (arp_get_mac(next_hop, if_next, &next_hop_mac) == 0) {
        // Forward this packet to the next hop
        ip_hdr->ttl--;
        set_ip_checksum(ip_packet);
        send_ip_packet(ip_packet, ip_len, if_next, &next_hop_mac);
    } else {
        fprintf(stderr, "MAC not found for IP %s\n", ip2str(next_hop));
    }
}

// ===== ICMP =====
static inline void set_icmp_checksum(const uint8_t *icmp_packet, size_t icmp_len) {
    struct icmphdr *icmp_hdr = (struct icmphdr *) icmp_packet;
    icmp_hdr->checksum = 0;
    icmp_hdr->checksum = get_cksum16(icmp_packet, icmp_len);
}

static void send_icmp_msg(uint8_t *ip_packet, size_t ip_len, int if_idx, uint8_t icmp_type, uint8_t icmp_code,
                          const struct ether_addr *dst_mac) {
    struct iphdr *ip_hdr = (struct iphdr *) ip_packet;
    size_t ip_hdr_len = ip_hdr->ihl * 4;
    // ICMP payload should be source packet's IP header + first 64 bits of IP payload.
    size_t icmp_body_len = ip_hdr_len + 8;
    if (ip_len >= icmp_body_len) {
        // ICMP packet
        uint8_t *icmp_packet = ip_packet + ip_hdr_len;
        uint8_t *icmp_body = icmp_packet + sizeof(struct icmphdr);
        memcpy(icmp_body, ip_packet, icmp_body_len);
        struct icmphdr *icmp_hdr = (struct icmphdr *) icmp_packet;
        icmp_hdr->type = icmp_type;
        icmp_hdr->code = icmp_code;
        memset(&icmp_hdr->un, 0, sizeof(icmp_hdr->un));
        size_t icmp_len = icmp_body_len + sizeof(struct icmphdr);
        set_icmp_checksum(icmp_packet, icmp_len);
        // IP packet
        ip_len = ip_hdr_len + icmp_len;
        ip_hdr->tot_len = htons(ip_len);
        ip_hdr->daddr = ip_hdr->saddr;
        ip_hdr->saddr = if_ips[if_idx];
        ip_hdr->ttl = IPDEFTTL;
        set_ip_checksum(ip_packet);
        // Send packet
        send_ip_packet(ip_packet, ip_len, if_idx, dst_mac);
    }
}

// ===== RIP =====
static const char RIP_MULTICAST_IP_STR[] = "224.0.0.9";
static in_addr_t RIP_MULTICAST_IP;
static struct ether_addr RIP_MULTICAST_MAC;
static const int RIP_UPDATE_TIME = 5000;   // send RIP response every 5 seconds

static void send_rip_response(int if_idx) {
    uint8_t ip_packet[BUFSIZ];
    struct iphdr *ip_hdr = (struct iphdr *) ip_packet;
    struct udphdr *udp_hdr = (struct udphdr *) (ip_hdr + 1);
    rip_hdr_t *rip_hdr = (rip_hdr_t *) (udp_hdr + 1);
    rip_entry_t *rip_entries = (rip_entry_t *) (rip_hdr + 1);

    // RIP packet
    *rip_hdr = (rip_hdr_t) {
            .command = RIP_CMD_RESPONSE,
            .version = RIP_V2,
            .unused = 0,
    };

    size_t rip_resp_num = 0;
    for (int i = 0; i < route_table.size; i++) {
        route_entry_t *route = &route_table.entries[i];
        uint32_t metric;
        if (route->if_idx == if_idx) {
            // RFC 2453 3.4.3 Split horizon with poisoned reverse
            metric = htonl(RIP_METRIC_INF);
        } else {
            metric = htonl(route->metric);
        }
        rip_entries[rip_resp_num] = (rip_entry_t) {
                .addr_family = htons(RIP_AF_IP),
                .tag = 0,
                .ip = route->dst_ip,
                .mask = route->mask,
                .next_hop = 0, // TODO: send next hop if directly connectable
                .metric = metric
        };
        rip_resp_num++;
        if (rip_resp_num == RIP_MAX_ENTRIES || i == route_table.size - 1) {
            // Need to send this route table fragment
            size_t rip_len = sizeof(rip_hdr_t) + rip_resp_num * sizeof(rip_entry_t);

            // UDP packet
            size_t udp_len = sizeof(struct udphdr) + rip_len;
            *udp_hdr = (struct udphdr) {
                    .source = htons(RIP_UDP_PORT),
                    .dest = htons(RIP_UDP_PORT),
                    .len = htons(udp_len),
                    .check = 0, // TODO: UDP checksum
            };
            // IP packet
            size_t ip_len = sizeof(struct iphdr) + udp_len;
            *ip_hdr = (struct iphdr) {
                    .version = 4,
                    .ihl = sizeof(struct iphdr) / 4,
                    .tos = IPTOS_PREC_INTERNETCONTROL,
                    .tot_len = htons(ip_len),
                    .id = (uint16_t) rand(),
                    .frag_off = 0,
                    .ttl = 1,
                    .protocol = IPPROTO_UDP,
                    .check = 0,
                    .saddr = if_ips[if_idx],
                    .daddr = RIP_MULTICAST_IP,
            };
            set_ip_checksum((uint8_t *) ip_hdr);
            // Send RIP response
            send_ip_packet(ip_packet, ip_len, if_idx, &RIP_MULTICAST_MAC);
            // clear RIP entries
            rip_resp_num = 0;
        }
    }
}

static void handle_udp_packet(uint8_t *ip_packet, size_t ip_len, int if_idx) {
    struct iphdr *ip_hdr = (struct iphdr *) ip_packet;
    size_t ip_hdr_len = ip_hdr->ihl * 4;
    struct udphdr *udp_hdr = (struct udphdr *) (ip_packet + ip_hdr_len);
    size_t udp_len = ip_len - ip_hdr_len;
    if (udp_hdr->len != htons(udp_len)) {
        fprintf(stderr, "Broken UDP packet\n");
        return;
    }
    if (udp_hdr->source == htons(RIP_UDP_PORT) && udp_hdr->dest == htons(RIP_UDP_PORT)) {
        // Got RIP packet
        rip_hdr_t *rip_hdr = (rip_hdr_t *) (udp_hdr + 1);
        size_t rip_len = udp_len - sizeof(struct udphdr);
        rip_entry_t *rip_entries = (rip_entry_t *) (rip_hdr + 1);
        size_t rip_entry_len = rip_len - sizeof(rip_hdr_t);
        if (rip_entry_len % sizeof(rip_entry_t) != 0) {
            fprintf(stderr, "Broken RIP packet\n");
            return;
        }
        int rip_num_entries = (int) (rip_entry_len / sizeof(rip_entry_t));
        if (rip_hdr->command == RIP_CMD_REQUEST) {
            // Handle RIP request
            printf("Received RIP request from %s via %s\n", ip2str(if_ips[if_idx]), if_names[if_idx]);
            if (rip_num_entries == 1 && rip_entries[0].tag == htons(RIP_AF_UNSPECIFIED) &&
                rip_entries[0].metric == htonl(RIP_METRIC_INF)) {
                // RFC 2453 3.9.1: special case: send the entire route table
                printf("Sending RIP response on request via %s\n", if_names[if_idx]);
                send_rip_response(if_idx);
            } else {
                fprintf(stderr, "RIP request of specific entries is not yet implemented\n");
            }
        } else if (rip_hdr->command == RIP_CMD_RESPONSE) {
            // Handle RIP response
            printf("Received RIP response from %s via %s\n", ip2str(if_ips[if_idx]), if_names[if_idx]);
            for (int rip_i = 0; rip_i < rip_num_entries; rip_i++) {
                rip_entry_t *rip_entry = &rip_entries[rip_i];
                if (rip_entry->next_hop != 0) {
                    printf("Next hop %s is not yet supported\n", ip2str(rip_entry->next_hop));
                    continue;
                }
                // Find route in route table according to this RIP entry. Exact matching.
                int route_i;
                route_entry_t *route = NULL;
                for (route_i = 0; route_i < route_table.size; route_i++) {
                    route = &route_table.entries[route_i];
                    if (route->dst_ip == rip_entry->ip && route->mask == rip_entry->mask) {
                        break;
                    }
                }
                if (route_i != route_table.size) {
                    // Route found, update route according to RIP entry
                    if (rip_entry->metric == htonl(RIP_METRIC_INF)) {
                        // Network is unreachable from source IP
                        if (route->next_hop == ip_hdr->saddr) {
                            erase_route(route_i);
                        }
                    } else if (ntohl(rip_entry->metric) + 1 < route->metric) {
                        // Metric is smaller from this next hop. Update this route.
                        route->next_hop = ip_hdr->saddr;
                        route->if_idx = if_idx;
                        route->metric = ntohl(rip_entry->metric) + 1;
                    }
                } else {
                    // Route not found, insert a new route
                    if (ntohl(rip_entry->metric) < RIP_METRIC_INF) {
                        insert_route(rip_entry->ip, rip_entry->mask, ip_hdr->saddr, if_idx,
                                     ntohl(rip_entry->metric) + 1);
                    }
                }
            }
        } else {
            fprintf(stderr, "Unknown RIP command %02x\n", rip_hdr->command);
        }
    } else {
        fprintf(stderr, "Unsupported UDP packet\n");
    }
}

RC router_init() {
    RC rc;
    // Insert interface IP into route table
    for (int i = 0; i < NUM_IF; i++) {
        insert_route(if_ips[i], if_masks[i], 0, i, 1);
    }
    // Get RIP multicast address
    inet_aton(RIP_MULTICAST_IP_STR, (struct in_addr *) &RIP_MULTICAST_IP);
    rc = arp_get_mac(RIP_MULTICAST_IP, 0, &RIP_MULTICAST_MAC);
    printf("RIP multicast IP is %s, MAC address is %s\n",
           ip2str(RIP_MULTICAST_IP), mac2str((uint8_t *) &RIP_MULTICAST_MAC));
    if (rc) { return rc; }
    return 0;
}

_Noreturn void run_router() {
    uint64_t last_timer_fire = 0;
    while (1) {
        // Timer
        uint64_t curr_time = get_clock_ms();
        if (curr_time - last_timer_fire >= RIP_UPDATE_TIME) {
            printf("Main timer fired, sending RIP response to all interfaces\n");
            for (int i = 0; i < NUM_IF; i++) {
                send_rip_response(i);
            }
            print_arp_table();
            print_route_table();
            last_timer_fire = curr_time;
        }
        struct ether_addr src_mac, dst_mac;
        int if_idx;
        uint8_t ip_packet[BUFSIZ];
        size_t ip_len = recv_ip_packet(1000, ip_packet, &if_idx, &src_mac, &dst_mac);
        if (ip_len == 0) {
            fprintf(stderr, "Recv packet time out for 1s\n");
            continue;
        }
        if (ip_len < sizeof(struct iphdr)) { continue; }
        struct iphdr *ip_hdr = (struct iphdr *) ip_packet;
        if (htons(ip_len) != ip_hdr->tot_len) { continue; }
        size_t ip_hdr_len = ip_hdr->ihl * 4;
        if (ip_len < ip_hdr_len) { continue; }
        // validate checksum
        uint16_t org_cksum = ip_hdr->check;
        set_ip_checksum(ip_packet);
        if (org_cksum != ip_hdr->check) {
            fprintf(stderr, "Incorrect IP checksum, expected %04x, got %04x\n", ip_hdr->check, org_cksum);
            continue;
        }
        // Check whether dst ip is mine
        int dst_if;
        for (dst_if = 0; dst_if < NUM_IF; dst_if++) {
            if (if_ips[dst_if] == ip_hdr->daddr) {
                break;
            }
        }
        // Check destination IP
        if (IN_MULTICAST(ntohl(ip_hdr->daddr))) {
            if (ip_hdr->daddr == RIP_MULTICAST_IP) {
                // Dst IP is RIP multicast address
                if (ip_hdr->protocol == IPPROTO_UDP) {
                    handle_udp_packet(ip_packet, ip_len, if_idx);
                } else {
                    fprintf(stderr, "Unsupported IP protocol %02x\n", ip_hdr->protocol);
                }
            }
        } else if (dst_if < NUM_IF) {
            // Dst IP is router's interface
            if (ip_hdr->protocol == IPPROTO_UDP) {
                handle_udp_packet(ip_packet, ip_len, if_idx);
            } else if (ip_hdr->protocol == IPPROTO_ICMP) {
                // Get ICMP echo (request)
                uint8_t *icmp_packet = ip_packet + ip_hdr_len;
                size_t icmp_len = ip_len - ip_hdr_len;
                struct icmphdr *icmp_hdr = (struct icmphdr *) icmp_packet;
                if (icmp_hdr->type == ICMP_ECHO) {
                    printf("Sending ICMP reply to %s via %s\n", ip2str(ip_hdr->saddr), if_names[if_idx]);
                    // Init ICMP packet
                    icmp_hdr->type = ICMP_ECHOREPLY;
                    set_icmp_checksum(icmp_packet, icmp_len);
                    // Init IP packet
                    SWAP(ip_hdr->saddr, ip_hdr->daddr);
                    ip_hdr->ttl = IPDEFTTL;
                    set_ip_checksum(ip_packet);
                    send_ip_packet(ip_packet, ip_len, if_idx, &src_mac);
                } else {
                    fprintf(stderr, "Unsupported ICMP type %02x\n", icmp_hdr->type);
                }
            } else {
                fprintf(stderr, "Unsupported IP protocol %02x\n", ip_hdr->protocol);
            }
        } else {
            // Dst IP is not router's interface: query route table, find next hop, and forward
            route_entry_t *route = get_route(ip_hdr->daddr);
            if (route != NULL) {
                // Found route to host, forward this packet
                if (ip_hdr->ttl > 1) {
                    ip_forward(ip_packet, ip_len, route);
                } else {
                    fprintf(stderr, "Zero TTL. Sending ICMP Time Exceeded Message\n");
                    send_icmp_msg(ip_packet, ip_len, if_idx, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, &src_mac);
                }
            } else {
                fprintf(stderr, "No route to host %s. Sending ICMP Destination Unreachable Message\n",
                        ip2str(ip_hdr->daddr));
                send_icmp_msg(ip_packet, ip_len, if_idx, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, &src_mac);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: ./router <config>");
        return 1;
    }
    RC rc;
    char *config_path = argv[1];
    rc = config_init(config_path);
    if (rc) { return rc; }
    rc = physical_init();
    if (rc) { return rc; }
    rc = ether_init();
    if (rc) { return rc; }
    rc = router_init();
    if (rc) { return rc; }
    run_router();
    return 0;
}
