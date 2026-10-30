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
  // Percentual de queda do sinal, sem relação arbitrária com NTU.
  assert(turbidezRelativa(2709) == 0.0f);
  assert(turbidezRelativa(3000) == 0.0f);
  assert(turbidezRelativa(1000, 2000) == 50.0f);
  assert(turbidezRelativa(0) == 100.0f);
  assert(std::isnan(turbidezRelativa(1000, 0)));
  assert(temperaturaFisicaValida(22.5f));
  assert(temperaturaFisicaValida(85.0f)); // Pode ser temperatura real após conversão.
  assert(!temperaturaFisicaValida(-127.0f));
  assert(!temperaturaFisicaValida(126.0f));
  assert(!temperaturaFisicaValida(NAN));
}
