#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct RegistroFlash {
  uint64_t sequencia;
  uint16_t tamanho;
  uint16_t reservado;
  char dados[496];
  uint32_t crc;
};
static_assert(sizeof(RegistroFlash) == 512, "Registro deve ocupar 512 bytes");
inline uint32_t crcFila(const void* dados, size_t tamanho) {
  uint32_t crc = 0xffffffff;
  const uint8_t* bytes = static_cast<const uint8_t*>(dados);
  for (size_t i = 0; i < tamanho; ++i) {
    crc ^= bytes[i];
    for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
class ArmazenamentoFila {
 public:
  virtual ~ArmazenamentoFila() {}
  virtual bool ler(size_t slot, RegistroFlash& registro) = 0;
  virtual bool gravar(size_t slot, const RegistroFlash& registro) = 0;
  virtual bool lerConfirmacao(uint64_t& sequencia) = 0;
  virtual bool gravarConfirmacao(uint64_t sequencia) = 0;
};
class FilaPersistente {
  ArmazenamentoFila& storage;
  size_t capacidade;
  uint64_t confirmado = 0, ultimo = 0;
  bool pronta = false;
  bool valido(const RegistroFlash& r) const {
    return r.sequencia && r.tamanho && r.tamanho < sizeof(r.dados) &&
      r.dados[r.tamanho] == '\0' && r.crc == crcFila(&r, offsetof(RegistroFlash, crc));
  }
 public:
  FilaPersistente(ArmazenamentoFila& s, size_t n) : storage(s), capacidade(n) {}
  bool iniciar() {
    pronta = false;
    if (!storage.lerConfirmacao(confirmado)) return false;
    ultimo = confirmado;
    for (size_t i = 0; i < capacidade; ++i) {
      RegistroFlash r;
      if (!storage.ler(i, r)) return false;
      if (!r.sequencia) {
        RegistroFlash vazio = {};
        if (memcmp(&r, &vazio, sizeof(r))) return false;
        continue;
      }
      if (!valido(r) || (r.sequencia - 1) % capacidade != i) return false;
      if (r.sequencia > ultimo) ultimo = r.sequencia;
    }
    if (ultimo - confirmado > capacidade) return false;
    for (uint64_t seq = confirmado + 1; seq <= ultimo; ++seq) {
      RegistroFlash r;
      if (!storage.ler((seq - 1) % capacidade, r) || !valido(r) || r.sequencia != seq) return false;
    }
    pronta = true;
    return true;
  }
  size_t pendentes() const { return static_cast<size_t>(ultimo - confirmado); }
  bool saudavel() const { return pronta; }
  bool cheia() const { return pendentes() >= capacidade; }
  bool salvar(const char* dados, size_t tamanho) {
    if (!pronta || cheia() || !tamanho || tamanho >= sizeof(RegistroFlash::dados)) return false;
    RegistroFlash r = {};
    r.sequencia = ultimo + 1;
    r.tamanho = tamanho;
    memcpy(r.dados, dados, tamanho);
    r.crc = crcFila(&r, offsetof(RegistroFlash, crc));
    size_t slot = (r.sequencia - 1) % capacidade;
    RegistroFlash teste;
    if (!storage.gravar(slot, r) || !storage.ler(slot, teste) || memcmp(&r, &teste, sizeof(r))) {
      pronta = false;
      return false;
    }
    ultimo = r.sequencia;
    return true;
  }
  bool frente(RegistroFlash& r) {
    if (!pronta || !pendentes()) return false;
    if (!storage.ler(confirmado % capacidade, r) || !valido(r) || r.sequencia != confirmado + 1) {
      pronta = false;
      return false;
    }
    return true;
  }
  bool atualizarFrente(const char* dados, size_t tamanho) {
    RegistroFlash r;
    if (!frente(r) || !tamanho || tamanho >= sizeof(r.dados)) return false;
    memset(r.dados, 0, sizeof(r.dados));
    memcpy(r.dados, dados, tamanho); r.tamanho = tamanho;
    r.crc = crcFila(&r, offsetof(RegistroFlash, crc));
    RegistroFlash teste;
    size_t slot = (r.sequencia - 1) % capacidade;
    if (!storage.gravar(slot, r) || !storage.ler(slot, teste) || memcmp(&r, &teste, sizeof(r))) {
      pronta = false; return false;
    }
    return true;
  }
  bool confirmar(uint64_t sequencia) {
    if (!pronta || sequencia != confirmado + 1 || !pendentes()) return false;
    if (!storage.gravarConfirmacao(sequencia)) { pronta = false; return false; }
    confirmado = sequencia;
    return true;
  }
};
