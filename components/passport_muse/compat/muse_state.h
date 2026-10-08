#pragma once
#include <stdbool.h>
typedef struct { int battery_pct, battery_mv; bool charging, usb; } muse_power_t;
muse_power_t muse_state_power(void);
