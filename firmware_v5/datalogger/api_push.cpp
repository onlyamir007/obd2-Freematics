#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <nvs.h>
#include "api_push.h"

#if defined(__GNUC__) && !defined(__INTELLISENSE__)
extern "C" int __attribute__((weak)) api_push_post_cell(const char* url, const char* body, const char* apiKey, char* err, size_t errlen)
{
  (void)url;
  (void)body;
  (void)apiKey;
  (void)err;
  (void)errlen;
  return API_PUSH_NO_ALT_NET;
}
extern "C" int __attribute__((weak)) api_push_link_available(void)
{
  return (WiFi.status() == WL_CONNECTED) ? 1 : 0;
}
#else
extern "C" int api_push_post_cell(const char* url, const char* body, const char* apiKey, char* err, size_t errlen);
extern "C" int api_push_link_available(void);
#endif

static nvs_handle_t s_nvs;
static char s_dev[16];
static char s_base[128];
static char s_key[96];
static char s_pathA[64];
static char s_pathI[64];
static uint8_t s_en;
static uint8_t s_authOk;
static uint16_t s_ivlMin;
static uint32_t s_win0;
#define AGG_N 56
struct AggSlot
{
  uint16_t id;
  uint32_t n;
  double sum;
  uint8_t used;
};
static AggSlot s_slot[AGG_N];
int api_push_parse_url(const char* url, char* host, size_t hmax, char* path, size_t pmax, uint16_t* port, int* is_https)
{
  if (!is_https || !port) {
    return -1;
  }
  *is_https = 0;
  if (strncmp(url, "https://", 8) == 0) {
    *is_https = 1;
    *port = 443;
    url += 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    *port = 80;
    url += 7;
  } else {
    return -1;
  }
  const char* slash = strchr(url, '/');
  const char* hend = slash ? slash : url + strlen(url);
  const char* colon = 0;
  for (const char* x = url; x < hend; x++) {
    if (*x == ':') {
      colon = x;
      break;
    }
  }
  if (colon) {
    size_t hl = (size_t)(colon - url);
    if (hl == 0 || hl >= hmax) {
      return -1;
    }
    memcpy(host, url, hl);
    host[hl] = 0;
    *port = (uint16_t)atoi(colon + 1);
  } else {
    size_t hl = (size_t)(hend - url);
    if (hl == 0 || hl >= hmax) {
      return -1;
    }
    memcpy(host, url, hl);
    host[hl] = 0;
  }
  if (slash) {
    snprintf(path, pmax, "%s", slash);
  } else {
    snprintf(path, pmax, "/");
  }
  return 0;
}
static int read_http_status(Stream& c)
{
  char line[48];
  size_t n = 0;
  uint32_t t0 = millis();
  while (c.available() == 0 && (millis() - t0) < 20000) {
    delay(1);
  }
  if (c.available() == 0) {
    return 0;
  }
  while (c.available() && n + 1 < sizeof(line)) {
    int ch = c.read();
    if (ch < 0) {
      break;
    }
    if (ch == '\n') {
      line[n] = 0;
      break;
    }
    if (ch != '\r') {
      line[n++] = (char)ch;
    }
  }
  line[n] = 0;
  int a = 0, b = 0, s = 0;
  if (sscanf(line, "HTTP/%d.%d %d", &a, &b, &s) == 3 && s >= 100 && s < 600) {
    return s;
  }
  return 0;
}
static int http_post_json(const char* url, const char* body, char* err, size_t errlen)
{
  if (err && errlen) {
    err[0] = 0;
  }
  char host[96];
  char pth[200];
  uint16_t port = 80;
  int is_https = 0;
  if (api_push_parse_url(url, host, sizeof(host), pth, sizeof(pth), &port, &is_https) != 0) {
    if (err && errlen) {
      snprintf(err, errlen, "url");
    }
    return -2;
  }
  if (WiFi.status() == WL_CONNECTED) {
    size_t blen = body ? strlen(body) : 0;
    if (is_https) {
      WiFiClientSecure c;
      c.setInsecure();
      c.setTimeout(20000);
      if (!c.connect(host, port)) {
        if (err && errlen) {
          snprintf(err, errlen, "connect");
        }
        return -2;
      }
      c.print(F("POST "));
      c.print(pth);
      c.print(F(" HTTP/1.1\r\n"));
      c.print(F("Host: "));
      c.print(host);
      c.print(F("\r\nContent-Type: application/json\r\n"));
      if (s_key[0]) {
        c.print(F("X-API-Key: "));
        c.print(s_key);
        c.print(F("\r\n"));
      }
      c.print(F("Content-Length: "));
      c.print((uint32_t)blen);
      c.print(F("\r\nConnection: close\r\n\r\n"));
      if (blen) {
        c.print(body);
      }
      int sc = read_http_status(c);
      c.stop();
      if (sc == 0 && err && errlen) {
        snprintf(err, errlen, "response");
      }
      return sc;
    }
    WiFiClient c;
    c.setTimeout(20000);
    if (!c.connect(host, port)) {
      if (err && errlen) {
        snprintf(err, errlen, "connect");
      }
      return -2;
    }
    c.print(F("POST "));
    c.print(pth);
    c.print(F(" HTTP/1.1\r\n"));
    c.print(F("Host: "));
    c.print(host);
    c.print(F("\r\nContent-Type: application/json\r\n"));
    if (s_key[0]) {
      c.print(F("X-API-Key: "));
      c.print(s_key);
      c.print(F("\r\n"));
    }
    c.print(F("Content-Length: "));
    c.print((uint32_t)blen);
    c.print(F("\r\nConnection: close\r\n\r\n"));
    if (blen) {
      c.print(body);
    }
    int sc = read_http_status(c);
    c.stop();
    if (sc == 0 && err && errlen) {
      snprintf(err, errlen, "response");
    }
    return sc;
  }
  {
    int c2 = api_push_post_cell(url, body, s_key, err, errlen);
    if (c2 != API_PUSH_NO_ALT_NET) {
      return c2;
    }
    if (err && errlen) {
      snprintf(err, errlen, "no_network");
    }
    return -1;
  }
}
static void build_url(const char* base, const char* part, char* out, size_t olen)
{
  out[0] = 0;
  if (!base[0] || olen < 8) {
    return;
  }
  char b[120];
  strncpy(b, base, sizeof(b) - 1);
  b[sizeof(b) - 1] = 0;
  size_t L = strlen(b);
  while (L > 0 && b[L - 1] == '/') {
    b[--L] = 0;
  }
  char pbuf[80];
  if (part[0] == '/') {
    snprintf(pbuf, sizeof(pbuf), "%s", part);
  } else {
    snprintf(pbuf, sizeof(pbuf), "/%s", part);
  }
  snprintf(out, olen, "%s%s", b, pbuf);
}
static int json_esc(const char* s, char* o, size_t olen)
{
  size_t j = 0;
  if (!s) {
    o[0] = 0;
    return 0;
  }
  for (size_t i = 0; s[i] && j + 2 < olen; i++) {
    if (s[i] == '\\' || s[i] == '"') {
      o[j++] = '\\';
    }
    o[j++] = s[i];
  }
  o[j] = 0;
  return (int)j;
}
static void slot_add(uint16_t pid, double v)
{
  for (int i = 0; i < AGG_N; i++) {
    if (s_slot[i].used && s_slot[i].id == pid) {
      s_slot[i].n++;
      s_slot[i].sum += v;
      return;
    }
  }
  for (int i = 0; i < AGG_N; i++) {
    if (!s_slot[i].used) {
      s_slot[i].used = 1;
      s_slot[i].id = pid;
      s_slot[i].n = 1;
      s_slot[i].sum = v;
      return;
    }
  }
}
void api_push_add_sample(uint16_t pid, double v)
{
  if (!s_en) {
    return;
  }
  if (!s_ivlMin || s_base[0] == 0 || s_pathI[0] == 0) {
    return;
  }
  if (s_win0 == 0) {
    s_win0 = millis();
  }
  slot_add(pid, v);
}
static void flush_slots()
{
  for (int i = 0; i < AGG_N; i++) s_slot[i].used = 0, s_slot[i].n = 0, s_slot[i].sum = 0;
  s_win0 = millis();
}
void api_push_reload()
{
  s_base[0] = 0;
  s_key[0] = 0;
  s_pathA[0] = 0;
  s_pathI[0] = 0;
  s_en = 0;
  s_authOk = 0;
  s_ivlMin = 0;
  if (!s_nvs) {
    return;
  }
  if (nvs_get_u8(s_nvs, "R_EN", &s_en) != ESP_OK) s_en = 0;
  if (nvs_get_u8(s_nvs, "R_OK", &s_authOk) != ESP_OK) s_authOk = 0;
  size_t n = sizeof(s_base);
  if (nvs_get_str(s_nvs, "R_BASE", s_base, &n) != ESP_OK) s_base[0] = 0;
  n = sizeof(s_key);
  if (nvs_get_str(s_nvs, "R_KEY", s_key, &n) != ESP_OK) s_key[0] = 0;
  n = sizeof(s_pathA);
  if (nvs_get_str(s_nvs, "R_PA", s_pathA, &n) != ESP_OK) s_pathA[0] = 0;
  n = sizeof(s_pathI);
  if (nvs_get_str(s_nvs, "R_PI", s_pathI, &n) != ESP_OK) s_pathI[0] = 0;
  uint16_t t = 0;
  s_ivlMin = (nvs_get_u16(s_nvs, "R_IV", &t) == ESP_OK) ? t : 0;
  flush_slots();
}
void api_push_init(nvs_handle_t h, const char* deviceId)
{
  s_nvs = h;
  strncpy(s_dev, deviceId ? deviceId : "", sizeof(s_dev) - 1);
  s_dev[sizeof(s_dev) - 1] = 0;
  api_push_reload();
}
void api_push_service()
{
  if (!s_nvs || !s_en) {
    return;
  }
  if (s_ivlMin == 0 || s_base[0] == 0 || s_pathI[0] == 0) {
    return;
  }
  if (!api_push_link_available()) {
    return;
  }
  uint32_t w = s_ivlMin * 60u * 1000u;
  if (w == 0) {
    return;
  }
  if (s_win0 == 0) s_win0 = millis();
  if ((uint32_t)(millis() - s_win0) < w) {
    return;
  }
  char url[220];
  build_url(s_base, s_pathI, url, sizeof(url));
  if (!url[0]) {
    flush_slots();
    return;
  }
  int any = 0;
  for (int i = 0; i < AGG_N; i++) {
    if (s_slot[i].used && s_slot[i].n) {
      any = 1;
      break;
    }
  }
  if (!any) {
    flush_slots();
    return;
  }
  time_t nowt = 0;
  time(&nowt);
  uint32_t tsu = (nowt > 100000) ? (uint32_t)nowt : (uint32_t)(millis() / 1000u);
  char b[2000];
  int o = snprintf(b, sizeof(b), "{\"t\":%u", (unsigned)tsu);
  if (o < 0 || o >= (int)sizeof(b)) {
    flush_slots();
    return;
  }
  if (s_dev[0]) {
    o += snprintf(b + o, sizeof(b) - (size_t)o, ",\"i\":\"");
    o += json_esc(s_dev, b + o, sizeof(b) - (size_t)o);
    o += snprintf(b + o, sizeof(b) - (size_t)o, "\"");
  }
  o += snprintf(b + o, sizeof(b) - (size_t)o, ",\"m\":[");
  bool first = true;
  for (int i = 0; i < AGG_N; i++) {
    if (!s_slot[i].used || s_slot[i].n == 0) {
      continue;
    }
    char vbuf[32];
    double avg = s_slot[i].n ? (s_slot[i].sum / (double)s_slot[i].n) : 0.0;
    snprintf(vbuf, sizeof(vbuf), "%.5g", avg);
    o += snprintf(b + o, sizeof(b) - (size_t)o, "%s{\"p\":%u,\"a\":%s}", first ? "" : ",", s_slot[i].id, vbuf);
    first = false;
    if (o >= (int)sizeof(b) - 64) {
      break;
    }
  }
  o += snprintf(b + o, sizeof(b) - (size_t)o, "]}");
  if (o < 0 || o >= (int)sizeof(b)) {
    flush_slots();
    return;
  }
  char e[64];
  http_post_json(url, b, e, sizeof(e));
  flush_slots();
}
static int json_hkey(const char* j, const char* key)
{
  char p[32];
  snprintf(p, sizeof(p), "\"%s\":", key);
  return strstr(j, p) != 0;
}
static void json_sval(const char* j, const char* key, char* out, size_t olen, int* has)
{
  out[0] = 0;
  *has = 0;
  char p[32];
  snprintf(p, sizeof(p), "\"%s\":", key);
  const char* s = strstr(j, p);
  if (!s) {
    return;
  }
  s += strlen(p);
  *has = 1;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '"') {
    s++;
    size_t n = 0;
    while (*s && n + 1 < olen) {
      if (*s == '\\' && s[1]) {
        s++;
        out[n++] = *s++;
        continue;
      }
      if (*s == '"') {
        break;
      }
      out[n++] = *s++;
    }
    out[n] = 0;
  }
}
static int json_ival(const char* j, const char* key, int* v)
{
  *v = 0;
  char p[32];
  snprintf(p, sizeof(p), "\"%s\":", key);
  const char* s = strstr(j, p);
  if (!s) {
    return 0;
  }
  s += strlen(p);
  while (*s == ' ' || *s == '\t') s++;
  *v = atoi(s);
  return 1;
}
bool api_push_to_json(char* out, size_t outLen)
{
  if (!s_nvs || outLen < 32) {
    return false;
  }
  char base[200], pA[200], pI[200], kbuf[200];
  uint8_t en = 0, ok = 0;
  uint16_t iv = 0;
  size_t L;
  base[0] = 0, pA[0] = 0, pI[0] = 0, kbuf[0] = 0;
  L = sizeof(base);
  if (nvs_get_str(s_nvs, "R_BASE", base, &L) != ESP_OK) base[0] = 0;
  L = sizeof(pA);
  if (nvs_get_str(s_nvs, "R_PA", pA, &L) != ESP_OK) pA[0] = 0;
  L = sizeof(pI);
  if (nvs_get_str(s_nvs, "R_PI", pI, &L) != ESP_OK) pI[0] = 0;
  L = sizeof(kbuf);
  if (nvs_get_str(s_nvs, "R_KEY", kbuf, &L) != ESP_OK) kbuf[0] = 0;
  if (nvs_get_u8(s_nvs, "R_EN", &en) != ESP_OK) en = 0;
  if (nvs_get_u8(s_nvs, "R_OK", &ok) != ESP_OK) ok = 0;
  if (nvs_get_u16(s_nvs, "R_IV", &iv) != ESP_OK) iv = 0;
  int keySet = kbuf[0] ? 1 : 0;
  char eb[220], eA[220], eI[220];
  json_esc(base, eb, sizeof(eb));
  json_esc(pA, eA, sizeof(eA));
  json_esc(pI, eI, sizeof(eI));
  snprintf(out, outLen, "{\"en\":%u,\"authOk\":%u,\"ivl\":%u,\"keySet\":%d,\"base\":\"%s\",\"pathAuth\":\"%s\",\"pathIngest\":\"%s\"}",
           (unsigned)en, (unsigned)ok, (unsigned)iv, keySet, eb, eA, eI);
  return true;
}
bool api_push_from_json(const char* j)
{
  if (!s_nvs || !j) {
    return false;
  }
  int h = 0;
  char tmp[200];
  json_sval(j, "base", tmp, sizeof(tmp), &h);
  if (h) nvs_set_str(s_nvs, "R_BASE", tmp);
  if (json_hkey(j, "key")) {
    int hk = 0;
    json_sval(j, "key", tmp, sizeof(tmp), &hk);
    if (hk) nvs_set_str(s_nvs, "R_KEY", tmp[0] ? tmp : "");
  }
  h = 0;
  json_sval(j, "pathAuth", tmp, sizeof(tmp), &h);
  if (h) nvs_set_str(s_nvs, "R_PA", tmp);
  h = 0;
  json_sval(j, "pathIngest", tmp, sizeof(tmp), &h);
  if (h) nvs_set_str(s_nvs, "R_PI", tmp);
  int v = 0;
  if (json_ival(j, "en", &v)) nvs_set_u8(s_nvs, "R_EN", v ? 1u : 0u);
  if (json_ival(j, "ivl", &v) && v >= 0 && v < 20000) nvs_set_u16(s_nvs, "R_IV", (uint16_t)v);
  nvs_commit(s_nvs);
  api_push_reload();
  return true;
}
int api_push_test_auth(char* errOut, size_t errLen)
{
  if (errOut && errLen) errOut[0] = 0;
  if (!s_nvs) {
    if (errOut && errLen) snprintf(errOut, errLen, "nvs");
    return -1;
  }
  if (s_pathA[0] == 0) {
    if (errOut && errLen) snprintf(errOut, errLen, "path");
    return -1;
  }
  if (s_base[0] == 0) {
    if (errOut && errLen) snprintf(errOut, errLen, "base");
    return -1;
  }
  char url[256];
  build_url(s_base, s_pathA, url, sizeof(url));
  if (!url[0]) {
    if (errOut && errLen) snprintf(errOut, errLen, "url");
    return -1;
  }
  char body[192];
  if (s_dev[0]) {
    snprintf(body, sizeof(body), "{\"i\":\"");
    int p = (int)strlen(body);
    p += json_esc(s_dev, body + p, sizeof(body) - (size_t)p);
    if (p < (int)sizeof(body) - 4) {
      snprintf(body + p, sizeof(body) - (size_t)p, "\"}");
    }
  } else {
    snprintf(body, sizeof(body), "{}");
  }
  int code = http_post_json(url, body, errOut, errLen);
  if (code >= 200 && code < 300) {
    s_authOk = 1;
    nvs_set_u8(s_nvs, "R_OK", 1);
    nvs_commit(s_nvs);
  } else if (code < 0) {
  } else {
    s_authOk = 0;
    nvs_set_u8(s_nvs, "R_OK", 0);
    nvs_commit(s_nvs);
    if (errOut && errLen) snprintf(errOut, errLen, "http %d", code);
  }
  return code;
}
