#pragma once

#include <Arduino.h>
#include <nvs.h>

#define OBD_USER_PID_MAX 24
#define OBD_USER_NVS_MAX 128
#define OBD_USER_NVS_KEY "OBD_USER_HEX"

void obdUserPidsInit(nvs_handle_t h);
bool obdUserPidsSave(nvs_handle_t h, const char* comma_hex);
void obdUserPidsGetSavedString(char* out, size_t len);
uint8_t obdUserPidsCount(void);
uint8_t obdUserPidsByte(uint8_t idx);
void obdUserPidsMarkRead(uint8_t idx, int value);
int obdUserPidsAppendLiveJson(char* buf, int space, uint32_t tnow_ms);
