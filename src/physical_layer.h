#pragma once

#include "error.h"
#include "config.h"
#include <inttypes.h>

RC physical_init();

void send_packet(const uint8_t *packet, size_t len, int if_idx);

size_t recv_packet(int timeout_sec, uint8_t *packet, int *out_if_idx);
