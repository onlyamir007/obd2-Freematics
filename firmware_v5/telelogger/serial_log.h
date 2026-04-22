#pragma once

#include <Arduino.h>

/* Single-buffer Serial lines so miniweb/httpd on another task cannot splice [DAT]/[BUF]/etc. */

void serialLogDatLine(const char* payload);

#if defined(__GNUC__)
void serialLogPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void serialLogPrintf(const char* fmt, ...);
#endif
