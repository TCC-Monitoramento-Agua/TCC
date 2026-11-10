#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// Pinagem do protótipo físico; conferir diferenças em relação ao esquemático anterior.
constexpr uint8_t PH_PIN = 34;
constexpr uint8_t TURBIDEZ_FISICA_PIN = 32;
constexpr uint8_t TDS_FISICO_PIN = 35;
constexpr uint8_t TEMPERATURA_PIN = 14;
constexpr size_t NUM_AMOSTRAS_FISICAS = 31;
constexpr unsigned long INTERVALO_AMOSTRAS_FISICAS_MS = 10;

// Pontos de calibração registrados na escala ADC/4095 * 3,3
// NO GPIO, já após o divisor; não aplicar de novo o fator do divisor.
constexpr float TENSAO_PH7 = 1.57f;
constexpr float TENSAO_PH4 = 1.95f;
constexpr float REFERENCIA_PH7 = 7.0f;
constexpr float REFERENCIA_PH4 = 4.0f;

// Referências medidas no GPIO, após o divisor; escala demonstrativa.
constexpr double TURBIDEZ_AGUA_LIMPA_V = 1.57;
constexpr double TURBIDEZ_REFERENCIA_ESCURA_V = 0.07;
constexpr double TURBIDEZ_MAXIMA_DEMO = 1000.0;

inline double calcularTurbidezCurva(double volts) {
  if (!std::isfinite(volts) || volts < 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double valor = TURBIDEZ_MAXIMA_DEMO * (TURBIDEZ_AGUA_LIMPA_V - volts) /
      (TURBIDEZ_AGUA_LIMPA_V - TURBIDEZ_REFERENCIA_ESCURA_V);
  if (valor < 0.0) return 0.0;
  return valor > TURBIDEZ_MAXIMA_DEMO ? TURBIDEZ_MAXIMA_DEMO : valor;
}

inline float tensaoEscalaCalibracao(uint16_t adc) {
  return adc * (3.3f / 4095.0f);
}

inline float converterPh(float tensao) {
  if (!std::isfinite(tensao) || TENSAO_PH4 == TENSAO_PH7) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const float ph = REFERENCIA_PH7 + (tensao - TENSAO_PH7) *
      (REFERENCIA_PH4 - REFERENCIA_PH7) / (TENSAO_PH4 - TENSAO_PH7);
  return ph >= 0.0f && ph <= 14.0f ? ph : std::numeric_limits<float>::quiet_NaN();
}

inline bool temperaturaFisicaValida(float temperatura) {
  return std::isfinite(temperatura) && temperatura >= -55.0f && temperatura <= 125.0f;
}
