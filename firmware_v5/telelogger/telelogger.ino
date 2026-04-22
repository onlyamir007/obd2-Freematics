/******************************************************************************
* Arduino sketch of a vehicle data data logger and telemeter for Freematics Hub
* Works with Freematics ONE+ Model A and Model B
* Developed by Stanley Huang <stanley@freematics.com.au>
* Distributed under BSD license
* Visit https://freematics.com/products for hardware information
* Visit https://hub.freematics.com to view live and history telemetry data
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
******************************************************************************/

#include <FreematicsPlus.h>
#include <httpd.h>
#include "config.h"
#include "obd_pids_full.h"
#include "obd_user_pids.h"
#include "serial_log.h"
#if ENABLE_WIFI
#include <WiFi.h>
#endif
#if ENABLE_HTTPD && ENABLE_WIFI && ENABLE_WIFI_PORTAL
#include "wifi_portal.h"
#include "api_push.h"
#endif
#include "telestore.h"
#include "teleclient.h"
#if BOARD_HAS_PSRAM
#include "esp32/himem.h"
#endif
#include "driver/adc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "netcfg.h"
#if ENABLE_OLED
#include "FreematicsOLED.h"
#endif

// states
#define STATE_STORAGE_READY 0x1
#define STATE_OBD_READY 0x2
#define STATE_GPS_READY 0x4
#define STATE_MEMS_READY 0x8
#define STATE_NET_READY 0x10
#define STATE_GPS_ONLINE 0x20
#define STATE_CELL_CONNECTED 0x40
#define STATE_WIFI_CONNECTED 0x80
#define STATE_WORKING 0x100
#define STATE_STANDBY 0x200

typedef struct {
  byte pid;
  byte tier;
  int value;
  uint32_t ts;
} PID_POLLING_INFO;

/*
 * Live Mode $01 PIDs — 2009 Chevy Avalanche LTZ (US OBD-II) + full standard SAE set.
 * Tier 1 = every processOBD(). Tier 2 = round-robin (OBD_TIER2_PER_CYCLE per call).
 * isValidPID() skips unsupported PIDs; GM Mode $22 / enhanced DIDs need different code.
 */
PID_POLLING_INFO obdData[]= {
  /* tier 1 — keep PID_SPEED at index 0 (motion + lastMotionTime) */
  {PID_SPEED, 1},
  {PID_RPM, 1},
  {PID_THROTTLE, 1},
  {PID_ENGINE_LOAD, 1},
  /* tier 2 — prioritize temps, then prior “full” Freematics list */
  {PID_COOLANT_TEMP, 2},
  {PID_INTAKE_TEMP, 2},
  {PID_SHORT_TERM_FUEL_TRIM_1, 2},
  {PID_LONG_TERM_FUEL_TRIM_1, 2},
  {PID_SHORT_TERM_FUEL_TRIM_2, 2},
  {PID_LONG_TERM_FUEL_TRIM_2, 2},
  {PID_FUEL_PRESSURE, 2},
  {PID_INTAKE_MAP, 2},
  {PID_TIMING_ADVANCE, 2},
  {PID_MAF_FLOW, 2},
  {PID_AUX_INPUT, 2},
  {PID_RUNTIME, 2},
  {PID_DISTANCE_WITH_MIL, 2},
  {PID_COMMANDED_EGR, 2},
  {PID_EGR_ERROR, 2},
  {PID_COMMANDED_EVAPORATIVE_PURGE, 2},
  {PID_FUEL_LEVEL, 2},
  {PID_WARMS_UPS, 2},
  {PID_DISTANCE, 2},
  {PID_EVAP_SYS_VAPOR_PRESSURE, 2},
  {PID_BAROMETRIC, 2},
  {PID_CATALYST_TEMP_B1S1, 2},
  {PID_CATALYST_TEMP_B2S1, 2},
  {PID_CATALYST_TEMP_B1S2, 2},
  {PID_CATALYST_TEMP_B2S2, 2},
  {PID_CONTROL_MODULE_VOLTAGE, 2},
  {PID_ABSOLUTE_ENGINE_LOAD, 2},
  {PID_AIR_FUEL_EQUIV_RATIO, 2},
  {PID_RELATIVE_THROTTLE_POS, 2},
  {PID_AMBIENT_TEMP, 2},
  {PID_ABSOLUTE_THROTTLE_POS_B, 2},
  {PID_ABSOLUTE_THROTTLE_POS_C, 2},
  {PID_ACC_PEDAL_POS_D, 2},
  {PID_ACC_PEDAL_POS_E, 2},
  {PID_ACC_PEDAL_POS_F, 2},
  {PID_COMMANDED_THROTTLE_ACTUATOR, 2},
  {PID_TIME_WITH_MIL, 2},
  {PID_TIME_SINCE_CODES_CLEARED, 2},
  {PID_ETHANOL_FUEL, 2},
  {PID_FUEL_RAIL_PRESSURE, 2},
  {PID_HYBRID_BATTERY_PERCENTAGE, 2},
  {PID_ENGINE_OIL_TEMP, 2},
  {PID_FUEL_INJECTION_TIMING, 2},
  {PID_ENGINE_FUEL_RATE, 2},
  {PID_ENGINE_TORQUE_DEMANDED, 2},
  {PID_ENGINE_TORQUE_PERCENTAGE, 2},
  {PID_ENGINE_REF_TORQUE, 2},
  {PID_ODOMETER, 2},
  /* extra Mode $01: monitor / O2 / capability bitmaps (ECU may NACK) */
  {PID_SUPPORTED_PIDS_01_20, 2},
  {PID_MONITOR_STATUS, 2},
  {PID_FREEZE_DTC, 2},
  {PID_FUEL_SYSTEM_STATUS, 2},
  {PID_CMD_SECONDARY_AIR, 2},
  {PID_O2_SENSORS_PRESENT_13, 2},
  {PID_O2_B1S1, 2},
  {PID_O2_B1S2, 2},
  {PID_O2_B1S3, 2},
  {PID_O2_B1S4, 2},
  {PID_O2_B2S1, 2},
  {PID_O2_B2S2, 2},
  {PID_O2_B2S3, 2},
  {PID_O2_B2S4, 2},
  {PID_OBD_STANDARDS, 2},
  {PID_O2_SENSORS_PRESENT_1D, 2},
  {PID_SUPPORTED_PIDS_21_40, 2},
  {PID_SUPPORTED_PIDS_41_60, 2},
  {PID_SUPPORTED_PIDS_61_80, 2},
  {PID_SUPPORTED_PIDS_81_A0, 2},
  {PID_SUPPORTED_PIDS_A1_C0, 2},
  {PID_ENGINE_OIL_LIFE_PCT, 2},
};

CBufferManager bufman;
Task subtask;

#if ENABLE_HTTPD
/* GNSS/OBD init in setup() races WiFi/HTTP; first process() pumps HTTP then inits hardware. */
static bool portalHwSetupPending = false;
#endif

#if ENABLE_MEMS
float accBias[3] = {0}; // calibrated reference accelerometer data
float accSum[3] = {0};
float acc[3] = {0};
float gyr[3] = {0};
float mag[3] = {0};
uint8_t accCount = 0;
#endif
int deviceTemp = 0;

// config data
char apn[32];
#if ENABLE_WIFI
char wifiSSID[32] = WIFI_SSID;
char wifiPassword[32] = WIFI_PASSWORD;
#endif
nvs_handle_t nvs;
uint8_t g_rt_wifi_ap = 1;
uint8_t g_rt_wifi_sta = 1;
uint8_t g_rt_ble = 1;
uint8_t g_rt_cell = 0;

void netcfg_reload_from_nvs(void)
{
  if (!nvs) {
    g_rt_wifi_ap = g_rt_wifi_sta = g_rt_ble = 1u;
    g_rt_cell = 0u;
    return;
  }
  uint8_t v;
  g_rt_wifi_ap = 1u;
  g_rt_wifi_sta = 1u;
  g_rt_ble = 1u;
  g_rt_cell = 0u;
  if (nvs_get_u8(nvs, "C_AP", &v) == ESP_OK) {
    g_rt_wifi_ap = v ? 1u : 0u;
  }
  if (nvs_get_u8(nvs, "C_STA", &v) == ESP_OK) {
    g_rt_wifi_sta = v ? 1u : 0u;
  }
  if (nvs_get_u8(nvs, "C_BLE", &v) == ESP_OK) {
    g_rt_ble = v ? 1u : 0u;
  }
  if (nvs_get_u8(nvs, "C_CELL", &v) == ESP_OK) {
    g_rt_cell = v ? 1u : 0u;
  }
  if (g_rt_wifi_ap == 0u && g_rt_wifi_sta == 0u) {
    g_rt_wifi_ap = 1u;
  }
}

// live data
String netop;
String ip;
int16_t rssi = 0;
int16_t rssiLast = 0;
char vin[18] = {0};
uint16_t dtc[6] = {0};
uint8_t g_dtc_count = 0; /* Mode 03 count; refreshed periodically for /api/live */
float batteryVoltage = 0;
GPS_DATA* gd = 0;

char devid[12] = {0};
char isoTime[32] = {0};

// stats data
uint32_t lastMotionTime = 0;
uint32_t timeoutsOBD = 0;
uint32_t timeoutsNet = 0;
uint32_t lastStatsTime = 0;

int32_t syncInterval = SERVER_SYNC_INTERVAL * 1000;
int32_t dataInterval = 1000;

#if STORAGE != STORAGE_NONE
int fileid = 0;
uint16_t lastSizeKB = 0;
#endif

byte ledMode = 0;

bool serverSetup(IPAddress& ip);
void serverProcess(int timeout);
void processMEMS(CBuffer* buffer);
bool processGPS(CBuffer* buffer);
void processBLE(int timeout);

class State {
public:
  bool check(uint16_t flags) { return (m_state & flags) == flags; }
  void set(uint16_t flags) { m_state |= flags; }
  void clear(uint16_t flags) { m_state &= ~flags; }
  uint16_t m_state = 0;
};

FreematicsESP32 sys;

class OBD : public COBD
{
protected:
  void idleTasks()
  {
    // do some quick tasks while waiting for OBD response
#if ENABLE_MEMS
    processMEMS(0);
#endif
    processBLE(0);
  }
};

OBD obd;

MEMS_I2C* mems = 0;

#if STORAGE == STORAGE_SPIFFS
SPIFFSLogger logger;
#elif STORAGE == STORAGE_SD
SDLogger logger;
#endif

#if SERVER_PROTOCOL == PROTOCOL_UDP
TeleClientUDP teleClient;
#else
TeleClientHTTP teleClient;
#endif

#if ENABLE_WIFI
/* Sent to the router as the DHCP client name (look for this on the LAN client list). */
static void wifiApplyStaHostname(void)
{
  if (!devid[0]) {
    return;
  }
  char hn[32];
  snprintf(hn, sizeof(hn), "fm-%s", devid);
  for (char* p = hn; *p; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      *p = (char)(c - 'A' + 'a');
    } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      *p = '-';
    }
  }
  WiFi.setHostname(hn);
}
#endif

#if ENABLE_OLED
OLED_SH1106 oled;
#endif

State state;

void printTimeoutStats()
{
  Serial.print("Timeouts: OBD:");
  Serial.print(timeoutsOBD);
  Serial.print(" Network:");
  Serial.println(timeoutsNet);
}

void beep(int duration)
{
    // turn on buzzer at 2000Hz frequency 
    sys.buzzer(2000);
    delay(duration);
    // turn off buzzer
    sys.buzzer(0);
}

#if LOG_EXT_SENSORS
void processExtInputs(CBuffer* buffer)
{
#if LOG_EXT_SENSORS == 1
  uint8_t levels[2] = {(uint8_t)digitalRead(PIN_SENSOR1), (uint8_t)digitalRead(PIN_SENSOR2)};
  buffer->add(PID_EXT_SENSORS, ELEMENT_UINT8, levels, sizeof(levels), 2);
#elif LOG_EXT_SENSORS == 2
  uint16_t reading[] = {adc1_get_raw(ADC1_CHANNEL_0), adc1_get_raw(ADC1_CHANNEL_1)};
  Serial.print("GPIO0:");
  Serial.print((float)reading[0] * 3.15 / 4095 - 0.01);
  Serial.print(" GPIO1:");
  Serial.println((float)reading[1] * 3.15 / 4095 - 0.01);
  buffer->add(PID_EXT_SENSORS, ELEMENT_UINT16, reading, sizeof(reading), 2);
#endif
}
#endif

/*******************************************************************************
  HTTP API
*******************************************************************************/
#if ENABLE_HTTPD
int handlerLiveData(UrlHandlerParam* param)
{
    char *buf = param->pucBuffer;
    int bufsize = param->bufSize;
    int n = snprintf(buf, bufsize, "{\"obd\":{\"vin\":\"%s\",\"battery\":%.1f,\"pid\":[", vin, batteryVoltage);
    uint32_t t = millis();
    for (int i = 0; i < sizeof(obdData) / sizeof(obdData[0]); i++) {
        unsigned int age;
        if (obdData[i].ts == 0) {
            age = 999999u; /* never read — portal can hide row */
        } else {
            age = (unsigned int)(t - obdData[i].ts);
        }
        n += snprintf(buf + n, bufsize - n, "{\"pid\":%u,\"value\":%d,\"age\":%u},",
            0x100 | obdData[i].pid, obdData[i].value, age);
    }
    n--;
    n += snprintf(buf + n, bufsize - n, "]");
    {
      int u = obdUserPidsAppendLiveJson(buf + n, bufsize - n, t);
      if (u > 0) {
        n += u;
      }
    }
#if ENABLE_OBD
    if (n + 160 < bufsize) {
      int k = snprintf(buf + n, bufsize - n, ",\"dtc_count\":%u,\"dtc_hex\":[", (unsigned)g_dtc_count);
      if (k > 0 && k < bufsize - n) {
        n += k;
        for (unsigned i = 0; i < g_dtc_count && i < 6; i++) {
          if (i) {
            n += snprintf(buf + n, bufsize - n, ",");
          }
          n += snprintf(buf + n, bufsize - n, "\"%04X\"", (unsigned)dtc[i]);
        }
        n += snprintf(buf + n, bufsize - n, "]");
      }
    }
#else
    if (n + 32 < bufsize) {
      n += snprintf(buf + n, bufsize - n, ",\"dtc_count\":0,\"dtc_hex\":[]");
    }
#endif
    n += snprintf(buf + n, bufsize - n, "}");
#if ENABLE_MEMS
    if (accCount) {
      n += snprintf(buf + n, bufsize - n, ",\"mems\":{\"acc\":[%d,%d,%d],\"stationary\":%u}",
          (int)((accSum[0] / accCount - accBias[0]) * 100), (int)((accSum[1] / accCount - accBias[1]) * 100), (int)((accSum[2] / accCount - accBias[2]) * 100),
          (unsigned int)(millis() - lastMotionTime));
    }
#endif
    if (gd && gd->ts) {
      n += snprintf(buf + n, bufsize - n, ",\"gps\":{\"utc\":\"%s\",\"lat\":%f,\"lng\":%f,\"alt\":%f,\"speed\":%f,\"sat\":%d,\"age\":%u}",
          isoTime, gd->lat, gd->lng, gd->alt, gd->speed, (int)gd->sat, (unsigned int)(millis() - gd->ts));
    }
    buf[n++] = '}';
    param->contentLength = n;
    param->contentType=HTTPFILETYPE_JSON;
    return FLAG_DATA_RAW;
}
#endif

/*******************************************************************************
  Reading and processing OBD data
*******************************************************************************/
#if ENABLE_OBD
static bool readPidRetry(byte pid, int& value)
{
  byte tries = (byte)(1 + OBD_PID_RETRIES);
  for (byte n = 0; n < tries; n++) {
    if (obd.readPID(pid, value))
      return true;
    if (n + 1 < tries)
      delay(20);
  }
  return false;
}

void processOBD(CBuffer* buffer)
{
  byte tier1Count = 0;
  for (byte i = 0; i < sizeof(obdData) / sizeof(obdData[0]); i++) {
    if (obdData[i].tier != 1) break;
    tier1Count++;
  }

  /* Tier 1: read every cycle; continue on failure (old code broke on first NACK). */
  byte tier1Ok = 0;
  for (byte i = 0; i < tier1Count; i++) {
    byte pid = obdData[i].pid;
    if (!obd.isValidPID(pid)) continue;
    int value;
    if (readPidRetry(pid, value)) {
      obdData[i].ts = millis();
      obdData[i].value = value;
      buffer->add((uint16_t)pid | 0x100, ELEMENT_INT32, &value, sizeof(value));
      tier1Ok++;
    }
  }
  /* One timeout tick only if every tier-1 PID failed (reduces Serial spam). */
  if (tier1Count > 0 && tier1Ok == 0) {
    timeoutsOBD++;
    if (timeoutsOBD <= 3 || (timeoutsOBD % 10) == 0)
      printTimeoutStats();
  }

  /* Tier 2: round-robin; read OBD_TIER2_PER_CYCLE PIDs per call (failures do not bump global OBD timeouts). */
  static uint16_t rr = 0;
  int n2 = (int)(sizeof(obdData) / sizeof(obdData[0])) - tier1Count;
  if (n2 > 0) {
    unsigned batch = (unsigned)OBD_TIER2_PER_CYCLE;
    if (batch < 1) {
      batch = 1;
    }
    if (batch > (unsigned)n2) {
      batch = (unsigned)n2;
    }
    for (unsigned k = 0; k < batch; k++) {
      byte j = (byte)(tier1Count + (rr % n2));
      rr++;
      byte pid = obdData[j].pid;
      if (obd.isValidPID(pid)) {
        int value;
        if (readPidRetry(pid, value)) {
          obdData[j].ts = millis();
          obdData[j].value = value;
          buffer->add((uint16_t)pid | 0x100, ELEMENT_INT32, &value, sizeof(value));
        }
      }
    }
  }

  /* User-configured Mode $01 PIDs (NVS / portal), round-robin */
  static uint16_t rrUser = 0;
  {
    uint8_t nu = obdUserPidsCount();
    if (nu > 0) {
      unsigned bu = (unsigned)OBD_USER_PIDS_PER_CYCLE;
      if (bu < 1) {
        bu = 1;
      }
      if (bu > nu) {
        bu = nu;
      }
      for (unsigned k = 0; k < bu; k++) {
        uint8_t idx = (uint8_t)(rrUser % nu);
        rrUser++;
        byte pid = (byte)obdUserPidsByte(idx);
        if (obd.isValidPID(pid)) {
          int value;
          if (readPidRetry(pid, value)) {
            obdUserPidsMarkRead(idx, value);
            buffer->add((uint16_t)pid | 0x100, ELEMENT_INT32, &value, sizeof(value));
          }
        }
      }
    }
  }

#if OBD_SERIAL_SUMMARY_MS
  static uint32_t lastObdSummary = 0;
  if (millis() - lastObdSummary >= (uint32_t)OBD_SERIAL_SUMMARY_MS) {
    lastObdSummary = millis();
    if (tier1Count >= 4) {
      Serial.print("[OBD] kph=");
      Serial.print(obdData[0].value);
      Serial.print(" rpm=");
      Serial.print(obdData[1].value);
      Serial.print(" thr=");
      Serial.print(obdData[2].value);
      Serial.print(" load=");
      Serial.println(obdData[3].value);
    }
  }
#endif

  int kph = obdData[0].value;
  if (kph >= 2) lastMotionTime = millis();
}
#endif

bool initGPS()
{
  // start GNSS receiver
  if (sys.gpsBeginExt()) {
    Serial.println("GNSS:OK(E)");
  } else if (sys.gpsBegin()) {
    Serial.println("GNSS:OK(I)");
  } else {
    Serial.println("GNSS:NO");
    return false;
  }
  return true;
}

bool processGPS(CBuffer* buffer)
{
  static uint32_t lastGPStime = 0;
  static float lastGPSLat = 0;
  static float lastGPSLng = 0;

  if (!gd) {
    lastGPStime = 0;
    lastGPSLat = 0;
    lastGPSLng = 0;
  }
#if GNSS == GNSS_STANDALONE
  if (state.check(STATE_GPS_READY)) {
    // read parsed GPS data
    if (!sys.gpsGetData(&gd)) {
      return false;
    }
  }
#else
    if (!teleClient.cell.getLocation(&gd)) {
      return false;
    }
#endif
  if (!gd || lastGPStime == gd->time) return false;
  if (gd->date) {
    // generate ISO time string
    char *p = isoTime + sprintf(isoTime, "%04u-%02u-%02uT%02u:%02u:%02u",
        (unsigned int)(gd->date % 100) + 2000, (unsigned int)(gd->date / 100) % 100, (unsigned int)(gd->date / 10000),
        (unsigned int)(gd->time / 1000000), (unsigned int)(gd->time % 1000000) / 10000, (unsigned int)(gd->time % 10000) / 100);
    unsigned char tenth = (gd->time % 100) / 10;
    if (tenth) p += sprintf(p, ".%c00", '0' + tenth);
    *p = 'Z';
    *(p + 1) = 0;
  }
  if (gd->lng == 0 && gd->lat == 0) {
    // coordinates not ready
    if (gd->date) {
      serialLogPrintf("[GNSS] %s\n", isoTime);
    }
    return false;
  }
  if ((lastGPSLat || lastGPSLng) && (abs(gd->lat - lastGPSLat) > 0.001 || abs(gd->lng - lastGPSLng) > 0.001)) {
    // invalid coordinates data
    lastGPSLat = 0;
    lastGPSLng = 0;
    return false;
  }
  lastGPSLat = gd->lat;
  lastGPSLng = gd->lng;

  float kph = gd->speed * 1.852f;
  if (kph >= 2) lastMotionTime = millis();

  if (buffer) {
    buffer->add(PID_GPS_TIME, ELEMENT_UINT32, &gd->time, sizeof(uint32_t));
    buffer->add(PID_GPS_LATITUDE, ELEMENT_FLOAT, &gd->lat, sizeof(float));
    buffer->add(PID_GPS_LONGITUDE, ELEMENT_FLOAT, &gd->lng, sizeof(float));
    buffer->add(PID_GPS_ALTITUDE, ELEMENT_FLOAT_D1, &gd->alt, sizeof(float)); /* m */
    buffer->add(PID_GPS_SPEED, ELEMENT_FLOAT_D1, &kph, sizeof(kph));
    buffer->add(PID_GPS_HEADING, ELEMENT_UINT16, &gd->heading, sizeof(uint16_t));
    if (gd->sat) buffer->add(PID_GPS_SAT_COUNT, ELEMENT_UINT8, &gd->sat, sizeof(uint8_t));
    if (gd->hdop) buffer->add(PID_GPS_HDOP, ELEMENT_UINT8, &gd->hdop, sizeof(uint8_t));
  }
  
  serialLogPrintf("[GNSS] %.6f %.6f %dkm/h SATS:%u HDOP:%u Course:%u\n",
      (double)gd->lat, (double)gd->lng, (int)kph,
      (unsigned)gd->sat, (unsigned)gd->hdop, (unsigned)gd->heading);
  //Serial.println(gd->errors);
  lastGPStime = gd->time;
  return true;
}

bool waitMotionGPS(int timeout)
{
  unsigned long t = millis();
  lastMotionTime = 0;
  do {
      serverProcess(100);
    if (!processGPS(0)) continue;
    if (lastMotionTime) return true;
  } while (millis() - t < timeout);
  return false;
}

#if ENABLE_MEMS
void processMEMS(CBuffer* buffer)
{
  if (!state.check(STATE_MEMS_READY)) return;

  // load and store accelerometer data
  float temp;
#if ENABLE_ORIENTATION
  ORIENTATION ori;
  if (!mems->read(acc, gyr, mag, &temp, &ori)) return;
#else
  if (!mems->read(acc, gyr, mag, &temp)) return;
#endif
  deviceTemp = (int)temp;

  accSum[0] += acc[0];
  accSum[1] += acc[1];
  accSum[2] += acc[2];
  accCount++;

  if (buffer) {
    if (accCount) {
      float value[3];
      value[0] = accSum[0] / accCount - accBias[0];
      value[1] = accSum[1] / accCount - accBias[1];
      value[2] = accSum[2] / accCount - accBias[2];
      buffer->add(PID_ACC, ELEMENT_FLOAT_D2, value, sizeof(value), 3);
/*
      Serial.print("[ACC] ");
      Serial.print(value[0]);
      Serial.print('/');
      Serial.print(value[1]);
      Serial.print('/');
      Serial.println(value[2]);
*/
#if ENABLE_ORIENTATION
      value[0] = ori.yaw;
      value[1] = ori.pitch;
      value[2] = ori.roll;
      buffer->add(PID_ORIENTATION, ELEMENT_FLOAT_D2, value, sizeof(value), 3);
#endif
#if 0
      // calculate motion
      float motion = 0;
      for (byte i = 0; i < 3; i++) {
        motion += value[i] * value[i];
      }
      if (motion >= MOTION_THRESHOLD * MOTION_THRESHOLD) {
        lastMotionTime = millis();
        Serial.print("Motion:");
        Serial.println(motion);
      }
#endif
    }
    accSum[0] = 0;
    accSum[1] = 0;
    accSum[2] = 0;
    accCount = 0;
  }
}

void calibrateMEMS()
{
  if (state.check(STATE_MEMS_READY)) {
    accBias[0] = 0;
    accBias[1] = 0;
    accBias[2] = 0;
    int n;
    unsigned long t = millis();
    for (n = 0; millis() - t < 1000; n++) {
      float acc[3];
      if (!mems->read(acc)) continue;
      accBias[0] += acc[0];
      accBias[1] += acc[1];
      accBias[2] += acc[2];
      delay(10);
    }
    if (n <= 0) {
      return;
    }
    accBias[0] /= n;
    accBias[1] /= n;
    accBias[2] /= n;
    Serial.print("ACC BIAS:");
    Serial.print(accBias[0]);
    Serial.print('/');
    Serial.print(accBias[1]);
    Serial.print('/');
    Serial.println(accBias[2]);
  }
}
#endif

void printTime()
{
  time_t utc;
  time(&utc);
  struct tm *btm = gmtime(&utc);
  if (btm->tm_year > 100) {
    // valid system time available
    char buf[64];
    sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u",
      1900 + btm->tm_year, btm->tm_mon + 1, btm->tm_mday, btm->tm_hour, btm->tm_min, btm->tm_sec);
    Serial.print("UTC:");
    Serial.println(buf);
  }
}

/*******************************************************************************
  Initializing all data logging components
*******************************************************************************/
void initialize()
{
  // dump buffer data
  bufman.purge();

#if ENABLE_MEMS
  if (state.check(STATE_MEMS_READY)) {
    calibrateMEMS();
  }
#endif

#if GNSS == GNSS_STANDALONE
#if !ENABLE_HTTPD
  if (!state.check(STATE_GPS_READY)) {
    if (initGPS()) {
      state.set(STATE_GPS_READY);
    }
  }
#endif
#endif

#if ENABLE_OBD
#if !ENABLE_HTTPD
  if (!state.check(STATE_OBD_READY)) {
    timeoutsOBD = 0;
    if (obd.init()) {
      Serial.println("OBD:OK");
      state.set(STATE_OBD_READY);
#if ENABLE_OLED
      oled.println("OBD OK");
#endif
    } else {
      Serial.println("OBD:NO");
    }
  }
#endif
#endif

#if STORAGE != STORAGE_NONE
  if (!state.check(STATE_STORAGE_READY)) {
    // init storage
    if (logger.init()) {
      state.set(STATE_STORAGE_READY);
    }
  }
  if (state.check(STATE_STORAGE_READY)) {
    fileid = logger.begin();
  }
#endif

  // re-try OBD if connection not established
#if ENABLE_OBD
#if !ENABLE_HTTPD
  if (state.check(STATE_OBD_READY)) {
    char buf[128];
    if (obd.getVIN(buf, sizeof(buf))) {
      memcpy(vin, buf, sizeof(vin) - 1);
      vin[sizeof(vin) - 1] = 0;
      Serial.print("VIN:");
      Serial.println(vin);
    }
    byte ndtc2 = obd.readDTC(dtc, sizeof(dtc) / sizeof(dtc[0]));
    g_dtc_count = (ndtc2 > 6) ? 6 : ndtc2;
    if (ndtc2 > 0) {
      Serial.print("DTC:");
      Serial.println((int)ndtc2);
    }
#if ENABLE_OLED
    oled.print("VIN:");
    oled.println(vin);
#endif
  }
#endif
#endif

  // check system time
  printTime();

  lastMotionTime = millis();
  state.set(STATE_WORKING);

#if ENABLE_OLED
  delay(1000);
  oled.clear();
  oled.print("DEVICE ID: ");
  oled.println(devid);
  oled.setCursor(0, 7);
  oled.print("Packets");
  oled.setCursor(80, 7);
  oled.print("KB Sent");
  oled.setFontSize(FONT_SIZE_MEDIUM);
#endif

#if ENABLE_HTTPD
  portalHwSetupPending = true;
#endif
}

void showStats()
{
  uint32_t t = millis() - teleClient.startTime;
  char buf[32];
  sprintf(buf, "%02u:%02u.%c ", t / 60000, (t % 60000) / 1000, (t % 1000) / 100 + '0');
  serialLogPrintf("[NET] %s| Packet #%lu | Out: %lu KB | In: %lu bytes | %u KB/h\n",
      buf,
      (unsigned long)teleClient.txCount,
      (unsigned long)(teleClient.txBytes >> 10),
      (unsigned long)teleClient.rxBytes,
      (unsigned int)((uint64_t)(teleClient.txBytes + teleClient.rxBytes) * 3600 / (millis() - teleClient.startTime)));
#if ENABLE_OLED
  oled.setCursor(0, 2);
  oled.println(timestr);
  oled.setCursor(0, 5);
  oled.printInt(teleClient.txCount, 2);
  oled.setCursor(80, 5);
  oled.printInt(teleClient.txBytes >> 10, 3);
#endif
}

bool waitMotion(long timeout)
{
#if ENABLE_MEMS
  unsigned long t = millis();
  if (state.check(STATE_MEMS_READY)) {
    do {
      // calculate relative movement
      float motion = 0;
      float acc[3];
      if (!mems->read(acc)) continue;
      if (accCount == 10) {
        accCount = 0;
        accSum[0] = 0;
        accSum[1] = 0;
        accSum[2] = 0;
      }
      accSum[0] += acc[0];
      accSum[1] += acc[1];
      accSum[2] += acc[2];
      accCount++;
      for (byte i = 0; i < 3; i++) {
        float m = (acc[i] - accBias[i]);
        motion += m * m;
      }
#if ENABLE_HTTPD
      serverProcess(100);
#endif
      processBLE(100);
      // check movement
      if (motion >= MOTION_THRESHOLD * MOTION_THRESHOLD) {
        //lastMotionTime = millis();
        Serial.println(motion);
        return true;
      }
    } while (state.check(STATE_STANDBY) && ((long)(millis() - t) < timeout || timeout == -1));
    return false;
  }
#endif
  serverProcess(timeout);
  return false;
}

/*******************************************************************************
  Collecting and processing data
*******************************************************************************/
void process()
{
  static uint32_t lastGPStick = 0;
  uint32_t startTime = millis();

#if ENABLE_HTTPD
  if (portalHwSetupPending) {
    portalHwSetupPending = false;
    for (int i = 0; i < 12; i++) {
      serverProcess(20);
      yield();
    }
#if GNSS == GNSS_STANDALONE
    if (!state.check(STATE_GPS_READY)) {
      if (initGPS()) {
        state.set(STATE_GPS_READY);
      }
    }
#endif
#if ENABLE_OBD
    if (!state.check(STATE_OBD_READY)) {
      timeoutsOBD = 0;
      if (obd.init()) {
        Serial.println("OBD:OK");
        state.set(STATE_OBD_READY);
#if ENABLE_OLED
        oled.println("OBD OK");
#endif
      } else {
        Serial.println("OBD:NO");
      }
    }
    if (state.check(STATE_OBD_READY)) {
      char buf[128];
      if (obd.getVIN(buf, sizeof(buf))) {
        memcpy(vin, buf, sizeof(vin) - 1);
        vin[sizeof(vin) - 1] = 0;
        Serial.print("VIN:");
        Serial.println(vin);
      }
      byte ndtc = obd.readDTC(dtc, sizeof(dtc) / sizeof(dtc[0]));
      g_dtc_count = (ndtc > 6) ? 6 : ndtc;
      int dtcCount = (int)ndtc;
      if (dtcCount > 0) {
        Serial.print("DTC:");
        Serial.println(dtcCount);
      }
#if ENABLE_OLED
      oled.print("VIN:");
      oled.println(vin);
#endif
    }
#endif
  }
#endif

  CBuffer* buffer = bufman.getFree();
  buffer->state = BUFFER_STATE_FILLING;

#if ENABLE_OBD
  // process OBD data if connected
  if (!state.check(STATE_OBD_READY)) {
    g_dtc_count = 0;
  }
  if (state.check(STATE_OBD_READY)) {
    processOBD(buffer);
    {
      static uint32_t s_lastDtcPoll = 0;
      uint32_t now = millis();
      if (s_lastDtcPoll == 0 || (now - s_lastDtcPoll) >= 8000) {
        s_lastDtcPoll = now;
        byte nd = obd.readDTC(dtc, sizeof(dtc) / sizeof(dtc[0]));
        g_dtc_count = (nd > 6) ? 6 : nd;
      }
    }
    if (obd.errors >= MAX_OBD_ERRORS) {
      if (!obd.init()) {
        Serial.println("[OBD] ECU OFF");
        state.clear(STATE_OBD_READY | STATE_WORKING);
        g_dtc_count = 0;
        return;
      }
    }
  } else if (obd.init(PROTO_AUTO, true)) {
    state.set(STATE_OBD_READY);
    Serial.println("[OBD] ECU ON");
  }
#endif

  if (rssi != rssiLast) {
    int val = (rssiLast = rssi);
    buffer->add(PID_CSQ, ELEMENT_INT32, &val, sizeof(val));
  }
#if ENABLE_OBD
  if (sys.devType > 12) {
    batteryVoltage = (float)(analogRead(A0) * 45) / 4095;
  } else {
    batteryVoltage = obd.getVoltage();
  }
  if (batteryVoltage) {
    uint16_t v = batteryVoltage * 100;
    buffer->add(PID_BATTERY_VOLTAGE, ELEMENT_UINT16, &v, sizeof(v));
  }
#endif

#if LOG_EXT_SENSORS
  processExtInputs(buffer);
#endif

#if ENABLE_MEMS
  processMEMS(buffer);
#endif

  bool success = processGPS(buffer);
#if GNSS_RESET_TIMEOUT
  if (success) {
    lastGPStick = millis();
    state.set(STATE_GPS_ONLINE);
  } else {
    if (millis() - lastGPStick > GNSS_RESET_TIMEOUT * 1000) {
      sys.gpsEnd();
      state.clear(STATE_GPS_ONLINE | STATE_GPS_READY);
      delay(20);
      if (initGPS()) state.set(STATE_GPS_READY);
      lastGPStick = millis();
    }
  }
#endif

  if (!state.check(STATE_MEMS_READY)) {
    deviceTemp = readChipTemperature();
  }
  buffer->add(PID_DEVICE_TEMP, ELEMENT_INT32, &deviceTemp, sizeof(deviceTemp));

  buffer->timestamp = millis();
  buffer->state = BUFFER_STATE_FILLED;
#if ENABLE_HTTPD && ENABLE_WIFI && ENABLE_WIFI_PORTAL
  api_push_on_buffer(buffer);
#endif

  // display file buffer stats
  if (startTime - lastStatsTime >= 3000) {
    bufman.printStats();
    lastStatsTime = startTime;
  }

#if STORAGE != STORAGE_NONE
  if (state.check(STATE_STORAGE_READY)) {
    buffer->serialize(logger);
    uint16_t sizeKB = (uint16_t)(logger.size() >> 10);
    if (sizeKB != lastSizeKB) {
      logger.flush();
      lastSizeKB = sizeKB;
      Serial.print("[FILE] ");
      Serial.print(sizeKB);
      Serial.println("KB");
    }
  }
#endif

  const int dataIntervals[] = DATA_INTERVAL_TABLE;
#if ENABLE_OBD || ENABLE_MEMS
  // motion adaptive data interval control
  const uint16_t stationaryTime[] = STATIONARY_TIME_TABLE;
  unsigned int motionless = (millis() - lastMotionTime) / 1000;
  bool stationary = true;
  for (byte i = 0; i < sizeof(stationaryTime) / sizeof(stationaryTime[0]); i++) {
    dataInterval = dataIntervals[i];
    if (motionless < stationaryTime[i] || stationaryTime[i] == 0) {
      stationary = false;
      break;
    }
  }
  if (stationary) {
#if ENABLE_HTTPD && ENABLE_WIFI
    /* Don't end the "trip" while a phone/PC is on the SoftAP — standby() tears down STA/UDP and kills the portal session. */
    if (WiFi.softAPgetStationNum() > 0) {
      lastMotionTime = millis();
    } else
#endif
    {
      Serial.print("Stationary for ");
      Serial.print(motionless);
      Serial.println(" secs");
      state.clear(STATE_WORKING);
      return;
    }
  }
#else
  dataInterval = dataIntervals[0];
#endif
  /* Interleave HTTP (portal) with BLE/delay — stock code only called serverProcess from standby paths,
   * so the miniweb stack never ran during normal logging and the portal appeared dead. */
  {
    uint32_t endAt = millis() + (uint32_t)dataInterval;
    uint16_t iter = 0;
    while ((int32_t)(endAt - millis()) > 0 && ++iter < 5000) {
#if ENABLE_HTTPD
      serverProcess(0);
#endif
      int remain = (int)(endAt - millis());
      if (remain < 1) break;
#if ENABLE_BLE
      /* xQueueReceive(timeout) uses timeout/portTICK_PERIOD_MS — sub-tick timeouts round to 0 and
       * return immediately → tight spin, starve IDLE/WiFi, abort(). Sleep tiny gaps instead. */
      if (remain < (int)portTICK_PERIOD_MS) {
        delay((uint32_t)remain);
        continue;
      }
#endif
      int slice = remain > 40 ? 40 : remain;
      processBLE(slice);
      yield();
    }
  }
}

bool initCell(bool quick = false)
{
  Serial.println("[CELL] Activating...");
  // power on network module
  if (!teleClient.cell.begin(&sys)) {
    Serial.println("[CELL] No supported module");
#if ENABLE_OLED
    oled.println("No Cell Module");
#endif
    return false;
  }
  if (quick) return true;
#if ENABLE_OLED
    oled.print(teleClient.cell.deviceName());
    oled.println(" OK\r");
    oled.print("IMEI:");
    oled.println(teleClient.cell.IMEI);
#endif
  Serial.print("CELL:");
  Serial.println(teleClient.cell.deviceName());
  if (!teleClient.cell.checkSIM(SIM_CARD_PIN)) {
    Serial.println("NO SIM CARD");
    //return false;
  }
  Serial.print("IMEI:");
  Serial.println(teleClient.cell.IMEI);
  Serial.println("[CELL] Searching...");
  if (*apn) {
    Serial.print("APN:");
    Serial.println(apn);
  }
  if (teleClient.cell.setup(apn, APN_USERNAME, APN_PASSWORD)) {
    netop = teleClient.cell.getOperatorName();
    if (netop.length()) {
      Serial.print("Operator:");
      Serial.println(netop);
#if ENABLE_OLED
      oled.println(op);
#endif
    }

#if GNSS == GNSS_CELLULAR
    if (teleClient.cell.setGPS(true)) {
      Serial.println("CELL GNSS:OK");
    }
#endif

    ip = teleClient.cell.getIP();
    if (ip.length()) {
      Serial.print("[CELL] IP:");
      Serial.println(ip);
#if ENABLE_OLED
      oled.print("IP:");
      oled.println(ip);
#endif
    }
    state.set(STATE_CELL_CONNECTED);
  } else {
    char *p = strstr(teleClient.cell.getBuffer(), "+CPSI:");
    if (p) {
      char *q = strchr(p, '\r');
      if (q) *q = 0;
      Serial.print("[CELL] ");
      Serial.println(p + 7);
#if ENABLE_OLED
      oled.println(p + 7);
#endif
    } else {
      Serial.print(teleClient.cell.getBuffer());
    }
  }
  timeoutsNet = 0;
  return state.check(STATE_CELL_CONNECTED);
}

/*******************************************************************************
  Initializing network, maintaining connection and doing transmissions
*******************************************************************************/
void telemetry(void* inst)
{
  uint32_t lastRssiTime = 0;
  uint8_t connErrors = 0;
  CStorageRAM store;
  store.init(
#if BOARD_HAS_PSRAM
    (char*)heap_caps_malloc(SERIALIZE_BUFFER_SIZE, MALLOC_CAP_SPIRAM),
#else
    (char*)malloc(SERIALIZE_BUFFER_SIZE),
#endif
    SERIALIZE_BUFFER_SIZE
  );
  teleClient.reset();

  for (;;) {
    if (state.check(STATE_STANDBY)) {
      if (state.check(STATE_CELL_CONNECTED) || state.check(STATE_WIFI_CONNECTED)) {
        teleClient.shutdown();
        netop = "";
        ip = "";
        rssi = 0;
      }
      state.clear(STATE_NET_READY | STATE_CELL_CONNECTED | STATE_WIFI_CONNECTED);
      teleClient.reset();
      bufman.purge();

      uint32_t t = millis();
      do {
        delay(1000);
      } while (state.check(STATE_STANDBY) && millis() - t < 1000L * PING_BACK_INTERVAL);
      if (state.check(STATE_STANDBY)) {
        // start ping
#if ENABLE_WIFI
        if (wifiSSID[0]) { 
          Serial.print("[WIFI] Joining SSID:");
          Serial.println(wifiSSID);
          wifiApplyStaHostname();
          teleClient.wifi.begin(wifiSSID, wifiPassword);
        }
        if (teleClient.wifi.setup()) {
          Serial.println("[WIFI] Ping...");
          teleClient.ping();
        }
        else
#endif
        {
          if (initCell()) {
            Serial.println("[CELL] Ping...");
            teleClient.ping();
          }
        }
        teleClient.shutdown();
        state.clear(STATE_CELL_CONNECTED | STATE_WIFI_CONNECTED);
      }
      continue;
    }

#if ENABLE_WIFI && ENABLE_HTTPD && ENABLE_WIFI_PORTAL
    if (wifi_portal_consume_sta_reconnect_pending()) {
      Serial.println("[PORTAL] Applying new STA credentials (keeping SoftAP up)");
      state.clear(STATE_WIFI_CONNECTED | STATE_NET_READY);
      netop = "";
      ip = "";
      rssi = 0;
      teleClient.shutdown();
      /* disconnect(false): STA only. disconnect(true) turns the radio off and kills the portal AP. */
      WiFi.disconnect(false);
      delay(100);
      WiFi.setAutoReconnect(true);
    }
#endif

#if ENABLE_WIFI
    if (wifiSSID[0] && !state.check(STATE_WIFI_CONNECTED)) {
      Serial.print("[WIFI] Joining SSID:");
      Serial.println(wifiSSID);
      wifiApplyStaHostname();
      teleClient.wifi.begin(wifiSSID, wifiPassword);
      teleClient.wifi.setup();
    }
#endif

    while (state.check(STATE_WORKING)) {
#if ENABLE_WIFI
      if (wifiSSID[0]) {
        if (!state.check(STATE_WIFI_CONNECTED) && teleClient.wifi.connected()) {
          ip = teleClient.wifi.getIP();
          if (ip.length()) {
            Serial.print("[WIFI] IP:");
            Serial.println(ip);
          }
          connErrors = 0;
          if (teleClient.connect()) {
            state.set(STATE_WIFI_CONNECTED | STATE_NET_READY);
            beep(50);
            // switch off cellular module when wifi connected
            if (state.check(STATE_CELL_CONNECTED)) {
              teleClient.cell.end();
              state.clear(STATE_CELL_CONNECTED);
              Serial.println("[CELL] Deactivated");
            }
          }
        } else if (state.check(STATE_WIFI_CONNECTED) && !teleClient.wifi.connected()) {
          Serial.println("[WIFI] Disconnected");
          state.clear(STATE_WIFI_CONNECTED);
        }
      }
#endif
      if (!state.check(STATE_WIFI_CONNECTED) && !state.check(STATE_CELL_CONNECTED)) {
        connErrors = 0;
        if (!initCell() || !teleClient.connect()) {
          teleClient.cell.end();
          state.clear(STATE_NET_READY | STATE_CELL_CONNECTED);
          Serial.println("[CELL] Deactivated");
          // avoid turning on/off cellular module too frequently to avoid operator banning
          delay(60000 * 3);
          break;
        }
        Serial.println("[CELL] In service");
        state.set(STATE_NET_READY);
        beep(50);
      }

      if (millis() - lastRssiTime > SIGNAL_CHECK_INTERVAL * 1000) {
#if ENABLE_WIFI
        if (state.check(STATE_WIFI_CONNECTED))
        {
          rssi = teleClient.wifi.RSSI();
        }
        else
#endif
        {
          rssi = teleClient.cell.RSSI();
        }
        if (rssi) {
          Serial.print("RSSI:");
          Serial.print(rssi);
          Serial.println("dBm");
        }
        lastRssiTime = millis();

#if ENABLE_WIFI
        if (wifiSSID[0] && !state.check(STATE_WIFI_CONNECTED)) {
          wifiApplyStaHostname();
          teleClient.wifi.begin(wifiSSID, wifiPassword);
        }
#endif
      }

      // get data from buffer
      CBuffer* buffer = bufman.getNewest();
      if (!buffer) {
        delay(50);
        continue;
      }
#if SERVER_PROTOCOL == PROTOCOL_UDP
      store.header(devid);
#endif
      store.timestamp(buffer->timestamp);
      buffer->serialize(store);
      bufman.free(buffer);
      store.tailer();
      serialLogDatLine(store.buffer());

      // start transmission
#ifdef PIN_LED
      if (ledMode == 0) digitalWrite(PIN_LED, HIGH);
#endif

      if (teleClient.transmit(store.buffer(), store.length())) {
        // successfully sent
        connErrors = 0;
        showStats();
      } else {
        timeoutsNet++;
        connErrors++;
        printTimeoutStats();
        if (connErrors < MAX_CONN_ERRORS_RECONNECT) {
          // quick reconnect
          teleClient.connect(true);
        }
      }
#ifdef PIN_LED
      if (ledMode == 0) digitalWrite(PIN_LED, LOW);
#endif
      store.purge();

      teleClient.inbound();
#if ENABLE_HTTPD && ENABLE_WIFI && ENABLE_WIFI_PORTAL
      api_push_service();
#endif

      if (state.check(STATE_CELL_CONNECTED) && !teleClient.cell.check(1000)) {
        Serial.println("[CELL] Not in service");
        state.clear(STATE_NET_READY | STATE_CELL_CONNECTED);
        break;
      }

      if (syncInterval > 10000 && millis() - teleClient.lastSyncTime > syncInterval) {
        Serial.println("[NET] Poor connection");
        timeoutsNet++;
        if (!teleClient.connect()) {
          connErrors++;
        }
      }

      if (connErrors >= MAX_CONN_ERRORS_RECONNECT) {
#if ENABLE_WIFI
        if (state.check(STATE_WIFI_CONNECTED)) {
          teleClient.wifi.end();
          state.clear(STATE_NET_READY | STATE_WIFI_CONNECTED);
          break;
        }
#endif
        if (state.check(STATE_CELL_CONNECTED)) {
          teleClient.cell.end();
          state.clear(STATE_NET_READY | STATE_CELL_CONNECTED);
          break;
        }
      }

      if (deviceTemp >= COOLING_DOWN_TEMP) {
        // device too hot, cool down by pause transmission
        Serial.print("HIGH DEVICE TEMP: ");
        Serial.println(deviceTemp);
        bufman.purge();
      }

    }
  }
}

/*******************************************************************************
  Implementing stand-by mode
*******************************************************************************/
void standby()
{
  state.set(STATE_STANDBY);
#if STORAGE != STORAGE_NONE
  if (state.check(STATE_STORAGE_READY)) {
    logger.end();
  }
#endif

#if !GNSS_ALWAYS_ON && GNSS == GNSS_STANDALONE
  if (state.check(STATE_GPS_READY)) {
    Serial.println("[GNSS] OFF");
    sys.gpsEnd(true);
    state.clear(STATE_GPS_READY | STATE_GPS_ONLINE);
    gd = 0;
  }
#endif

  state.clear(STATE_WORKING | STATE_OBD_READY | STATE_STORAGE_READY);
  // this will put co-processor into sleep mode
#if ENABLE_OLED
  oled.print("STANDBY");
  delay(1000);
  oled.clear();
#endif
  Serial.println("STANDBY");
  obd.enterLowPowerMode();
#if ENABLE_MEMS
  calibrateMEMS();
  waitMotion(-1);
#elif ENABLE_OBD
  do {
    delay(5000);
  } while (obd.getVoltage() < JUMPSTART_VOLTAGE);
#else
  delay(5000);
#endif
  Serial.println("WAKEUP");
  sys.resetLink();
#if RESET_AFTER_WAKEUP
#if ENABLE_MEMS
  if (mems) mems->end();  
#endif
  ESP.restart();
#endif  
  state.clear(STATE_STANDBY);
}

/*******************************************************************************
  Tasks to perform in idle/waiting time
*******************************************************************************/
void genDeviceID(char* buf)
{
    uint64_t seed = ESP.getEfuseMac() >> 8;
    for (int i = 0; i < 8; i++, seed >>= 5) {
      byte x = (byte)seed & 0x1f;
      if (x >= 10) {
        x = x - 10 + 'A';
        switch (x) {
          case 'B': x = 'W'; break;
          case 'D': x = 'X'; break;
          case 'I': x = 'Y'; break;
          case 'O': x = 'Z'; break;
        }
      } else {
        x += '0';
      }
      buf[i] = x;
    }
    buf[8] = 0;
}

void showSysInfo()
{
  Serial.print("CPU:");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.print("MHz FLASH:");
  Serial.print(ESP.getFlashChipSize() >> 20);
  Serial.println("MB");
  Serial.print("IRAM:");
  Serial.print(ESP.getHeapSize() >> 10);
  Serial.print("KB");
#if BOARD_HAS_PSRAM
  if (psramInit()) {
    Serial.print(" PSRAM:");
    Serial.print(esp_spiram_get_size() >> 20);
    Serial.print("MB");
  }
#endif
  Serial.println();

  int rtc = rtc_clk_slow_freq_get();
  if (rtc) {
    Serial.print("RTC:");
    Serial.println(rtc);
  }

#if ENABLE_OLED
  oled.clear();
  oled.print("CPU:");
  oled.print(ESP.getCpuFreqMHz());
  oled.print("Mhz ");
  oled.print(getFlashSize() >> 10);
  oled.println("MB Flash");
#endif

  Serial.print("DEVICE ID:");
  Serial.println(devid);
#if ENABLE_OLED
  oled.print("DEVICE ID:");
  oled.println(devid);
#endif
}

void loadConfig()
{
  size_t len;
  len = sizeof(apn);
  apn[0] = 0;
  nvs_get_str(nvs, "CELL_APN", apn, &len);
  if (!apn[0]) {
    strcpy(apn, CELL_APN);
  }

#if ENABLE_WIFI
  len = sizeof(wifiSSID);
  nvs_get_str(nvs, "WIFI_SSID", wifiSSID, &len);
  len = sizeof(wifiPassword);
  nvs_get_str(nvs, "WIFI_PWD", wifiPassword, &len);
#endif
}

void processBLE(int timeout)
{
#if ENABLE_BLE
  if (!g_rt_ble) {
    if (timeout) {
      delay(timeout);
    }
    return;
  }
  static byte echo = 0;
  char* cmd;
  if (!(cmd = ble_recv_command(timeout))) {
    return;
  }

  char *p = strchr(cmd, '\r');
  if (p) *p = 0;
  char buf[48];
  int bufsize = sizeof(buf);
  int n = 0;
  if (echo) n += snprintf(buf + n, bufsize - n, "%s\r", cmd);
  Serial.print("[BLE] ");
  Serial.print(cmd);
  if (!strcmp(cmd, "UPTIME") || !strcmp(cmd, "TICK")) {
    n += snprintf(buf + n, bufsize - n, "%lu", millis());
  } else if (!strcmp(cmd, "BATT")) {
    n += snprintf(buf + n, bufsize - n, "%.2f", (float)(analogRead(A0) * 42) / 4095);
  } else if (!strcmp(cmd, "RESET")) {
#if STORAGE
    logger.end();
#endif
    ESP.restart();
    // never reach here
  } else if (!strcmp(cmd, "OFF")) {
    state.set(STATE_STANDBY);
    state.clear(STATE_WORKING);
    n += snprintf(buf + n, bufsize - n, "OK");
  } else if (!strcmp(cmd, "ON")) {
    state.clear(STATE_STANDBY);
    n += snprintf(buf + n, bufsize - n, "OK");
  } else if (!strcmp(cmd, "ON?")) {
    n += snprintf(buf + n, bufsize - n, "%u", state.check(STATE_STANDBY) ? 0 : 1);
  } else if (!strcmp(cmd, "APN?")) {
    n += snprintf(buf + n, bufsize - n, "%s", *apn ? apn : "DEFAULT");
  } else if (!strncmp(cmd, "APN=", 4)) {
    n += snprintf(buf + n, bufsize - n, nvs_set_str(nvs, "CELL_APN", strcmp(cmd + 4, "DEFAULT") ? cmd + 4 : "") == ESP_OK ? "OK" : "ERR");
    loadConfig();
  } else if (!strcmp(cmd, "NET_OP")) {
    if (state.check(STATE_WIFI_CONNECTED)) {
#if ENABLE_WIFI
      n += snprintf(buf + n, bufsize - n, "%s", wifiSSID[0] ? wifiSSID : "-");
#endif
    } else {
      snprintf(buf + n, bufsize - n, "%s", netop.length() ? netop.c_str() : "-");
      char *p = strchr(buf + n, ' ');
      if (p) *p = 0;
      n += strlen(buf + n);
    }
  } else if (!strcmp(cmd, "NET_IP")) {
    n += snprintf(buf + n, bufsize - n, "%s", ip.length() ? ip.c_str() : "-");
  } else if (!strcmp(cmd, "NET_PACKET")) {
      n += snprintf(buf + n, bufsize - n, "%u", teleClient.txCount);
  } else if (!strcmp(cmd, "NET_DATA")) {
      n += snprintf(buf + n, bufsize - n, "%u", teleClient.txBytes);
  } else if (!strcmp(cmd, "NET_RATE")) {
      n += snprintf(buf + n, bufsize - n, "%u", teleClient.startTime ? (unsigned int)((uint64_t)(teleClient.txBytes + teleClient.rxBytes) * 3600 / (millis() - teleClient.startTime)) : 0);
  } else if (!strcmp(cmd, "RSSI")) {
    n += snprintf(buf + n, bufsize - n, "%d", rssi);
#if ENABLE_WIFI
  } else if (!strcmp(cmd, "SSID?")) {
    n += snprintf(buf + n, bufsize - n, "%s", wifiSSID[0] ? wifiSSID : "-");
  } else if (!strncmp(cmd, "SSID=", 5)) {
    const char* p = cmd + 5;
    n += snprintf(buf + n, bufsize - n, nvs_set_str(nvs, "WIFI_SSID", strcmp(p, "-") ? p : "") == ESP_OK ? "OK" : "ERR");
    loadConfig();
  } else if (!strcmp(cmd, "WPWD?")) {
    n += snprintf(buf + n, bufsize - n, "%s", wifiPassword[0] ? wifiPassword : "-");
  } else if (!strncmp(cmd, "WPWD=", 5)) {
    const char* p = cmd + 5;
    n += snprintf(buf + n, bufsize - n, nvs_set_str(nvs, "WIFI_PWD", strcmp(p, "-") ? p : "") == ESP_OK ? "OK" : "ERR");
    loadConfig();
#else
  } else if (!strcmp(cmd, "SSID?") || !strcmp(cmd, "WPWD?")) {
    n += snprintf(buf + n, bufsize - n, "-");
#endif
#if ENABLE_MEMS
  } else if (!strcmp(cmd, "TEMP")) {
    n += snprintf(buf + n, bufsize - n, "%d", (int)deviceTemp);
  } else if (!strcmp(cmd, "ACC")) {
    n += snprintf(buf + n, bufsize - n, "%.1f/%.1f/%.1f", acc[0], acc[1], acc[2]);
  } else if (!strcmp(cmd, "GYRO")) {
    n += snprintf(buf + n, bufsize - n, "%.1f/%.1f/%.1f", gyr[0], gyr[1], gyr[2]);
  } else if (!strcmp(cmd, "GF")) {
    n += snprintf(buf + n, bufsize - n, "%f", (float)sqrt(acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2]));
#endif
  } else if (!strcmp(cmd, "ATE0")) {
    echo = 0;
    n += snprintf(buf + n, bufsize - n, "OK");
  } else if (!strcmp(cmd, "ATE1")) {
    echo = 1;
    n += snprintf(buf + n, bufsize - n, "OK");
  } else if (!strcmp(cmd, "FS")) {
    n += snprintf(buf + n, bufsize - n, "%u",
#if STORAGE == STORAGE_NONE
    0
#else
    logger.size()
#endif
      );
  } else if (!memcmp(cmd, "01", 2)) {
    byte pid = hex2uint8(cmd + 2);
    for (byte i = 0; i < sizeof(obdData) / sizeof(obdData[0]); i++) {
      if (obdData[i].pid == pid) {
        n += snprintf(buf + n, bufsize - n, "%d", obdData[i].value);
        pid = 0;
        break;
      }
    }
    if (pid) {
      int value;
      if (obd.readPID(pid, value)) {
        n += snprintf(buf + n, bufsize - n, "%d", value);
      } else {
        n += snprintf(buf + n, bufsize - n, "N/A");
      }
    }
  } else if (!strcmp(cmd, "VIN")) {
    n += snprintf(buf + n, bufsize - n, "%s", vin[0] ? vin : "N/A");
  } else if (!strcmp(cmd, "LAT") && gd) {
    n += snprintf(buf + n, bufsize - n, "%f", gd->lat);
  } else if (!strcmp(cmd, "LNG") && gd) {
    n += snprintf(buf + n, bufsize - n, "%f", gd->lng);
  } else if (!strcmp(cmd, "ALT") && gd) {
    n += snprintf(buf + n, bufsize - n, "%d", (int)gd->alt);
  } else if (!strcmp(cmd, "SAT") && gd) {
    n += snprintf(buf + n, bufsize - n, "%u", (unsigned int)gd->sat);
  } else if (!strcmp(cmd, "SPD") && gd) {
    n += snprintf(buf + n, bufsize - n, "%d", (int)(gd->speed * 1852 / 1000));
  } else if (!strcmp(cmd, "CRS") && gd) {
    n += snprintf(buf + n, bufsize - n, "%u", (unsigned int)gd->heading);
  } else {
    n += snprintf(buf + n, bufsize - n, "ERROR");
  }
  Serial.print(" -> ");
  Serial.println((p = strchr(buf, '\r')) ? p + 1 : buf);
  if (n < bufsize - 1) {
    buf[n++] = '\r';
  } else {
    n = bufsize - 1;
  }
  buf[n] = 0;
  ble_send_response(buf, n, cmd);
#else
  if (timeout) delay(timeout);
#endif
}

void setup()
{
  delay(500);

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK( err );
  err = nvs_open("storage", NVS_READWRITE, &nvs);
  if (err == ESP_OK) {
    loadConfig();
    netcfg_reload_from_nvs();
    obdUserPidsInit(nvs);
  }

#if ENABLE_OLED
  oled.begin();
  oled.setFontSize(FONT_SIZE_SMALL);
#endif
  // initialize USB serial
  Serial.begin(115200);

  // init LED pin
#ifdef PIN_LED
  pinMode(PIN_LED, OUTPUT);
  if (ledMode == 0) digitalWrite(PIN_LED, HIGH);
#endif

  // generate unique device ID
  genDeviceID(devid);
#if ENABLE_HTTPD && ENABLE_WIFI && ENABLE_WIFI_PORTAL
  if (err == ESP_OK) {
    api_push_init(nvs, devid);
  }
#endif

#if CONFIG_MODE_TIMEOUT
  configMode();
#endif

#if LOG_EXT_SENSORS == 1
  pinMode(PIN_SENSOR1, INPUT);
  pinMode(PIN_SENSOR2, INPUT);
#elif LOG_EXT_SENSORS == 2
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_11);
#endif

  // show system information
  showSysInfo();

  bufman.init();
  
  //Serial.print(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10);
  //Serial.println("KB");

#if ENABLE_OBD
  if (sys.begin()) {
    Serial.print("TYPE:");
    Serial.println(sys.devType);
    obd.begin(sys.link);
  }
#else
  sys.begin(false, true);
#endif

#if ENABLE_MEMS
if (!state.check(STATE_MEMS_READY)) do {
  Serial.print("MEMS:");
  mems = new ICM_42627;
  byte ret = mems->begin();
  if (ret) {
    state.set(STATE_MEMS_READY);
    Serial.println("ICM-42627");
    break;
  }
  delete mems;
  mems = new ICM_20948_I2C;
  ret = mems->begin();
  if (ret) {
    state.set(STATE_MEMS_READY);
    Serial.println("ICM-20948");
    break;
  } 
  delete mems;
  /*
  mems = new MPU9250;
  ret = mems->begin();
  if (ret) {
    state.set(STATE_MEMS_READY);
    Serial.println("MPU-9250");
    break;
  }
  */
  mems = 0;
  Serial.println("NO");
} while (0);
#endif

#if ENABLE_HTTPD
  IPAddress ip;
  if (serverSetup(ip)) {
    Serial.println("HTTPD:");
    Serial.println(ip);
#if ENABLE_OLED
    oled.println(ip);
#endif
  } else {
    Serial.println("HTTPD:NO");
  }
#endif

  state.set(STATE_WORKING);

  // initialize components (OBD/GNSS deferred to first process() when ENABLE_HTTPD)
  initialize();

#if ENABLE_BLE
  if (g_rt_ble) {
    char bleNm[32];
    bleNm[0] = 0;
    if (nvs) {
      size_t bleL = sizeof(bleNm);
      if (nvs_get_str(nvs, "BLE_SPP_NM", bleNm, &bleL) != ESP_OK) {
        bleNm[0] = 0;
      }
    }
    if (!bleNm[0]) {
      strncpy(bleNm, "FreematicsPlus", sizeof(bleNm) - 1);
      bleNm[sizeof(bleNm) - 1] = 0;
    }
    ble_init(bleNm);
    Serial.println("[BLE] advertising (System tab to disable; reboot to apply)");
  } else {
    Serial.println("[BLE] off (NVS) — use portal System + reboot to enable");
  }
#endif

  // initialize network and maintain connection
  subtask.create(telemetry, "telemetry", 2, 8192);

#ifdef PIN_LED
  digitalWrite(PIN_LED, LOW);
#endif
}

void loop()
{
  // error handling
  if (!state.check(STATE_WORKING)) {
    standby();
#ifdef PIN_LED
    if (ledMode == 0) digitalWrite(PIN_LED, HIGH);
#endif
    initialize();
#ifdef PIN_LED
    digitalWrite(PIN_LED, LOW);
#endif
    return;
  }

  // collect and log data
  process();
}
