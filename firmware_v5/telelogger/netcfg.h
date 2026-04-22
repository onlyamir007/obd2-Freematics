#ifndef NETCFG_H
#define NETCFG_H
#include <stdint.h>

/* Loaded from NVS; WiFi / BLE / cell toggles apply after reboot (except when noted). */
extern uint8_t g_rt_wifi_ap;
extern uint8_t g_rt_wifi_sta;
extern uint8_t g_rt_ble;
extern uint8_t g_rt_cell;

#ifndef HAVE_RUNTIME_CELL
#define HAVE_RUNTIME_CELL 0
#endif

void netcfg_reload_from_nvs(void);

#endif
