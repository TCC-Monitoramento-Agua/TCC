#include <cassert>
#include <iostream>
#include "FlashStorage.h"
int main() {
  const size_t n = 4;
  FlashStorage storage;
  assert(storage.montar(n));
  assert(LittleFS.files.at("/fila.bin").empty());
  FilaPersistente queue(storage, n);
  assert(queue.iniciar());
  for (size_t i=0; i<n; ++i) {
    assert(queue.salvar("{}", 2));
    assert(LittleFS.files.at("/fila.bin").size() == (i+1)*sizeof(RegistroFlash));
  }
  assert(!queue.salvar("{}", 2));
  FlashStorage restarted;
  assert(restarted.montar(n));
  FilaPersistente recovered(restarted, n);
  assert(recovered.iniciar() && recovered.pendentes() == n);
  for (uint64_t seq=1; seq<=n; ++seq) assert(recovered.confirmar(seq));
  assert(recovered.salvar("new", 3));
  assert(LittleFS.files.at("/fila.bin").size() == n*sizeof(RegistroFlash));
  FlashStorage wrapped;
  assert(wrapped.montar(n));
  FilaPersistente afterWrap(wrapped, n);
  assert(afterWrap.iniciar() && afterWrap.pendentes() == 1);
  RegistroFlash record;
  assert(afterWrap.frente(record) && record.sequencia == 5);
  auto saved = LittleFS.files;
  LittleFS.files["/fila.bin"].resize(sizeof(RegistroFlash));
  FlashStorage truncated;
  assert(truncated.montar(n));
  FilaPersistente broken(truncated, n);
  assert(!broken.iniciar()); // ACK 4 não pode existir em arquivo com apenas um slot.
  LittleFS.files = saved;
  LittleFS.files["/fila.bin"].pop_back();
  FlashStorage partial;
  assert(!partial.montar(n));
  LittleFS.files.clear();
  LittleFS.files["/fila.bin"].resize(n*sizeof(RegistroFlash), 0);
  FlashStorage legacy;
  assert(legacy.montar(n));
  assert(legacy.gravarConfirmacao(0));
  FilaPersistente oldQueue(legacy,n);
  assert(oldQueue.iniciar() && oldQueue.salvar("{}",2));
  std::cout << "PASS: flash incremental, reboot, anel, truncamento e arquivo legado\n";
}
