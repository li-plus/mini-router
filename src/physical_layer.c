#include "physical_layer.h"
#include <pcap/pcap.h>
#include <sys/epoll.h>
#include <string.h>
#include <time.h>

static pcap_t *pcap_handle[MAX_IF];
static int epfd;

RC physical_init() {
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1()");
        return PHYSICAL_INIT_FAIL;
    }
    char error_buffer[PCAP_ERRBUF_SIZE];
    for (int i = 0; i < NUM_IF; i++) {
        pcap_handle[i] = pcap_open_live(if_names[i], BUFSIZ, 1, 1, error_buffer);
        if (pcap_handle[i] == NULL) {
            fprintf(stderr, "Cannot open pcap for interface %s\n", if_names[i]);
            return PHYSICAL_INIT_FAIL;
        }
        pcap_setnonblock(pcap_handle[i], 1, error_buffer);
        int fd = pcap_get_selectable_fd(pcap_handle[i]);
        if (fd < 0) {
            fprintf(stderr, "Cannot get FD of pcap handle. Are you on Linux?\n");
            return PHYSICAL_INIT_FAIL;
        }
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.u32 = i;     // interface index
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) < 0) {
            perror("epoll_ctl()");
            return PHYSICAL_INIT_FAIL;
        }
    }
    return 0;
}

uint64_t get_clock_ms() {
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (uint64_t) tp.tv_sec * 1000 + (uint64_t) tp.tv_nsec / 1000000;
}

void send_packet(const uint8_t *packet, size_t len, int if_idx) {
    if (pcap_inject(pcap_handle[if_idx], packet, len) == PCAP_ERROR) {
//        fprintf(stderr, "pcap error: %s\n", pcap_geterr(pcap_handle[if_idx]));
    }
}

size_t recv_packet(int timeout_ms, uint8_t *packet, int *out_if_idx) {
    struct epoll_event event;
    int num_events = epoll_wait(epfd, &event, 1, timeout_ms);
    if (num_events > 0) {
        if (event.events & EPOLLIN) {
            int if_idx = event.data.u32;
            struct pcap_pkthdr hdr;
            const uint8_t *next_pkt = pcap_next(pcap_handle[if_idx], &hdr);
            if (next_pkt != NULL) {
                // return the first active interface
                memcpy(packet, next_pkt, hdr.caplen);
                *out_if_idx = if_idx;
                return hdr.caplen;
            }
        }
    }
    return 0;
}
