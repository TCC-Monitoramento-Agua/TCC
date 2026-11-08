#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct SerialFake {
  void println(const char*) {}
  template <typename... Args> void printf(const char*, Args...) {}
};
static SerialFake Serial;

struct EstadoFS {
  std::map<std::string, std::vector<uint8_t>> arquivos;
  int escritasRestantes = -1;
  bool falharRename = false;
};
static EstadoFS estadoFS;

class File {
  std::string nome;
  size_t pos = 0;
  bool aberta = false, editavel = false;
 public:
  File() = default;
  File(const char* n, const char* modo) : nome(n) {
    editavel = strcmp(modo, "r") != 0;
    if (!strcmp(modo, "w")) estadoFS.arquivos[nome].clear();
    aberta = estadoFS.arquivos.count(nome) != 0;
  }
  explicit operator bool() const { return aberta; }
  size_t size() const { return aberta ? estadoFS.arquivos.at(nome).size() : 0; }
  bool seek(size_t p) { pos = p; return aberta; }
  size_t read(uint8_t* bytes, size_t n) {
    if (!aberta) return 0;
    const auto& dados = estadoFS.arquivos.at(nome);
    n = std::min(n, dados.size() - std::min(pos, dados.size()));
    if (n) memcpy(bytes, dados.data() + pos, n);
    pos += n; return n;
  }
  size_t write(const uint8_t* bytes, size_t n) {
    if (!aberta || !editavel) return 0;
    if (estadoFS.escritasRestantes == 0) n /= 2; // Escrita interrompida.
    else if (estadoFS.escritasRestantes > 0) --estadoFS.escritasRestantes;
    auto& dados = estadoFS.arquivos[nome];
    if (dados.size() < pos + n) dados.resize(pos + n);
    memcpy(dados.data() + pos, bytes, n); pos += n; return n;
  }
  void flush() {}
  void close() { aberta = false; }
};

struct LittleFSFake {
  bool begin(bool, const char*, int, const char*) { return true; }
  bool exists(const char* nome) { return estadoFS.arquivos.count(nome) != 0; }
  File open(const char* nome, const char* modo) { return File(nome, modo); }
  size_t totalBytes() { return 0x160000; }
  bool rename(const char* de, const char* para) {
    if (estadoFS.falharRename || !exists(de)) return false;
    estadoFS.arquivos[para] = estadoFS.arquivos.at(de);
    estadoFS.arquivos.erase(de); return true;
  }
};
static LittleFSFake LittleFS;
