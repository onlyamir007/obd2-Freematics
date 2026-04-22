#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <httpd.h>

int handlerPortalHtml(UrlHandlerParam* param);
int handlerPortalJs(UrlHandlerParam* param);
int handlerPortalLogin(UrlHandlerParam* param);
int handlerPortalScan(UrlHandlerParam* param);
int handlerPortalConnect(UrlHandlerParam* param);
int handlerPortalStatus(UrlHandlerParam* param);
int handlerPortalObdPids(UrlHandlerParam* param);
int handlerPortalRemote(UrlHandlerParam* param);
int handlerPortalRemoteTest(UrlHandlerParam* param);
int handlerPortalBt(UrlHandlerParam* param);
int handlerPortalCfg(UrlHandlerParam* param);
int handlerFavicon(UrlHandlerParam* param);

/* Set from portal when NVS saved; telemetry task consumes and applies STA (never WiFi ops in HTTP handler). */
bool wifi_portal_consume_sta_reconnect_pending(void);

#endif
