#pragma once
#include "FlashStorage.h"
#include "ControleHorario.h"
#include <esp_sntp.h>
#ifndef LOG_DETALHADO
#define LOG_DETALHADO 0
#endif
const size_t ENVIOS_POR_CICLO = 2;

const char* motivoReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON: início de execução/energia";
    case ESP_RST_SW: return "reinício solicitado pelo firmware";
    case ESP_RST_PANIC: return "falha do firmware (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "queda de tensão";
    default: return "outro";
  }
}

// Limite de registros, independente da frequência de coleta.
const size_t CAPACIDADE_FILA = 1000;
const uint32_t RETRY_INICIAL_MS = 5000;
const uint32_t RETRY_MAXIMO_MS = 60000;
FlashStorage storageFila;
FilaPersistente filaLocal(storageFila, CAPACIDADE_FILA);
bool filaPronta = false;
ControleHorarioColeta relogioColeta;
uint32_t ultimaTentativaFila = 0, esperaFila = 0;

void aoSincronizarHorario(struct timeval*) {
  relogioColeta.marcarSincronizado();
}

bool coletaComHorarioPermitida() {
  if (relogioColeta.podeColetar(time(nullptr))) return true;
  Serial.println("[RELOGIO] Aguardando sincronização NTP; coleta não realizada");
  return false;
}

void iniciarFilaLocal() {
  Serial.println("[FILA] Inicializando flash...");
  filaPronta = storageFila.montar(CAPACIDADE_FILA) && filaLocal.iniciar();
  if (filaPronta) {
    Serial.printf("[LOCAL] Fila pronta | recuperadas=%u | capacidade=%u\r\n", unsigned(filaLocal.pendentes()), unsigned(CAPACIDADE_FILA));
    if (!filaLocal.pendentes()) Serial.println("[LOCAL] Nenhuma leitura pendente encontrada nesta flash");
  }
  else Serial.println("[FILA] ERRO: flash indisponível ou registros inválidos; dados existentes preservados");
}

bool guardarMedicaoLocal(JsonDocument& doc) {
  if (!filaPronta || !filaLocal.saudavel()) {
    Serial.println("[FILA] ERRO: não foi possível armazenar a medição"); return false;
  }
  if (!relogioColeta.podeGuardar(time(nullptr), doc["data_hora"] | "")) {
    Serial.println("[RELOGIO] Medição recusada: horário não sincronizado ou data_hora ausente");
    return false;
  }
  char payload[496];
  const size_t n = measureJson(doc);
  if (doc.overflowed() || n >= sizeof(payload)) {
    Serial.println("[FILA] ERRO: medição maior que o slot"); return false;
  }
  serializeJson(doc, payload, sizeof(payload));
  bool ok = filaLocal.salvar(payload, n);
  if (ok) {
    const bool relativa = !doc["turbidez_relativa"].isNull();
    const double turbidez = relativa ? doc["turbidez_relativa"].as<double>() : doc["turbidez"].as<double>();
    Serial.printf("[LEITURA] %s | pH=%.2f | temperatura=%.1f C | turbidez=%.1f %s | TDS=",
        doc["data_hora"] | "sem horário", doc["ph"].as<double>(), doc["temperatura"].as<double>(),
        turbidez, relativa ? "% relativo" : "NTU");
    if (doc["tds"].isNull()) Serial.println("indisponível");
    else Serial.printf("%.0f ppm\r\n", doc["tds"].as<double>());
    Serial.printf("[LOCAL] SALVA NO ESP32 | UUID=%s | pendentes=%u/%u\r\n",
        doc["leitura_id"] | "", unsigned(filaLocal.pendentes()), unsigned(CAPACIDADE_FILA));
#if LOG_DETALHADO
    Serial.printf("[DEBUG] JSON salvo: %s\r\n", payload);
#endif
  } else {
    Serial.printf("[LOCAL] NÃO SALVA | fila cheia ou falha de escrita | pendentes=%u/%u\r\n",
        unsigned(filaLocal.pendentes()), unsigned(CAPACIDADE_FILA));
  }
  return ok;
}

void processarFilaLocal(int (*enviar)(const String&)) {
  if (!filaPronta || !filaLocal.saudavel() || WiFi.status() != WL_CONNECTED) return;
  if (uint32_t(millis() - ultimaTentativaFila) < esperaFila) return;
  // Duas confirmações por ciclo permitem reduzir a fila mesmo com uma nova coleta.
  for (size_t envio = 0; envio < ENVIOS_POR_CICLO; ++envio) {
    RegistroFlash registro;
    if (!filaLocal.frente(registro)) return;
    StaticJsonDocument<1024> doc;
    bool pronto = !deserializeJson(doc, registro.dados);
    if (pronto && !strlen(doc["data_hora"] | "")) {
      if (!filaLocal.descartarSemHorario(registro.sequencia, doc["data_hora"] | "")) {
        Serial.println("[FILA] ERRO ao descartar registro sem horário; fila suspensa");
        return;
      }
      Serial.printf("[LOCAL] DESCARTADA: leitura antiga sem horário | UUID=%s | pendentes=%u\r\n",
          doc["leitura_id"] | "", unsigned(filaLocal.pendentes()));
      esperaFila = 0;
      ultimaTentativaFila = millis();
      continue;
    }
    int codigo = -1;
    if (pronto) {
      String payload; serializeJson(doc, payload);
      Serial.printf("[ENVIO] Tentando enviar | UUID=%s | pendentes=%u\r\n",
          doc["leitura_id"] | "", unsigned(filaLocal.pendentes()));
  #if LOG_DETALHADO
      Serial.printf("[DEBUG] JSON enviado: %s\r\n", payload.c_str());
  #endif
      codigo = enviar(payload);
  #if TESTAR_DUPLICACAO
      if (codigo == 200 || codigo == 201) {
        Serial.println("[TESTE] Reenviando o mesmo JSON"); enviar(payload);
      }
  #endif
    }
    if (codigo == 200 || codigo == 201) {
      bool ok = filaLocal.confirmar(registro.sequencia);
      if (!ok) {
        Serial.println("[LOCAL] Banco confirmou, mas falhou ao liberar o registro local; fila suspensa");
        return;
      }
      Serial.printf("[LOCAL] Liberada da fila | UUID=%s | pendentes=%u\r\n", doc["leitura_id"] | "", unsigned(filaLocal.pendentes()));
      esperaFila = 250;
      ultimaTentativaFila = millis();
      if (!filaLocal.pendentes()) {
        Serial.println("[LOCAL] FILA VAZIA: todas as leituras pendentes foram confirmadas pelo banco");
        return;
      }
      if (envio + 1 < ENVIOS_POR_CICLO) delay(250);
    } else {
      esperaFila = esperaFila < RETRY_INICIAL_MS ? RETRY_INICIAL_MS : min(esperaFila * 2, RETRY_MAXIMO_MS);
      Serial.printf("[LOCAL] MANTIDA NO ESP32 | UUID=%s | pendentes=%u | próxima tentativa em %u s\r\n",
          doc["leitura_id"] | "", unsigned(filaLocal.pendentes()), unsigned(esperaFila / 1000));
      ultimaTentativaFila = millis();
      return; // Falha não dispara outro POST em seguida.
    }
  }
}
