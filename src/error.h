#pragma once

#ifndef __cplusplus
typedef enum {
    false = 0,
    true = 1,
} bool;
#endif

// Return Code
typedef int RC;
#define CONFIG_PARSE_FAIL 100
#define CONFIG_INIT_FAIL 101
#define PHYSICAL_INIT_FAIL 102
#define UNKNOWN_MAC_ADDR 103
#define OVERFLOW_ERROR 104
#define OUT_OF_RANGE_ERROR 105
