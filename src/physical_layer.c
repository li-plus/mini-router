#include "physical_layer.h"
#include <pcap/pcap.h>
#include <string.h>
#include <time.h>

static pcap_t *pcap_handle[MAX_IF];

RC physical_init() {
    char error_buffer[PCAP_ERRBUF_SIZE];
    for (int i = 0; i < NUM_IF; i++) {
        pcap_handle[i] = pcap_open_live(if_names[i], BUFSIZ, 1, 1, error_buffer);
        if (pcap_handle[i] == NULL) {
            fprintf(stderr, "Cannot open pcap for interface %s\n", if_names[i]);
            return PHYSICAL_INIT_FAIL;
        }
        pcap_setnonblock(pcap_handle[i], 1, error_buffer);
    }
    return 0;
}

void send_packet(const uint8_t *packet, size_t len, int if_idx) {
    if (pcap_inject(pcap_handle[if_idx], packet, len) == PCAP_ERROR) {
//        fprintf(stderr, "pcap error: %s\n", pcap_geterr(pcap_handle[if_idx]));
    }
}

size_t recv_packet(int timeout_sec, uint8_t *packet, int *out_if_idx) {
    // Round robin
    clock_t start = clock();
    int if_idx = 0;
    while (1) {
        struct pcap_pkthdr hdr;
        const uint8_t *next_pkt = pcap_next(pcap_handle[if_idx], &hdr);
        if (next_pkt != NULL) {
            memcpy(packet, next_pkt, hdr.caplen);
            *out_if_idx = if_idx;
            return hdr.caplen;
        }
        if_idx = (if_idx + 1) % NUM_IF;
        // Handle timeout
        if ((clock() - start) / CLOCKS_PER_SEC >= timeout_sec) {
            return 0;
        }
    }
}
