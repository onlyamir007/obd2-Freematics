#ifndef API_PUSH_H
#define API_PUSH_H

#include <nvs.h>
#include <stddef.h>
#include <stdint.h>

/* Weak alternative transport (cellular) returns this when not linked or unavailable. */
#define API_PUSH_NO_ALT_NET (-1000)

int api_push_parse_url(const char* url, char* host, size_t hmax, char* path, size_t pmax, uint16_t* port, int* is_https);

class CBuffer;

void api_push_init(nvs_handle_t h, const char* deviceId);
void api_push_reload();
void api_push_on_buffer(CBuffer* buffer);
void api_push_service();
bool api_push_to_json(char* out, size_t outLen);
bool api_push_from_json(const char* body);
int api_push_test_auth(char* errOut, size_t errLen);

#endif
