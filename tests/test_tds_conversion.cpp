// Execute na raiz: g++ -std=c++11 tests/test_tds_conversion.cpp -o /tmp/test_tds_conversion && /tmp/test_tds_conversion
#include <cassert>
#include <cmath>
#include "../firmware/esp32/include/ConversaoTds.h"

int main() {
  // Com o divisor 10k/15k, 0,6 V no ADC correspondem a 1 V no módulo.
  assert(std::abs(tensaoModuloTds(600) - 1.0f) < 0.0001f);
  assert(std::abs(tensaoModuloTds(1200) - 2.0f) < 0.0001f);
  // Referências numéricas da curva a 25 °C, sem fator de calibração ajustado.
  assert(std::abs(estimarTdsPpm(tensaoModuloTds(600)) - 367.475f) < 0.01f);
  assert(std::abs(estimarTdsPpm(tensaoModuloTds(1200)) - 879.35f) < 0.01f);
  assert(estimarTdsPpm(0.0f) == 0.0f);
  return 0;
}
