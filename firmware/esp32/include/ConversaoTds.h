#pragma once
#include <stdint.h>
#include <cmath>
#include <limits>

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

// Estimativa com temperatura medida; não equivale a calibração com padrão TDS.
inline float estimarTdsCompensado(float tensaoModuloV, float temperaturaC) {
  const float coeficiente = 1.0f + 0.02f * (temperaturaC - TDS_TEMPERATURA_REFERENCIA_C);
  if (!std::isfinite(tensaoModuloV) || !std::isfinite(temperaturaC) ||
      tensaoModuloV < 0.0f || tensaoModuloV > 2.3f || coeficiente <= 0.0f) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const float ppm = estimarTdsPpm(tensaoModuloV / coeficiente);
  return std::isfinite(ppm) && ppm >= 0.0f && ppm <= 1000.0f
      ? ppm : std::numeric_limits<float>::quiet_NaN();
}
