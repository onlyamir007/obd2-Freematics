#ifndef TELECLIENT_H
#define TELECLIENT_H
#include "config.h"
#include "cbuffer.h"
#include "telestore.h"
#include <FreematicsPlus.h>

#define EVENT_LOGIN 1
#define EVENT_LOGOUT 2
#define EVENT_SYNC 3
#define EVENT_RECONNECT 4
#define EVENT_COMMAND 5
#define EVENT_ACK 6
#define EVENT_PING 7

class TeleClient
{
public:
    virtual void reset()
    {
        txCount = 0;
        txBytes = 0;
        rxBytes = 0;
        login = false;
        startTime = millis();
    }
    virtual bool notify(byte event, const char* payload = 0) { return true; }
    virtual bool connect() { return true; }
    virtual bool transmit(const char* packetBuffer, unsigned int packetSize)  { return true; }
    virtual void inbound() {}
    uint32_t txCount = 0;
    uint32_t txBytes = 0;
    uint32_t rxBytes = 0;
    uint32_t lastSyncTime = 0;
    uint16_t feedid = 0;
    uint32_t startTime = 0;
    uint8_t packets = 0;
    bool login = false;
};

class TeleClientUDP : public TeleClient
{
public:
    bool notify(byte event, const char* payload = 0);
    bool connect(bool quick = false);
    bool transmit(const char* packetBuffer, unsigned int packetSize);
    bool ping();
    void inbound();
    bool verifyChecksum(char* data);
    void shutdown();
#if ENABLE_WIFI
    WifiUDP wifi;
#endif
    CellUDP cell;
};

class TeleClientHTTP : public TeleClient
{
public:
    bool notify(byte event, const char* payload = 0);
    bool connect(bool quick = false);
    bool transmit(const char* packetBuffer, unsigned int packetSize);
    bool ping();
    void shutdown();
#if ENABLE_WIFI
    WifiHTTP wifi;
#endif
    CellHTTP cell;
};

#endif