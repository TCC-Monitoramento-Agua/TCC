// Execute na raiz: g++ -std=c++11 tests/test_tds_conversion.cpp -o /tmp/test_tds_conversion && /tmp/test_tds_conversion
#include <cassert>
#include <cmath>
#include "../firmware/esp32/include/ConversaoTds.h"

int main() {
  // Ligação direta: a tensão no GPIO é a tensão de saída do módulo.
  assert(std::abs(tensaoModuloTds(600) - 0.6f) < 0.0001f);
  assert(std::abs(tensaoModuloTds(1200) - 1.2f) < 0.0001f);
  assert(std::abs(tensaoModuloTds(220) - 0.220f) < 0.0001f);
  // Referências numéricas da curva a 25 °C, sem fator de calibração ajustado.
  assert(std::abs(estimarTdsPpm(tensaoModuloTds(1000)) - 367.475f) < 0.01f);
  assert(std::abs(estimarTdsPpm(tensaoModuloTds(2000)) - 879.35f) < 0.01f);
  assert(estimarTdsPpm(0.0f) == 0.0f);
  assert(std::abs(tensaoModuloTds(180) - 0.18f) < 0.0001f);
  assert(std::abs(tensaoModuloTds(360) - 0.36f) < 0.0001f);
  assert(std::abs(estimarTdsCompensado(0.3f, 25.0f) - estimarTdsPpm(0.3f)) < 0.001f);
  assert(estimarTdsCompensado(0.6f, 25.0f) > estimarTdsCompensado(0.3f, 25.0f));
  assert(estimarTdsCompensado(0.3f, 20.0f) > estimarTdsCompensado(0.3f, 25.0f));
  assert(std::isnan(estimarTdsCompensado(0.3f, NAN)));
  assert(std::isnan(estimarTdsCompensado(NAN, 25.0f)));
  assert(std::isnan(estimarTdsCompensado(-0.1f, 25.0f)));
  assert(std::isnan(estimarTdsCompensado(3.3f, 25.0f)));
  assert(std::isnan(estimarTdsCompensado(0.3f, -25.0f)));
  assert(std::isnan(estimarTdsCompensado(2.3f, 25.0f))); // Fora da escala de 1.000 ppm.
  return 0;
}
