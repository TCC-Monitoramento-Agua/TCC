#pragma once
#include <atomic>
#include <time.h>

class ControleHorarioColeta {
  std::atomic<bool> sincronizado{false};
 public:
  // Notificado pelo SNTP após acertar o relógio, em cada inicialização da placa.
  void marcarSincronizado() { sincronizado.store(true); }
  bool podeColetar(time_t agora) const {
    return sincronizado.load() && agora >= 1000000000;
  }
  bool podeGuardar(time_t agora, const char* dataHora) const {
    return podeColetar(agora) && dataHora && dataHora[0] != '\0';
  }
};
