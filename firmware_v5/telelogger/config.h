#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#ifdef CONFIG_ENABLE_OBD
#define ENABLE_OBD CONFIG_ENABLE_OBD
#endif
#ifdef CONFIG_ENABLE_MEMS
#define ENABLE_MEMS CONFIG_ENABLE_MEMS
#endif
#ifdef CONFIG_GNSS
#define GNSS CONFIG_GNSS
#endif
#ifdef CONFIG_STORAGE
#define STORAGE CONFIG_STORAGE
#endif
#ifdef CONFIG_BOARD_HAS_PSRAM
#define BOARD_HAS_PSRAM 1
#endif
#ifdef CONFIG_ENABLE_WIFI
#define ENABLE_WIFI CONFIG_ENABLE_WIFI
#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
#endif
#ifdef CONFIG_ENABLE_BLE
#define ENABLE_BLE CONFIG_ENABLE_BLE
#endif
#ifdef CONFIG_ENABLE_HTTPD
#define ENABLE_HTTPD CONFIG_ENABLE_HTTPD
#endif
#ifdef CONFIG_SERVER_HOST
#define SERVER_HOST CONFIG_SERVER_HOST
#define SERVER_PORT CONFIG_SERVER_PORT
#define SERVER_PROTOCOL CONFIG_SERVER_PROTOCOL
#endif
#ifdef CONFIG_CELL_APN
#define CELL_APN CONFIG_CELL_APN
#endif

/**************************************
* Circular Buffer Configuration
**************************************/
#if BOARD_HAS_PSRAM
#define BUFFER_SLOTS 1024 /* max number of buffer slots */
#define BUFFER_LENGTH 384 /* bytes per slot */
#define SERIALIZE_BUFFER_SIZE 4096 /* bytes */
#else
#define BUFFER_SLOTS 32 /* max number of buffer slots */
#define BUFFER_LENGTH 256 /* bytes per slot */
#define SERIALIZE_BUFFER_SIZE 1024 /* bytes */
#endif

/**************************************
* Configuration Definitions
**************************************/
#define STORAGE_NONE 0
#define STORAGE_SPIFFS 1
#define STORAGE_SD 2

#define GNSS_NONE 0
#define GNSS_STANDALONE 1
#define GNSS_CELLULAR 2

#define PROTOCOL_UDP 1
#define PROTOCOL_HTTPS_GET 2
#define PROTOCOL_HTTPS_POST 3

/**************************************
* OBD-II configurations
**************************************/
#ifndef ENABLE_OBD
#define ENABLE_OBD 1
#endif

// maximum consecutive OBD access errors before entering standby
#define MAX_OBD_ERRORS 3

// OBD: retries per PID (helps flaky bus / engine-off ECUs). 0 = single attempt only.
#ifndef OBD_PID_RETRIES
#define OBD_PID_RETRIES 2
#endif
// Print compact tier-1 line every N ms (0 = disable): kph rpm thr load
#ifndef OBD_SERIAL_SUMMARY_MS
#define OBD_SERIAL_SUMMARY_MS 2000
#endif
// Tier-2 PIDs are round-robin; read this many distinct PIDs per processOBD() (>=1).
#ifndef OBD_TIER2_PER_CYCLE
#define OBD_TIER2_PER_CYCLE 6
#endif
/* Extra Mode $01 PIDs from portal (NVS); polled per processOBD round-robin. */
#ifndef OBD_USER_PIDS_PER_CYCLE
#define OBD_USER_PIDS_PER_CYCLE 2
#endif

/**************************************
* Networking configurations
**************************************/
#ifndef ENABLE_WIFI
#define ENABLE_WIFI 1
// WiFi settings
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

/* SoftAP + station (required for WiFi portal scan while AP is up) */
#ifndef ENABLE_WIFI_AP
#define ENABLE_WIFI_AP 1
#endif
#ifndef ENABLE_WIFI_STATION
#define ENABLE_WIFI_STATION 1
#endif
#ifndef NET_WIFI
#define NET_WIFI 1
#endif
#ifndef NET_DEVICE
#define NET_DEVICE NET_WIFI
#endif

/* Browser WiFi setup UI on AP (http://192.168.4.1/portal). Disable with 0. */
#ifndef ENABLE_WIFI_PORTAL
#define ENABLE_WIFI_PORTAL 1
#endif
#define PORTAL_HTTP_USER "admin"
#define PORTAL_HTTP_PASS "amir" 

#ifndef SERVER_HOST
// cellular network settings
#define CELL_APN ""
// Freematics Hub server settings
#define SERVER_HOST "hub.freematics.com"
#define SERVER_PROTOCOL PROTOCOL_UDP
#endif

// SIM card setting
#define SIM_CARD_PIN ""
#define APN_USERNAME NULL
#define APN_PASSWORD NULL

// HTTPS settings
#define SERVER_PATH "/hub/api"

#if !SERVER_PORT
#undef SERVER_PORT
#if SERVER_PROTOCOL == PROTOCOL_UDP
#define SERVER_PORT 8081
#else
#define SERVER_PORT 443
#endif
#endif

// WiFi Mesh settings
#define WIFI_MESH_ID "123456"
#define WIFI_MESH_CHANNEL 13

// WiFi AP settings
#define WIFI_AP_SSID "TELELOGGER"
#define WIFI_AP_PASSWORD "PASSWORD"

// maximum consecutive communication errors before resetting network
#define MAX_CONN_ERRORS_RECONNECT 5
// maximum allowed connecting time
#define MAX_CONN_TIME 10000 /* ms */
// data receiving timeout
#define DATA_RECEIVING_TIMEOUT 5000 /* ms */
// expected maximum server sync signal interval
#define SERVER_SYNC_INTERVAL 120 /* seconds, 0 to disable */
// data interval settings
#define STATIONARY_TIME_TABLE {10, 60, 180} /* seconds */
#define DATA_INTERVAL_TABLE {1000, 2000, 5000} /* ms */
#define PING_BACK_INTERVAL 900 /* seconds */
#define SIGNAL_CHECK_INTERVAL 10 /* seconds */

/**************************************
* Data storage configurations
**************************************/
#ifndef STORAGE
// change the following line to change storage type
#define STORAGE STORAGE_SD
#endif

/**************************************
* MEMS sensors
**************************************/
#ifndef ENABLE_MEMS
#define ENABLE_MEMS 1
#endif

/**************************************
* GPS
**************************************/
#ifndef GNSS
// change the following line to change GNSS setting
#define GNSS GNSS_STANDALONE
#endif
// keeping GNSS power on during standby 
#define GNSS_ALWAYS_ON 0
// GNSS reset timeout while no signal
#define GNSS_RESET_TIMEOUT 300 /* seconds */

/**************************************
* Standby/wakeup
**************************************/
// motion threshold for waking up
#define MOTION_THRESHOLD 0.4f /* vehicle motion threshold in G */
// engine jumpstart voltage for waking up (when MEMS unavailable) 
#define JUMPSTART_VOLTAGE 14 /* V */
// reset device after waking up
#define RESET_AFTER_WAKEUP 1

/**************************************
* Additional features
**************************************/
#define PIN_SENSOR1 34
#define PIN_SENSOR2 26

#define COOLING_DOWN_TEMP 75 /* celsius degrees */

// enable(1)/disable(0) http server (SoftAP dashboard + WiFi portal)
#ifndef ENABLE_HTTPD
#define ENABLE_HTTPD 1
#endif

// enable(1)/disable(0) BLE SPP server (for Freematics Controller App).
#ifndef ENABLE_BLE
#define ENABLE_BLE 1
#endif

/* BLE + SoftAP/HTTP on ESP32 abort() in ble_init after a few seconds; keep BLE off when the portal is on. */
#if ENABLE_HTTPD && ENABLE_BLE
#undef ENABLE_BLE
#define ENABLE_BLE 0
#endif


#endif // CONFIG_H_INCLUDED
