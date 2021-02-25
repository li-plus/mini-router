#pragma once

#include <arpa/inet.h>
#include <inttypes.h>

// UDP port
#define RIP_UDP_PORT 0x0208

// Version
#define RIP_V2 0x02

// Command
#define RIP_CMD_REQUEST 0x01
#define RIP_CMD_RESPONSE 0x02

// Address Family
#define RIP_AF_UNSPECIFIED 0x0000
#define RIP_AF_IP 0x0002

// Metric
#define RIP_METRIC_INF 16

// RIP entry
#define RIP_MAX_ENTRIES 25

typedef struct rip_hdr {
    uint8_t command;
    uint8_t version;
    uint16_t unused;
} rip_hdr_t;

typedef struct rip_entry {
    uint16_t addr_family;
    uint16_t tag;
    in_addr_t ip;
    in_addr_t mask;
    in_addr_t next_hop;
    uint32_t metric;
} rip_entry_t;
