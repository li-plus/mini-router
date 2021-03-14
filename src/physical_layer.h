#pragma once

#include "error.h"
#include "config.h"
#include <inttypes.h>

RC physical_init();

uint64_t get_clock_ms();

void send_packet(const uint8_t *packet, size_t len, int if_idx);

size_t recv_packet(int timeout_ms, uint8_t *packet, int *out_if_idx);
