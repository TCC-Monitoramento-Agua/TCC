// g++ -std=c++11 -Wall -Wextra -Werror -Itests/fakes tests/test_fila_1000.cpp -o /tmp/test_fila_1000 && /tmp/test_fila_1000
#include <cassert>
#include <cstdio>
#include "../firmware/esp32/include/FlashStorage.h"

static void salvar(FilaPersistente& fila, unsigned n) {
  char json[128];
  int size = snprintf(json, sizeof(json), "{\"leitura_id\":\"uuid-%u\",\"data_hora\":\"2026-10-29 12:00:00\"}", n);
  assert(fila.salvar(json, size));
}

static void verificarFrente(FilaPersistente& fila, unsigned n) {
  RegistroFlash r;
  assert(fila.frente(r));
  char uuid[40]; snprintf(uuid, sizeof(uuid), "\"uuid-%u\"", n);
  assert(strstr(r.dados, uuid));
  assert(strstr(r.dados, "2026-10-29 12:00:00"));
}

int main() {
  // Enche 1000 slots, recusa o 1001º sem sobrescrever, recupera e dá a volta.
  {
    FlashStorage storage;
    FilaPersistente fila(storage, 1000);
    assert(storage.montar(1000) && fila.iniciar());
    for (unsigned i = 1; i <= 1000; ++i) salvar(fila, i);
    assert(estadoFS.arquivos.at("/fila-v2.bin").size() == 512000);
    assert(fila.cheia() && !fila.salvar("novo", 4));
  }
  {
    FlashStorage storage;
    FilaPersistente fila(storage, 1000);
    assert(storage.montar(1000) && fila.iniciar() && fila.pendentes() == 1000);
    for (unsigned i = 1; i <= 700; ++i) {
      verificarFrente(fila, i); assert(fila.confirmar(i));
    }
    for (unsigned i = 1001; i <= 1700; ++i) salvar(fila, i);
  }
  {
    FlashStorage storage;
    FilaPersistente fila(storage, 1000);
    assert(storage.montar(1000) && fila.iniciar() && fila.pendentes() == 1000);
    for (unsigned i = 701; i <= 1700; ++i) {
      verificarFrente(fila, i); assert(fila.confirmar(i));
    }
    assert(fila.pendentes() == 0);
    // A capacidade persistida impede reinterpretar o anel com outro tamanho.
    FlashStorage diferente; assert(!diferente.montar(500));
  }

  // Prepara legado de 20 slots já reutilizados: UUIDs 76 a 95 pendentes.
  estadoFS = EstadoFS();
  {
    FlashStorage antigo("/fila-local.bin", "/fila-local-ack.bin", "/fila-local-ack.tmp", false);
    FilaPersistente fila(antigo, 20);
    assert(antigo.montar(20) && fila.iniciar());
    for (unsigned i = 1; i <= 95; ++i) {
      if (fila.cheia()) assert(fila.confirmar(i - 20));
      salvar(fila, i);
    }
  }
  const auto legado = estadoFS.arquivos;
  // Interrupções em cada escrita da cópia/ACK/configuração permitem nova tentativa.
  for (int limite = 0; limite <= 23; ++limite) {
    estadoFS = EstadoFS(); estadoFS.arquivos = legado;
    estadoFS.escritasRestantes = limite;
    FlashStorage tentativa; tentativa.montar(1000);
    estadoFS.escritasRestantes = -1;
    FlashStorage novo;
    FilaPersistente fila(novo, 1000);
    assert(novo.montar(1000) && fila.iniciar() && fila.pendentes() == 20);
    assert(estadoFS.arquivos.at("/fila-local.bin") == legado.at("/fila-local.bin"));
    assert(estadoFS.arquivos.at("/fila-local-ack.bin") == legado.at("/fila-local-ack.bin"));
    for (unsigned i = 76; i <= 95; ++i) {
      verificarFrente(fila, i); assert(fila.confirmar(i - 75));
    }
    salvar(fila, 96);
    // Reiniciar não importa de novo as pendências antigas já transferidas.
    FlashStorage reinicio; FilaPersistente recuperada(reinicio, 1000);
    assert(reinicio.montar(1000) && recuperada.iniciar() && recuperada.pendentes() == 1);
    verificarFrente(recuperada, 96);
  }
  // Falha no rename não publica o ACK, e a tentativa posterior preserva o legado.
  estadoFS = EstadoFS(); estadoFS.arquivos = legado;
  estadoFS.falharRename = true;
  FlashStorage falha; assert(!falha.montar(1000));
  estadoFS.falharRename = false;
  FlashStorage volta; FilaPersistente fila(volta, 1000);
  assert(volta.montar(1000) && fila.iniciar() && fila.pendentes() == 20);

  // Corrupção/ausência de arquivos não pode recriar dados nem recuperar legado velho.
  estadoFS.arquivos.erase("/fila-v2-ack.bin");
  const auto antes = estadoFS.arquivos;
  FlashStorage semAck; assert(!semAck.montar(1000));
  assert(estadoFS.arquivos == antes);
  estadoFS = EstadoFS(); estadoFS.arquivos = legado;
  estadoFS.arquivos.at("/fila-local.bin")[0] ^= 0xff;
  const auto corrompido = estadoFS.arquivos;
  FlashStorage invalido; assert(!invalido.montar(1000));
  assert(estadoFS.arquivos == corrompido);
  puts("Fila 1000: capacidade, FIFO, reinício, migração e falhas verificados.");
}
