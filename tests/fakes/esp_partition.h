#pragma once
#include <cstddef>
#include <cstring>
constexpr int ESP_PARTITION_TYPE_DATA = 1;
constexpr int ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82;
constexpr int ESP_OK = 0;
struct esp_partition_t { size_t size; };
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) {
  static esp_partition_t part = {0x160000}; return &part;
}
inline int esp_partition_read(const esp_partition_t*, size_t, void* dados, size_t n) {
  memset(dados, 0, n); return ESP_OK; // Partição montável, não apagada.
}
