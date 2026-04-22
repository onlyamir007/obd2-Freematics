#include "obd_user_pids.h"
#include <esp_err.h>
#include <string.h>

static nvs_handle_t s_nvs;
static char s_cached[OBD_USER_NVS_MAX];

void obdUserPidsInit(nvs_handle_t nvs)
{
  s_nvs = nvs;
  s_cached[0] = 0;
  if (!nvs) {
    return;
  }
  size_t l = sizeof(s_cached);
  if (nvs_get_str(nvs, OBD_USER_NVS_KEY, s_cached, &l) != ESP_OK) {
    s_cached[0] = 0;
  }
}

bool obdUserPidsSave(nvs_handle_t nvs, const char* comma_hex)
{
  if (!nvs) {
    return false;
  }
  const char* s = comma_hex ? comma_hex : "";
  strncpy(s_cached, s, sizeof(s_cached) - 1);
  s_cached[sizeof(s_cached) - 1] = 0;
  if (nvs_set_str(nvs, OBD_USER_NVS_KEY, s_cached) != ESP_OK) {
    return false;
  }
  nvs_commit(nvs);
  s_nvs = nvs;
  return true;
}

void obdUserPidsGetSavedString(char* out, size_t len)
{
  if (!out || len < 2) {
    return;
  }
  strncpy(out, s_cached, len - 1);
  out[len - 1] = 0;
}

uint8_t obdUserPidsCount(void) { return 0; }
uint8_t obdUserPidsByte(uint8_t idx) { (void)idx; return 0; }
void obdUserPidsMarkRead(uint8_t idx, int value) { (void)idx; (void)value; }

int obdUserPidsAppendLiveJson(char* buf, int space, uint32_t tnow_ms)
{
  (void)tnow_ms;
  if (space < 16 || !buf) {
    return 0;
  }
  return snprintf(buf, (size_t)space, ",\"user\":[]");
}
