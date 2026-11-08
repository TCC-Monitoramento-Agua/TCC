// Na raiz: g++ -std=c++11 tests/test_controle_horario.cpp -o /tmp/test_controle_horario && /tmp/test_controle_horario
#include <cassert>
#include <vector>
#include "../firmware/esp32/include/ControleHorario.h"
#include "../firmware/esp32/include/FilaCore.h"

class Memoria : public ArmazenamentoFila {
 public:
  std::vector<RegistroFlash> registros{2};
  uint64_t ack = 0;
  bool falharAck = false;
  bool ler(size_t slot, RegistroFlash& r) override { r = registros.at(slot); return true; }
  bool gravar(size_t slot, const RegistroFlash& r) override { registros.at(slot) = r; return true; }
  bool lerConfirmacao(uint64_t& seq) override { seq = ack; return true; }
  bool gravarConfirmacao(uint64_t seq) override {
    if (falharAck) return false;
    ack = seq; return true;
  }
};

int main() {
  const time_t horaValida = 1792627200;
  const char* data = "2026-10-21 20:00:00";
  ControleHorarioColeta relogio;
  // No boot não se coleta nem se guarda, mesmo se o RTC reteve um valor plausível.
  assert(!relogio.podeColetar(0));
  assert(!relogio.podeColetar(horaValida));
  assert(!relogio.podeGuardar(horaValida, data));
  relogio.marcarSincronizado();
  assert(relogio.podeColetar(horaValida));
  assert(relogio.podeGuardar(horaValida, data));
  // Não aceita documentos sem horário nem relógio inválido entre coleta e gravação.
  assert(!relogio.podeGuardar(horaValida, nullptr));
  assert(!relogio.podeGuardar(horaValida, ""));
  assert(!relogio.podeGuardar(0, data));
  // Reiniciar exige uma nova sincronização; a anterior não atravessa boots.
  ControleHorarioColeta reiniciado;
  assert(!reiniciado.podeGuardar(horaValida, data));
  reiniciado.marcarSincronizado();
  assert(reiniciado.podeGuardar(horaValida, data));

  // Descarta apenas a frente sem horário; mantém a leitura datada após reiniciar.
  Memoria memoria;
  FilaPersistente fila(memoria, 2);
  assert(fila.iniciar());
  assert(fila.salvar("sem-horario", 11));
  assert(fila.salvar(data, 19));
  assert(!fila.descartarSemHorario(2, "")); // Não pula registros fora de ordem.
  assert(!fila.descartarSemHorario(1, data)); // Não descarta um registro com data.
  memoria.falharAck = true;
  assert(!fila.descartarSemHorario(1, ""));
  assert(memoria.ack == 0); // Se a confirmação local falhar, nada é removido.
  memoria.falharAck = false;
  FilaPersistente recuperada(memoria, 2);
  assert(recuperada.iniciar());
  assert(recuperada.descartarSemHorario(1, ""));
  FilaPersistente aposReinicio(memoria, 2);
  assert(aposReinicio.iniciar());
  assert(aposReinicio.pendentes() == 1);
  RegistroFlash frente;
  assert(aposReinicio.frente(frente));
  assert(!strcmp(frente.dados, data));
  assert(!aposReinicio.descartarSemHorario(frente.sequencia, data));
}
