#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "api_push.h"
#include "teleclient.h"
#include "FreematicsNetwork.h"

#if SERVER_PROTOCOL == PROTOCOL_UDP
/* Freematics Hub in UDP mode uses only CellUDP: no HTTP stack on cellular for this firmware. */
#else
extern TeleClientHTTP teleClient;

static bool cell_gprs_data_ok()
{
  String ip = teleClient.cell.getIP();
  return (ip.length() > 0 && ip != "0.0.0.0");
}

extern "C" int api_push_link_available(void)
{
  if (WiFi.status() == WL_CONNECTED) {
    return 1;
  }
  return cell_gprs_data_ok() ? 1 : 0;
}

extern "C" int api_push_post_cell(const char* url, const char* body, const char* apiKey, char* err, size_t errlen)
{
  if (err && errlen) {
    err[0] = 0;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return API_PUSH_NO_ALT_NET;
  }
  if (!cell_gprs_data_ok()) {
    if (err && errlen) {
      snprintf(err, errlen, "no_cell");
    }
    return -1;
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
  char hbuf[192];
  if (apiKey && apiKey[0]) {
    snprintf(hbuf, sizeof(hbuf), "Content-Type: application/json\r\nX-API-Key: %s", apiKey);
  } else {
    snprintf(hbuf, sizeof(hbuf), "Content-Type: application/json");
  }

  teleClient.cell.close();
  teleClient.cell.setExtraHeaders(hbuf);
  if (!teleClient.cell.open(host, port)) {
    teleClient.cell.clearExtraHeaders();
    if (err && errlen) {
      snprintf(err, errlen, "open");
    }
    (void)teleClient.connect(true);
    return -2;
  }
  int blen = (int)strlen(body ? body : "");
  if (!teleClient.cell.send(METHOD_POST, host, port, pth, body, blen)) {
    teleClient.cell.clearExtraHeaders();
    teleClient.cell.close();
    if (err && errlen) {
      snprintf(err, errlen, "send");
    }
    (void)teleClient.connect(true);
    return -2;
  }
  int rbytes = 0;
  (void)teleClient.cell.receive(&rbytes, HTTP_CONN_TIMEOUT);
  int sc = (int)teleClient.cell.code();
  teleClient.cell.close();
  teleClient.cell.clearExtraHeaders();
  (void)teleClient.connect(true);
  if (sc == 0) {
    if (err && errlen) {
      snprintf(err, errlen, "response");
    }
  }
  return sc;
}
#endif
