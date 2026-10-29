#pragma once
#include <algorithm>
#include <LittleFS.h>
#include <esp_partition.h>
#include <esp_littlefs.h>
#include "FilaCore.h"

class FlashStorage : public ArmazenamentoFila {
  struct Ack { uint64_t sequencia; uint32_t magic; uint32_t crc; };
  struct Configuracao { uint32_t magic; uint32_t capacidade; uint32_t crc; };
  const char* arquivo;
  const char* confirmacao;
  const char* temporario;
  bool migrarAnterior;
  size_t tamanhoArquivo = 0, tamanhoMaximo = 0;
  bool existe(const char* nome) {
    return LittleFS.exists(nome);
  }
  bool configurar(size_t capacidade, bool gravar) {
    Configuracao config = {0x46494c32, static_cast<uint32_t>(capacidade), 0};
    config.crc = crcFila(&config, offsetof(Configuracao, crc));
    if (gravar) {
      File file = LittleFS.open("/fila-v2-config.tmp", "w");
      bool ok = file && file.write(reinterpret_cast<const uint8_t*>(&config), sizeof(config)) == sizeof(config);
      file.flush(); file.close();
      if (!ok || !LittleFS.rename("/fila-v2-config.tmp", "/fila-v2-config.bin")) return false;
    }
    File file = LittleFS.open("/fila-v2-config.bin", "r");
    Configuracao teste;
    bool ok = file && file.size() == sizeof(teste) &&
        file.read(reinterpret_cast<uint8_t*>(&teste), sizeof(teste)) == sizeof(teste) &&
        !memcmp(&config, &teste, sizeof(config));
    file.close(); return ok;
  }
  bool criarArquivoVazio() {
    File file = LittleFS.open(arquivo, "w");
    if (!file) return false;
    file.flush(); file.close();
    tamanhoArquivo = 0;
    return true;
  }
  bool prepararArquivo() {
    if (!migrarAnterior) {
      // Um arquivo legado sem confirmação não pode ser considerado vazio.
      if (existe(arquivo)) return false;
      return criarArquivoVazio() && gravarConfirmacao(0);
    }
    const bool dadosAntigos = existe("/fila-local.bin");
    const bool ackAntigo = existe("/fila-local-ack.bin");
    if (dadosAntigos != ackAntigo) return false;
    if (!dadosAntigos) {
      // Só uma criação interrompida ainda vazia pode ser refeita sem legado.
      File existente = LittleFS.open(arquivo, "r");
      bool vazio = !existente || existente.size() == 0;
      existente.close();
      return vazio && criarArquivoVazio() && gravarConfirmacao(0);
    }

    FlashStorage anterior("/fila-local.bin", "/fila-local-ack.bin", "/fila-local-ack.tmp", false);
    FilaPersistente filaAnterior(anterior, 20);
    if (!anterior.montar(20) || !filaAnterior.iniciar()) return false;
    // Sem ACK v2 ainda não houve coleta no destino: uma cópia interrompida
    // pode ser refeita. O legado permanece intacto inclusive após concluir.
    if (!criarArquivoVazio() || !filaAnterior.copiarPendentesPara(*this, tamanhoMaximo / sizeof(RegistroFlash))) return false;
    Serial.printf("[FILA] Atualização concluída | pendências preservadas=%u\r\n", unsigned(filaAnterior.pendentes()));
    return true;
  }
 public:
  FlashStorage(const char* dados = "/fila-v2.bin", const char* ack = "/fila-v2-ack.bin",
               const char* tmp = "/fila-v2-ack.tmp", bool migrar = true)
      : arquivo(dados), confirmacao(ack), temporario(tmp), migrarAnterior(migrar) {}
  bool montar(size_t capacidade) {
    // Verifica se a partição está apagada antes de montar: flash nova não é corrupção.
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!part) return false;
    uint8_t bytes[1024];
    bool apagada = true;
    for (size_t pos = 0; pos < part->size && apagada; pos += sizeof(bytes)) {
      size_t n = std::min<size_t>(sizeof(bytes), part->size - pos);
      if (esp_partition_read(part, pos, bytes, n) != ESP_OK) return false;
      for (size_t i = 0; i < n; ++i) if (bytes[i] != 0xff) { apagada = false; break; }
    }
    if (apagada) {
      Serial.println("[LOCAL] FLASH VAZIA: nova execução ou apagamento; sem pendências para recuperar");
      if (esp_littlefs_format("spiffs") != ESP_OK) return false;
    }
    // Dados não vazios nunca são formatados em caso de erro.
    if (!LittleFS.begin(false, "/littlefs", 10, "spiffs")) return false;
    if (!capacidade || capacidade > LittleFS.totalBytes() / sizeof(RegistroFlash)) return false;
    tamanhoMaximo = capacidade * sizeof(RegistroFlash);
    const bool configurada = migrarAnterior && existe("/fila-v2-config.bin");
    if (configurada && (!configurar(capacidade, false) || !existe(confirmacao) || !existe(arquivo))) return false;
    if (!existe(confirmacao) && !prepararArquivo()) return false;
    // ACK existente sem arquivo de dados é uma inconsistência, nunca recriar.
    if (!existe(arquivo)) return false;
    File file = LittleFS.open(arquivo, "r");
    tamanhoArquivo = file ? file.size() : 0;
    bool ok = file && tamanhoArquivo <= tamanhoMaximo && tamanhoArquivo % sizeof(RegistroFlash) == 0;
    file.close();
    uint64_t seq;
    if (!ok || !lerConfirmacao(seq)) return false;
    return !migrarAnterior || configurada || configurar(capacidade, true);
  }
  bool ler(size_t slot, RegistroFlash& r) override {
    const size_t pos = slot * sizeof(r);
    if (pos >= tamanhoMaximo) return false;
    // Slots ainda não criados são vazios; não precisam de escrita nem leitura de flash.
    if (pos >= tamanhoArquivo) { memset(&r, 0, sizeof(r)); return true; }
    File file = LittleFS.open(arquivo, "r");
    bool ok = file && file.size() == tamanhoArquivo && file.seek(pos) && file.read(reinterpret_cast<uint8_t*>(&r), sizeof(r)) == sizeof(r);
    file.close(); return ok;
  }
  bool gravar(size_t slot, const RegistroFlash& r) override {
    const size_t pos = slot * sizeof(r);
    if (pos >= tamanhoMaximo || pos > tamanhoArquivo) return false;
    File file = LittleFS.open(arquivo, "r+");
    bool ok = file && file.size() == tamanhoArquivo && file.seek(pos) && file.write(reinterpret_cast<const uint8_t*>(&r), sizeof(r)) == sizeof(r);
    file.flush();
    if (ok) tamanhoArquivo = file.size();
    file.close(); return ok;
  }
  bool lerConfirmacao(uint64_t& seq) override {
    Ack ack;
    File file = LittleFS.open(confirmacao, "r");
    bool ok = file && file.size() == sizeof(ack) && file.read(reinterpret_cast<uint8_t*>(&ack), sizeof(ack)) == sizeof(ack);
    file.close();
    if (!ok || ack.magic != 0x41434b31 || ack.crc != crcFila(&ack, offsetof(Ack, crc))) return false;
    // Antes da primeira volta do anel, um ACK além dos slots presentes indica truncamento.
    if (tamanhoArquivo < tamanhoMaximo && ack.sequencia > tamanhoArquivo / sizeof(RegistroFlash)) return false;
    seq = ack.sequencia; return true;
  }
  bool gravarConfirmacao(uint64_t seq) override {
    Ack ack = {seq, 0x41434b31, 0};
    ack.crc = crcFila(&ack, offsetof(Ack, crc));
    File file = LittleFS.open(temporario, "w");
    bool ok = file && file.write(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack)) == sizeof(ack);
    file.flush(); file.close();
    if (!ok) return false;
    file = LittleFS.open(temporario, "r");
    Ack teste;
    ok = file && file.read(reinterpret_cast<uint8_t*>(&teste), sizeof(teste)) == sizeof(teste) && !memcmp(&ack, &teste, sizeof(ack));
    file.close();
    // LittleFS oferece rename atômico; se houver queda, o ACK anterior permite reenvio.
    return ok && LittleFS.rename(temporario, confirmacao);
  }
};
