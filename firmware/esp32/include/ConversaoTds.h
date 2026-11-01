#pragma once
#include <stdint.h>

// R1: saída do módulo -> GPIO; R2: GPIO -> GND.
constexpr float TDS_R1_OHMS = 10000.0f;
constexpr float TDS_R2_OHMS = 15000.0f;
constexpr float TDS_TEMPERATURA_REFERENCIA_C = 25.0f;

inline float tensaoModuloTds(uint32_t milivoltsAdc) {
  return milivoltsAdc / 1000.0f * (TDS_R1_OHMS + TDS_R2_OHMS) / TDS_R2_OHMS;
}

// Estimativa de referência da biblioteca DFRobot/GravityTDS, K=1 e fator TDS=0,5.
// https://github.com/DFRobot/GravityTDS/blob/master/GravityTDS.cpp
// Pressupõe 25 °C; não usa uma temperatura medida nem substitui a calibração.
inline float estimarTdsPpm(float tensaoModuloV) {
  return (133.42f * tensaoModuloV * tensaoModuloV * tensaoModuloV
          - 255.86f * tensaoModuloV * tensaoModuloV
          + 857.39f * tensaoModuloV) * 0.5f;
}
