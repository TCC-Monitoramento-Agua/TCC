#pragma once
#include <stdint.h>
#include <cmath>
#include <limits>

// R1: saída analógica do módulo -> GPIO; R2: GPIO -> GND.
constexpr float TURBIDEZ_R1_OHMS = 10000.0f;
constexpr float TURBIDEZ_R2_OHMS = 15000.0f;

inline float tensaoModuloTurbidez(uint32_t milivoltsAdc) {
  return milivoltsAdc / 1000.0f *
      (TURBIDEZ_R1_OHMS + TURBIDEZ_R2_OHMS) / TURBIDEZ_R2_OHMS;
}

// Ajuste quadrático de referência para módulos do tipo SEN0189.
// A compatibilidade com o módulo da Eletrogate ainda não foi calibrada/validada.
// Não extrapola abaixo de 2,5 V, onde a curva deixa de ser útil.
inline float estimarTurbidezNtu(float tensaoModuloV) {
  if (!std::isfinite(tensaoModuloV) || tensaoModuloV < 2.5f || tensaoModuloV > 4.5f) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const float ntu = -1120.4f * tensaoModuloV * tensaoModuloV
      + 5742.3f * tensaoModuloV - 4352.9f;
  // O ajuste pode cruzar zero em água clara; turbidez negativa não tem significado.
  return ntu < 0.0f ? 0.0f : ntu;
}
