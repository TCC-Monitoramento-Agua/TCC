#pragma once
#include <stddef.h>
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 2
#define ESP_OK 0
struct esp_partition_t { size_t size; };
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) { return nullptr; }
inline int esp_partition_read(const esp_partition_t*, size_t, void*, size_t) { return -1; }
