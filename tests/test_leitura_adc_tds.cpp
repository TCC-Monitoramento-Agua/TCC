#include <cassert>
#include <vector>
#include "../firmware/esp32/include/LeituraAdcTds.h"
#include "../firmware/esp32/include/ConversaoTds.h"

struct AdcSimulado {
  std::vector<bool> configuracoes;
  uint16_t baixo, alto;
  uint32_t mvBaixo, mvAlto;
  bool ampla = false;
  unsigned brutos = 0, mvs = 0, esperas = 0;
  AdcSimulado(uint16_t b, uint16_t a, uint32_t vb, uint32_t va)
      : baixo(b), alto(a), mvBaixo(vb), mvAlto(va) {}
  void configurar(bool a) {
    ampla = a; configuracoes.push_back(a); brutos = mvs = 0;
  }
  // Primeiro par transitório e um outlier entre as amostras: não podem decidir a faixa.
  uint16_t lerBruto() { return ++brutos == 1 || brutos == 9 ? 4095 : (ampla ? alto : baixo); }
  uint32_t lerMilivolts() { return ++mvs == 1 || mvs == 9 ? 9999 : (ampla ? mvAlto : mvBaixo); }
  void aguardar(unsigned long) { ++esperas; }
};

int main() {
  AdcSimulado baixo(400, 100, 90, 90);
  const auto l = adquirirTds(baixo);
  assert(!l.faixaAmpla && l.bruto == 400 && l.milivolts == 90);
  assert(baixo.configuracoes == std::vector<bool>{false});
  assert(baixo.brutos == 32 && baixo.mvs == 32);
  assert(baixo.esperas == 31);
  // A faixa baixa fornece mV reais ao divisor/curva, sem reutilizar ADC*3,3/4095.
  assert(std::isfinite(estimarTdsCompensado(tensaoModuloTds(l.milivolts), 25.0f)));

  AdcSimulado alto(3900, 2200, 900, 1000);
  const auto h = adquirirTds(alto);
  assert(h.faixaAmpla && h.bruto == 2200 && h.milivolts == 1000);
  assert((alto.configuracoes == std::vector<bool>{false, true}));
  assert(alto.esperas == 62);
  // Próxima aquisição começa de novo em 0 dB, podendo detectar uma amostra mais diluída.
  alto.baixo = 500; alto.mvBaixo = 110;
  const auto seguinte = adquirirTds(alto);
  assert(!seguinte.faixaAmpla && seguinte.milivolts == 110);

  AdcSimulado limite(3600, 1700, 850, 850);
  assert(adquirirTds(limite).faixaAmpla);
  AdcSimulado abaixo(3599, 1700, 849, 849);
  assert(!adquirirTds(abaixo).faixaAmpla);
  // Não transforma ausência de sinal nem saturação em uma leitura válida.
  AdcSimulado zero(0, 0, 0, 0);
  assert(adquirirTds(zero).bruto == 0);
  AdcSimulado saturado(4095, 4095, 950, 1750);
  const auto s = adquirirTds(saturado);
  assert(s.faixaAmpla && s.bruto == 4095);
}
