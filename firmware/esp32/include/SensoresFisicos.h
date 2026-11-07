#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// GPIOs do firmware/teste físico enviados pelo usuário (diferem do esquema antigo).
constexpr uint8_t PH_PIN = 34;
constexpr uint8_t TURBIDEZ_FISICA_PIN = 32;
constexpr uint8_t TDS_FISICO_PIN = 35;
constexpr uint8_t TEMPERATURA_PIN = 14;
constexpr size_t NUM_AMOSTRAS_FISICAS = 31;
constexpr unsigned long INTERVALO_AMOSTRAS_FISICAS_MS = 10;

// Pontos confirmados pelo usuário. São tensões calculadas por ADC/4095 * 3,3
// NO GPIO, já após o divisor; não aplicar de novo o fator do divisor.
constexpr float TENSAO_PH7 = 1.57f;
constexpr float TENSAO_PH4 = 1.95f;
constexpr float REFERENCIA_PH7 = 7.0f;
constexpr float REFERENCIA_PH4 = 4.0f;

// Deslocamento final solicitado pelo usuário. Não é nova calibração com padrões.
constexpr float AJUSTE_FINAL_PH = -3.0f;

// Referência do código de teste. Reavaliar se mudar água, posição ou circuito.
constexpr uint16_t ADC_AGUA_REFERENCIA = 2709;

// Convenção de DEMONSTRAÇÃO, não calibração óptica em padrões NTU.
// Referência fornecida pelo usuário na escala ADC*3,3/4095 do firmware.
constexpr float TURBIDEZ_REFERENCIA_V = 0.921f;
constexpr float TURBIDEZ_REFERENCIA_DEMO = 2.5f;
constexpr float TURBIDEZ_GANHO_DEMO = 50.0f; // Unidades demonstrativas por volt.

inline float turbidezDemonstrativa(float volts) {
  if (!std::isfinite(volts) || volts < 0.0f || volts > 3.3f) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const float valor = TURBIDEZ_REFERENCIA_DEMO +
      (TURBIDEZ_REFERENCIA_V - volts) * TURBIDEZ_GANHO_DEMO;
  return valor < 0.0f ? 0.0f : valor;
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

inline float ajustarPhFinal(float phOriginal) {
  const float ajustado = phOriginal + AJUSTE_FINAL_PH;
  return std::isfinite(ajustado) && ajustado >= 0.0f && ajustado <= 14.0f
      ? ajustado : std::numeric_limits<float>::quiet_NaN();
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
