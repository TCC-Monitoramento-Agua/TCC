// g++ -std=c++11 -Wall -Wextra -Werror tests/test_log_diagnostico.cpp -o /tmp/test_log_diagnostico && /tmp/test_log_diagnostico
#include <cassert>
#include "../firmware/esp32/include/LogDiagnostico.h"

template <typename Buffer>
void escrever(Buffer& log, const char* texto, uint32_t ms) {
  log.escrever(reinterpret_cast<const uint8_t*>(texto), strlen(texto), ms);
}

int main() {
  LogDiagnostico<3, 32> log;
  LogDiagnostico<3, 32>::Linha linha;
  assert(log.primeiro() == 0 && log.ultimo() == 0);
  escrever(log, "[PH] ", 10);
  escrever(log, "7.00\r\n", 20);
  assert(log.obter(1, linha));
  assert(!strcmp(linha.texto, "[PH] 7.00") && linha.ms == 20);
  escrever(log, "\n\r\n", 30); // Linhas vazias não ocupam o buffer.
  assert(log.ultimo() == 1);
  escrever(log, "temperatura\nturbidez\nenvio\n", 40);
  assert(log.primeiro() == 2 && log.ultimo() == 4);
  assert(!log.obter(1, linha));
  assert(log.obter(4, linha) && !strcmp(linha.texto, "envio"));
  escrever(log, "uma linha muito longa que ultrapassa os limites da memória\n", 50);
  assert(log.obter(5, linha));
  assert(strlen(linha.texto) < 32);
  assert(!strcmp(linha.texto + strlen(linha.texto) - 3, "..."));
  escrever(log, "nova\n", 60);
  assert(log.obter(6, linha) && !strcmp(linha.texto, "nova"));

  // Truncamento não deixa metade de um caractere UTF-8 na linha.
  LogDiagnostico<2, 10> utf8;
  LogDiagnostico<2, 10>::Linha curta;
  escrever(utf8, "abcdeé123456789\n", 70);
  assert(utf8.obter(1, curta));
  assert(!strcmp(curta.texto, "abcde..."));
  // Cada instância começa uma sessão de logs independente do boot anterior.
  LogDiagnostico<3, 32> reiniciado;
  assert(reiniciado.ultimo() == 0);
}
