#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// GPIOs do firmware/teste físico enviados pelo usuário (diferem do esquema antigo).
constexpr uint8_t PH_PIN = 34;
constexpr uint8_t TURBIDEZ_FISICA_PIN = 32;
constexpr uint8_t TEMPERATURA_PIN = 14;
constexpr size_t NUM_AMOSTRAS_FISICAS = 31;
constexpr unsigned long INTERVALO_AMOSTRAS_FISICAS_MS = 10;

// Pontos confirmados pelo usuário. São tensões calculadas por ADC/4095 * 3,3
// NO GPIO, já após o divisor; não aplicar de novo o fator do divisor.
constexpr float TENSAO_PH7 = 1.57f;
constexpr float TENSAO_PH4 = 1.95f;
constexpr float REFERENCIA_PH7 = 7.0f;
constexpr float REFERENCIA_PH4 = 4.0f;

// Referência do código de teste. Reavaliar se mudar água, posição ou circuito.
constexpr uint16_t ADC_AGUA_REFERENCIA = 2709;

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

// Queda percentual do sinal em relação à amostra inicial, NÃO concentração em NTU.
inline float turbidezRelativa(uint16_t adc, uint16_t referencia = ADC_AGUA_REFERENCIA) {
  if (!referencia) return std::numeric_limits<float>::quiet_NaN();
  const float queda = (static_cast<float>(referencia) - adc) * 100.0f / referencia;
  return queda < 0.0f ? 0.0f : (queda > 100.0f ? 100.0f : queda);
}

inline bool temperaturaFisicaValida(float temperatura) {
  return std::isfinite(temperatura) && temperatura >= -55.0f && temperatura <= 125.0f;
}
