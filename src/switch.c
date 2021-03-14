#include "physical_layer.h"
#include "config.h"
#include <string.h>

// ===== MAC TABLE =====
typedef struct mac_entry {
    struct ether_addr mac;
    int if_idx;
} mac_entry_t;

#define MAC_TABLE_CAPACITY 1024
struct {
    mac_entry_t entries[MAC_TABLE_CAPACITY];
    int size;
} mac_table;

static mac_entry_t *get_mac_entry(const struct ether_addr *mac) {
    for (int i = 0; i < mac_table.size; i++) {
        mac_entry_t *entry = &mac_table.entries[i];
        if (memcmp(&entry->mac, mac, sizeof(struct ether_addr)) == 0) {
            return entry;
        }
    }
    return NULL;
}

static RC insert_mac_entry(const struct ether_addr *mac, int if_idx) {
    mac_entry_t *entry = get_mac_entry(mac);
    if (entry) {
        // Update existing mac entry
        entry->if_idx = if_idx;
    } else {
        // Insert a new mac entry
        if (mac_table.size >= MAC_TABLE_CAPACITY) {
            return OVERFLOW_ERROR;
        }
        fprintf(stderr, "Learned mac of %s is %s\n", if_names[if_idx], mac2str((uint8_t *) mac));
        entry = &mac_table.entries[mac_table.size];
        mac_table.size++;
        memcpy(&entry->mac, mac, sizeof(struct ether_addr));
        entry->if_idx = if_idx;
    }
    return 0;
}

void print_mac_table() {
    printf("=========== MAC TABLE ===========\n");
    char separator[] = "+-------------------+-----------+";
    printf("%s\n", separator);
    printf("| %17s | %9s |\n", "MAC", "IF");
    printf("%s\n", separator);
    for (int i = 0; i < mac_table.size; i++) {
        mac_entry_t *entry = &mac_table.entries[i];
        printf("| %17s | %9s |\n", mac2str((uint8_t *) &entry->mac), if_names[entry->if_idx]);
    }
    printf("%s\n", separator);
}

void broadcast_packet(uint8_t *packet, size_t len, int if_idx) {
    for (int i = 0; i < NUM_IF; i++) {
        if (i != if_idx) {
            send_packet(packet, len, i);
        }
    }
}

static const struct ether_addr BROADCAST_MAC = {"\xff\xff\xff\xff\xff\xff"};

_Noreturn void run_switch() {
    int print_interval = 5000;
    uint64_t last_time_fire = 0;
    while (1) {
        uint64_t curr_time = get_clock_ms();
        if (curr_time - last_time_fire >= print_interval) {
            print_mac_table();
            last_time_fire = curr_time;
        }
        int if_idx;
        uint8_t packet[BUFSIZ];
        size_t len = recv_packet(1000, packet, &if_idx);
        if (len == 0) {
            fprintf(stderr, "Recv packet time out for 1s\n");
            continue;
        }
        if (len < sizeof(struct ether_header)) {
            fprintf(stderr, "Broken ethernet packet\n");
        }
        struct ether_header *eth_hdr = (struct ether_header *) packet;
        // Learn source mac address
        insert_mac_entry((struct ether_addr *) eth_hdr->ether_shost, if_idx);
        // Check dest mac address
        if (memcmp(eth_hdr->ether_dhost, &BROADCAST_MAC, sizeof(struct ether_addr)) == 0) {
            // Dest mac is broadcast address
            broadcast_packet(packet, len, if_idx);
        } else {
            // Find next interface by dest mac
            mac_entry_t *mac_entry = get_mac_entry((struct ether_addr *) eth_hdr->ether_dhost);
            if (mac_entry) {
                // Dst mac found: forward this packet to dst interface
                send_packet(packet, len, mac_entry->if_idx);
            } else {
                // Dst mac not found: broadcast
                fprintf(stderr, "Dest MAC addr %s not found, broadcasting\n", mac2str(eth_hdr->ether_dhost));
                broadcast_packet(packet, len, if_idx);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: ./switch <config>");
        return 1;
    }
    RC rc;
    char *config_path = argv[1];
    rc = config_init(config_path);
    if (rc) { return rc; }
    rc = physical_init();
    if (rc) { return rc; }
    run_switch();
    return 0;
}
