#include "obd_user_pids.h"
#include <cstdlib>
#include <cstring>
#include <esp_err.h>

static uint8_t s_pid[OBD_USER_PID_MAX];
static uint8_t s_n;
static int s_val[OBD_USER_PID_MAX];
static uint32_t s_ts[OBD_USER_PID_MAX];
static char s_saved[OBD_USER_NVS_MAX];

static int parseHexByte(const char* p, uint8_t* out)
{
  char* end = nullptr;
  unsigned long v = strtoul(p, &end, 16);
  if (end == p || v > 255UL) {
    return -1;
  }
  *out = (uint8_t)v;
  return 0;
}

static void parseList(const char* s)
{
  s_n = 0;
  if (!s || !s[0]) {
    return;
  }
  const char* p = s;
  while (s_n < OBD_USER_PID_MAX && *p) {
    while (*p == ' ' || *p == '\t' || *p == ',') {
      p++;
    }
    if (!*p) {
      break;
    }
    char hex[8];
    int k = 0;
    while (*p && *p != ',' && k < (int)sizeof(hex) - 1) {
      hex[k++] = *p++;
    }
    hex[k] = 0;
    if (k > 0) {
      uint8_t b;
      if (parseHexByte(hex, &b) == 0) {
        s_pid[s_n] = b;
        s_val[s_n] = 0;
        s_ts[s_n] = 0;
        s_n++;
      }
    }
  }
}

void obdUserPidsInit(nvs_handle_t nvs)
{
  s_n = 0;
  s_saved[0] = 0;
  if (!nvs) {
    return;
  }
  size_t len = sizeof(s_saved);
  if (nvs_get_str(nvs, OBD_USER_NVS_KEY, s_saved, &len) != ESP_OK) {
    return;
  }
  parseList(s_saved);
}

bool obdUserPidsSave(nvs_handle_t nvs, const char* comma_hex)
{
  if (!nvs || !comma_hex) {
    return false;
  }
  char tmp[OBD_USER_NVS_MAX];
  strncpy(tmp, comma_hex, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  parseList(tmp);
  strncpy(s_saved, comma_hex, sizeof(s_saved) - 1);
  s_saved[sizeof(s_saved) - 1] = 0;
  if (nvs_set_str(nvs, OBD_USER_NVS_KEY, s_saved) != ESP_OK) {
    return false;
  }
  nvs_commit(nvs);
  return true;
}

void obdUserPidsGetSavedString(char* out, size_t len)
{
  if (!out || len < 2) {
    return;
  }
  strncpy(out, s_saved, len - 1);
  out[len - 1] = 0;
}

uint8_t obdUserPidsCount(void)
{
  return s_n;
}

uint8_t obdUserPidsByte(uint8_t idx)
{
  return idx < s_n ? s_pid[idx] : 0;
}

void obdUserPidsMarkRead(uint8_t idx, int value)
{
  if (idx >= s_n) {
    return;
  }
  s_val[idx] = value;
  s_ts[idx] = millis();
}

int obdUserPidsAppendLiveJson(char* buf, int space, uint32_t tnow_ms)
{
  if (space < 16) {
    return 0;
  }
  if (s_n == 0) {
    return snprintf(buf, (size_t)space, ",\"user\":[]");
  }
  int n = snprintf(buf, (size_t)space, ",\"user\":[");
  if (n < 0 || n >= space) {
    return 0;
  }
  int total = n;
  for (uint8_t i = 0; i < s_n; i++) {
    unsigned age = 999999u;
    if (s_ts[i] && tnow_ms >= s_ts[i]) {
      age = (unsigned)(tnow_ms - s_ts[i]);
    }
    int w = snprintf(buf + total, (size_t)(space - total),
                       "{\"pid\":%u,\"value\":%d,\"age\":%u}%s",
                       (unsigned)(0x100u | s_pid[i]), s_val[i], age,
                       (i + 1 < s_n) ? "," : "");
    if (w < 0 || total + w >= space) {
      break;
    }
    total += w;
  }
  if (total + 2 < space) {
    int c = snprintf(buf + total, (size_t)(space - total), "]");
    if (c > 0) {
      total += c;
    }
  }
  return total;
}
