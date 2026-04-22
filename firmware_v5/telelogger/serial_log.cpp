#include "serial_log.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>

void serialLogDatLine(const char* payload)
{
  if (!payload) {
    return;
  }
  char line[SERIALIZE_BUFFER_SIZE + 24];
  int n = snprintf(line, sizeof(line), "[DAT] %s\n", payload);
  if (n <= 0) {
    return;
  }
  size_t len = (size_t)n;
  if (len >= sizeof(line)) {
    len = sizeof(line) - 1;
  }
  Serial.write((const uint8_t*)line, len);
}

void serialLogPrintf(const char* fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return;
  }
  size_t len = (size_t)n;
  if (len >= sizeof(buf)) {
    len = sizeof(buf) - 1;
  }
  Serial.write((const uint8_t*)buf, len);
}
