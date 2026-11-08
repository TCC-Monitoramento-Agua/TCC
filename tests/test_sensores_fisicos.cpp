// g++ -std=c++11 -Wall -Wextra -Werror tests/test_sensores_fisicos.cpp -o /tmp/test_sensores_fisicos && /tmp/test_sensores_fisicos
#include <cassert>
#include "../firmware/esp32/include/SensoresFisicos.h"

int main() {
  // Reproduz os dois pontos confirmados sem compensar novamente o divisor.
  assert(std::abs(converterPh(1.57f) - 7.0f) < 0.001f);
  assert(std::abs(converterPh(1.95f) - 4.0f) < 0.001f);
  assert(std::abs(converterPh((1.57f + 1.95f) / 2) - 5.5f) < 0.001f);
  assert(std::abs(converterPh(tensaoEscalaCalibracao(1948)) - 7.0f) < 0.01f);
  assert(std::isnan(converterPh(NAN)));
  assert(std::isnan(converterPh(0.0f)));
  assert(std::isnan(converterPh(3.3f)));
  assert(std::abs(ajustarPhFinal(9.8f) - 6.8f) < 0.001f);
  assert(std::abs(ajustarPhFinal(9.81f) - 6.81f) < 0.001f);
  assert(ajustarPhFinal(3.0f) == 0.0f);
  assert(ajustarPhFinal(14.0f) == 11.0f);
  assert(std::isnan(ajustarPhFinal(2.9f))); // Não inventar zero para resultado negativo.
  assert(std::isnan(ajustarPhFinal(NAN)));
  // Percentual de queda do sinal, sem relação arbitrária com NTU.
  assert(turbidezRelativa(2709) == 0.0f);
  assert(turbidezRelativa(3000) == 0.0f);
  assert(turbidezRelativa(1000, 2000) == 50.0f);
  assert(turbidezRelativa(0) == 100.0f);
  assert(std::isnan(turbidezRelativa(1000, 0)));
  // Escala arbitrária de demonstração, não equivalência com padrões NTU.
  assert(std::abs(turbidezDemonstrativa(1.591f) - 2.5f) < 0.001f);
  assert(std::abs(turbidezDemonstrativa(1.641f) - 5.0f) < 0.001f);
  assert(turbidezDemonstrativa(1.541f) < 0.001f);
  assert(turbidezDemonstrativa(1.7f) > 5.0f);
  assert(turbidezDemonstrativa(1.1f) == 0.0f);
  assert(turbidezDemonstrativa(2.0f) > turbidezDemonstrativa(1.7f));
  assert(std::isnan(turbidezDemonstrativa(NAN)));
  assert(std::isnan(turbidezDemonstrativa(-0.1f)));
  assert(temperaturaFisicaValida(22.5f));
  assert(temperaturaFisicaValida(85.0f)); // Pode ser temperatura real após conversão.
  assert(!temperaturaFisicaValida(-127.0f));
  assert(!temperaturaFisicaValida(126.0f));
  assert(!temperaturaFisicaValida(NAN));
}
