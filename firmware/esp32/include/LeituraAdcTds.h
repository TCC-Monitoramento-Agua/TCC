#pragma once
#include <algorithm>
#include "SensoresFisicos.h"

struct LeituraAdcTds {
  uint16_t bruto;
  uint32_t milivolts;
  bool faixaAmpla;
};

// Primeiro usa 0 dB; se estiver perto do limite, repete em 6 dB.
// O adaptador configura somente o GPIO do TDS e converte mV na faixa selecionada.
template <typename Adc>
LeituraAdcTds adquirirTds(Adc& adc) {
  uint16_t brutos[NUM_AMOSTRAS_FISICAS];
  uint32_t milivolts[NUM_AMOSTRAS_FISICAS];
  LeituraAdcTds leitura{};
  for (unsigned faixa = 0; faixa < 2; ++faixa) {
    adc.configurar(faixa != 0);
    adc.aguardar(2);
    // Descarta o primeiro par após configurar a atenuação.
    adc.lerBruto();
    adc.lerMilivolts();
    for (size_t i = 0; i < NUM_AMOSTRAS_FISICAS; ++i) {
      brutos[i] = adc.lerBruto();
      milivolts[i] = adc.lerMilivolts();
      if (i + 1 < NUM_AMOSTRAS_FISICAS) adc.aguardar(INTERVALO_AMOSTRAS_FISICAS_MS);
    }
    std::sort(brutos, brutos + NUM_AMOSTRAS_FISICAS);
    std::sort(milivolts, milivolts + NUM_AMOSTRAS_FISICAS);
    leitura = {brutos[NUM_AMOSTRAS_FISICAS / 2], milivolts[NUM_AMOSTRAS_FISICAS / 2], faixa != 0};
    if (leitura.bruto < 3600) break;
  }
  return leitura;
}
