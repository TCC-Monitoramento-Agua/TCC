#pragma once
#include <LittleFS.h>
#include <esp_partition.h>
#include "FilaCore.h"

class FlashStorage : public ArmazenamentoFila {
  struct Ack { uint64_t sequencia; uint32_t magic; uint32_t crc; };
  const char* arquivo = "/fila.bin";
  size_t tamanhoArquivo = 0, tamanhoMaximo = 0;
 public:
  bool montar(size_t capacidade) {
    if (!LittleFS.begin(false, "/littlefs", 10, "storage")) {
      // Só formata uma partição totalmente apagada, nunca uma fila corrompida.
      const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
      if (!part) return false;
      uint8_t bytes[1024];
      for (size_t pos = 0; pos < part->size; pos += sizeof(bytes)) {
        size_t n = min(sizeof(bytes), part->size - pos);
        if (esp_partition_read(part, pos, bytes, n) != ESP_OK) return false;
        for (size_t i = 0; i < n; ++i) if (bytes[i] != 0xff) return false;
      }
      if (!LittleFS.format() || !LittleFS.begin(false, "/littlefs", 10, "storage")) return false;
    }
    tamanhoMaximo = capacidade * sizeof(RegistroFlash);
    if (!LittleFS.exists(arquivo)) {
      Serial.println("[FILA] Criando arquivo vazio; os slots serão gravados conforme a coleta");
      File file = LittleFS.open(arquivo, "w");
      if (!file) return false;
      file.flush(); file.close();
      if (!gravarConfirmacao(0)) return false;
    }
    File file = LittleFS.open(arquivo, "r");
    tamanhoArquivo = file ? file.size() : 0;
    bool ok = file && tamanhoArquivo <= tamanhoMaximo && tamanhoArquivo % sizeof(RegistroFlash) == 0;
    file.close();
    return ok;
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
    File file = LittleFS.open("/ack.bin", "r");
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
    File file = LittleFS.open("/ack.tmp", "w");
    bool ok = file && file.write(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack)) == sizeof(ack);
    file.flush(); file.close();
    if (!ok) return false;
    file = LittleFS.open("/ack.tmp", "r");
    Ack teste;
    ok = file && file.read(reinterpret_cast<uint8_t*>(&teste), sizeof(teste)) == sizeof(teste) && !memcmp(&ack, &teste, sizeof(ack));
    file.close();
    // LittleFS oferece rename atômico; se houver queda, o ACK anterior permite reenvio.
    return ok && LittleFS.rename("/ack.tmp", "/ack.bin");
  }
};
