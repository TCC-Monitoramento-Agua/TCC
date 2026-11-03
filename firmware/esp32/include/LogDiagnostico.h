#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Buffer circular em RAM. O chamador sincroniza acesso entre tarefas.
template <size_t Capacidade = 80, size_t Comprimento = 256>
class LogDiagnostico {
 public:
  struct Linha { uint32_t id = 0, ms = 0; char texto[Comprimento] = {}; };
 private:
  static_assert(Capacidade > 0 && Comprimento >= 8, "Buffer de log inválido");
  Linha linhas[Capacidade];
  char parcial[Comprimento] = {};
  size_t tamanho = 0, quantidade = 0;
  uint32_t proximo = 1;
  bool truncada = false;

  void concluir(uint32_t ms) {
    if (!tamanho) return;
    Linha& linha = linhas[(proximo - 1) % Capacidade];
    linha.id = proximo++;
    linha.ms = ms;
    size_t n = tamanho;
    if (truncada) {
      n = Comprimento - 4;
      // Não cortar no meio de um caractere UTF-8.
      while (n && (static_cast<uint8_t>(parcial[n]) & 0xc0) == 0x80) --n;
    }
    memcpy(linha.texto, parcial, n);
    if (truncada) { memcpy(linha.texto + n, "...", 3); n += 3; }
    linha.texto[n] = '\0';
    if (quantidade < Capacidade) ++quantidade;
    tamanho = 0; truncada = false;
  }
 public:
  void escrever(const uint8_t* bytes, size_t n, uint32_t ms) {
    for (size_t i = 0; i < n; ++i) {
      const char c = static_cast<char>(bytes[i]);
      if (c == '\n') concluir(ms);
      else if (c != '\r') {
        if (tamanho < Comprimento - 1) parcial[tamanho++] = c;
        else truncada = true;
      }
    }
  }
  uint32_t primeiro() const { return quantidade ? proximo - quantidade : 0; }
  uint32_t ultimo() const { return quantidade ? proximo - 1 : 0; }
  bool obter(uint32_t id, Linha& destino) const {
    if (!id || id < primeiro() || id > ultimo()) return false;
    destino = linhas[(id - 1) % Capacidade];
    return destino.id == id;
  }
};
