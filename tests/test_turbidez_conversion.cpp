// Na raiz: g++ -std=c++11 tests/test_turbidez_conversion.cpp -o /tmp/test_turbidez_conversion && /tmp/test_turbidez_conversion
#include <cassert>
#include <cmath>
#include "../firmware/esp32/include/ConversaoTurbidez.h"

int main() {
  // Saída de 4,5 V no módulo fica em 2,7 V após o divisor 10k/15k.
  assert(std::abs(tensaoModuloTurbidez(2700) - 4.5f) < 0.0001f);
  assert(std::abs(tensaoModuloTurbidez(1500) - 2.5f) < 0.0001f);
  assert(tensaoModuloTurbidez(0) == 0.0f);
  // Âncoras numéricas do ajuste de referência; usa a tensão ANTES do divisor.
  assert(std::abs(estimarTurbidezNtu(tensaoModuloTurbidez(2400)) - 689.9f) < 0.02f);
  assert(std::abs(estimarTurbidezNtu(3.0f) - 2790.4f) < 0.02f);
  assert(std::abs(estimarTurbidezNtu(2.5f) - 3000.35f) < 0.02f);
  assert(std::abs(estimarTurbidezNtu(4.2f) - 0.904f) < 0.02f);
  assert(estimarTurbidezNtu(4.5f) == 0.0f);
  // Sinal ausente/fora da curva não vira 0 NTU por extrapolação do polinômio.
  assert(std::isnan(estimarTurbidezNtu(0.0f)));
  assert(std::isnan(estimarTurbidezNtu(2.49f)));
  assert(std::isnan(estimarTurbidezNtu(4.51f)));
  assert(std::isnan(estimarTurbidezNtu(NAN)));
  assert(std::isnan(estimarTurbidezNtu(INFINITY)));
}
