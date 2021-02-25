#include "config.h"
#include <json-c/json.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <string.h>

int NUM_IF;
in_addr_t if_ips[MAX_IF];
in_addr_t if_masks[MAX_IF];
char *if_names[MAX_IF];
struct ether_addr if_macs[MAX_IF];

RC config_init(const char *config_path) {
    // Parse config json file to get IF, IP, MASK
    json_object *root = json_object_from_file(config_path);
    if (root == NULL) {
        fprintf(stderr, "Config file parse failed: %s\n", config_path);
        return CONFIG_PARSE_FAIL;
    }
    NUM_IF = json_object_array_length(root);
    for (size_t i = 0; i < NUM_IF; i++) {
        json_object *iface = json_object_array_get_idx(root, i);
        const char *if_name = json_object_get_string(json_object_object_get(iface, "if_name"));
        const char *ip_str = json_object_get_string(json_object_object_get(iface, "ip"));
        const char *mask_str = json_object_get_string(json_object_object_get(iface, "mask"));
        if_names[i] = strdup(if_name);
        inet_aton(ip_str, (struct in_addr *) &if_ips[i]);
        inet_aton(mask_str, (struct in_addr *) &if_masks[i]);
        printf("Load interface %s: %s %s\n", if_name, ip_str, mask_str);
    }
    json_object_put(root);
    // Find mac address of interfaces
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) < 0) {
        fprintf(stderr, "Cannot get interface address\n");
        return CONFIG_INIT_FAIL;
    }
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        for (int i = 0; i < NUM_IF; i++) {
            if (ifa->ifa_addr->sa_family == AF_PACKET && strcmp(ifa->ifa_name, if_names[i]) == 0) {
                struct ether_addr *mac = &if_macs[i];
                memcpy(mac, ((struct sockaddr_ll *) ifa->ifa_addr)->sll_addr, sizeof(struct ether_addr));
                printf("Found MAC address of interface %s: %s\n", if_names[i], mac2str((uint8_t *) mac));
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return 0;
}

void config_destroy() {
    for (int i = 0; i < NUM_IF; i++) {
        free(if_names[i]);
    }
}
