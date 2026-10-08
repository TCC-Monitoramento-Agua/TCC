#pragma once
#include "FlashStorage.h"

// Limite de registros, independente da frequência de coleta.
const size_t CAPACIDADE_FILA = 20;
const uint32_t RETRY_INICIAL_MS = 5000;
const uint32_t RETRY_MAXIMO_MS = 60000;
FlashStorage storageFila;
FilaPersistente filaLocal(storageFila, CAPACIDADE_FILA);
bool filaPronta = false;
String bootFila;
uint32_t ultimaTentativaFila = 0, esperaFila = 0;

void iniciarFilaLocal(const String& boot) {
  bootFila = boot;
  Serial.println("[FILA] Inicializando flash...");
  filaPronta = storageFila.montar(CAPACIDADE_FILA) && filaLocal.iniciar();
  if (filaPronta) Serial.printf("[FILA] Recuperadas %u pendências; capacidade=%u\n", unsigned(filaLocal.pendentes()), unsigned(CAPACIDADE_FILA));
  else Serial.println("[FILA] ERRO: flash indisponível ou registros inválidos; dados existentes preservados");
}

bool guardarMedicaoLocal(JsonDocument& doc) {
  if (!filaPronta || !filaLocal.saudavel()) {
    Serial.println("[FILA] ERRO: não foi possível armazenar a medição"); return false;
  }
  if (time(nullptr) < 1000000000) {
    doc["data_hora"] = "";
    doc["_boot_id"] = bootFila;
    doc["_uptime_ms"] = millis();
    Serial.println("[RELOGIO] Sem NTP: guardando valores e uptime até sincronizar neste boot");
  }
  char payload[496];
  const size_t n = measureJson(doc);
  if (doc.overflowed() || n >= sizeof(payload)) {
    Serial.println("[FILA] ERRO: medição maior que o slot"); return false;
  }
  serializeJson(doc, payload, sizeof(payload));
  bool ok = filaLocal.salvar(payload, n);
  Serial.printf("[FILA] %s; pendentes=%u/%u\n", ok ? "Medição salva na flash" : "ERRO: fila cheia ou falha de escrita; novas leituras podem ser perdidas", unsigned(filaLocal.pendentes()), unsigned(CAPACIDADE_FILA));
  if (ok) Serial.printf("[FILA] Salva; [JSON] %s\n", payload);
  return ok;
}

void processarFilaLocal(int (*enviar)(const String&)) {
  if (!filaPronta || !filaLocal.saudavel() || WiFi.status() != WL_CONNECTED) return;
  if (uint32_t(millis() - ultimaTentativaFila) < esperaFila) return;
  RegistroFlash registro;
  if (!filaLocal.frente(registro)) return;
  StaticJsonDocument<1024> doc;
  bool pronto = !deserializeJson(doc, registro.dados);
  if (pronto && !strlen(doc["data_hora"] | "")) {
    pronto = doc["_boot_id"].as<String>() == bootFila && time(nullptr) >= 1000000000;
    if (pronto) {
      time_t epoch = time(nullptr) - uint32_t(millis() - doc["_uptime_ms"].as<uint32_t>()) / 1000;
      struct tm info; localtime_r(&epoch, &info);
      char data[20]; strftime(data, sizeof(data), "%Y-%m-%d %H:%M:%S", &info);
      doc["data_hora"] = data;
      doc.remove("_boot_id"); doc.remove("_uptime_ms");
      char payload[496]; size_t n = measureJson(doc);
      pronto = n < sizeof(payload);
      if (pronto) {
        serializeJson(doc, payload, sizeof(payload));
        // Persistir a hora resolvida ANTES do POST mantém todos os reenvios iguais.
        pronto = filaLocal.atualizarFrente(payload, n);
      }
    }
    if (!pronto) Serial.println("[FILA] Aguardando horário confiável; registro preservado (reinício antes de NTP exige tratamento)");
  }
  int codigo = -1;
  if (pronto) {
    String payload; serializeJson(doc, payload);
    Serial.printf("[FILA] Enviando pendência; [JSON] %s\n", payload.c_str());
    codigo = enviar(payload);
#if TESTAR_DUPLICACAO
    if (codigo == 200 || codigo == 201) {
      Serial.println("[TESTE] Reenviando o mesmo JSON"); enviar(payload);
    }
#endif
  }
  if (codigo == 200 || codigo == 201) {
    bool ok = filaLocal.confirmar(registro.sequencia);
    Serial.printf("[FILA] %s; pendentes=%u\n", ok ? "Confirmada pela API" : "ERRO: confirmação local não gravada; reenvio após recuperação", unsigned(filaLocal.pendentes()));
    esperaFila = 250;
  } else {
    esperaFila = esperaFila < RETRY_INICIAL_MS ? RETRY_INICIAL_MS : min(esperaFila * 2, RETRY_MAXIMO_MS);
    Serial.printf("[FILA] Sem confirmação; leitura mantida na flash. Nova tentativa em %u ms\n", unsigned(esperaFila));
  }
  ultimaTentativaFila = millis();
}
