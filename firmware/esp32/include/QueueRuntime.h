#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "FlashStorage.h"

// 2880 x 512 bytes: 24 horas de registros com intervalo de 30 segundos.
const size_t CAPACIDADE_FILA = 2880;
const uint32_t RETRY_INICIAL_MS = 5000;
const uint32_t RETRY_MAXIMO_MS = 120000;
FlashStorage armazenamento;
FilaPersistente fila(armazenamento, CAPACIDADE_FILA);
SemaphoreHandle_t mutexFila;
String bootId;
bool flashPronta = false;
struct ReferenciaRelogio { int64_t epoch; uint32_t uptime; uint32_t reservado; };
uint32_t crcRelogio(int64_t epoch, uint32_t uptime) {
  ReferenciaRelogio ref = {epoch, uptime, 0};
  return crcFila(&ref, sizeof(ref));
}

String horarioDeEpoch(time_t epoch) {
  struct tm info;
  localtime_r(&epoch, &info);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
  return String(buf);
}

void registrarAncoraRelogio() {
  if (time(nullptr) < 1000000000) return;
  String path = "/clock-" + bootId + ".json";
  xSemaphoreTake(mutexFila, portMAX_DELAY);
  if (!LittleFS.exists(path)) {
    StaticJsonDocument<192> doc;
    doc["epoch"] = static_cast<int64_t>(time(nullptr));
    doc["uptime"] = millis();
    doc["crc"] = crcRelogio(doc["epoch"].as<int64_t>(), doc["uptime"].as<uint32_t>());
    File file = LittleFS.open("/clock.tmp", "w");
    bool ok = file && serializeJson(doc, file) > 0;
    file.flush(); file.close();
    if (ok) {
      file = LittleFS.open("/clock.tmp", "r");
      StaticJsonDocument<192> teste;
      ok = file && !deserializeJson(teste, file) && teste["epoch"].as<int64_t>() == doc["epoch"].as<int64_t>() &&
        teste["crc"].as<uint32_t>() == crcRelogio(teste["epoch"].as<int64_t>(), teste["uptime"].as<uint32_t>());
      file.close();
      if (ok) LittleFS.rename("/clock.tmp", path);
    }
  }
  xSemaphoreGive(mutexFila);
}

bool resolverHorario(JsonDocument& doc) {
  if (doc["data_hora"].is<const char*>() && strlen(doc["data_hora"].as<const char*>())) return true;
  const char* boot = doc["_boot_id"] | "";
  if (!strlen(boot)) return false;
  String path = "/clock-" + String(boot) + ".json";
  xSemaphoreTake(mutexFila, portMAX_DELAY);
  File file = LittleFS.open(path, "r");
  StaticJsonDocument<192> anchor;
  bool ok = file && !deserializeJson(anchor, file);
  file.close();
  xSemaphoreGive(mutexFila);
  if (!ok || !anchor["crc"].is<uint32_t>() || anchor["crc"].as<uint32_t>() !=
      crcRelogio(anchor["epoch"].as<int64_t>(), anchor["uptime"].as<uint32_t>())) return false;
  int32_t delta = static_cast<int32_t>(doc["_uptime_ms"].as<uint32_t>() - anchor["uptime"].as<uint32_t>());
  time_t epoch = static_cast<time_t>(anchor["epoch"].as<int64_t>() + delta / 1000);
  if (epoch < 1000000000) return false;
  doc["data_hora"] = horarioDeEpoch(epoch);
  return true;
}

bool enviarRegistro(const RegistroFlash& registro) {
  StaticJsonDocument<1024> dados;
  if (deserializeJson(dados, registro.dados)) {
    Serial.println("[FILA] ALERTA: JSON inválido; registro preservado");
    return false;
  }
  if (!resolverHorario(dados)) {
    Serial.println("[FILA] ALERTA: medição sem horário confiável; preservada até resolução do relógio");
    return false;
  }
  String id = dados["leitura_id"].as<String>();
  String equipamento = dados["equipamento_id"].as<String>();
  dados.remove("_boot_id"); dados.remove("_uptime_ms");
  String payload;
  serializeJson(dados, payload);
  Serial.printf("[JSON] %s\n", payload.c_str());
  WiFiClientSecure client;
  client.setInsecure(); // Configuração HTTPS existente; certificado deve ser tratado separadamente.
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, serverUrl)) return false;
  http.addHeader("Content-Type", "application/json");
  int codigo = http.POST(payload);
  String resposta = codigo > 0 ? http.getString() : http.errorToString(codigo);
  Serial.printf("[HTTP] Codigo HTTP: %d\n[RESPOSTA] %s\n", codigo, resposta.c_str());
  http.end(); client.stop();
  StaticJsonDocument<512> ack;
  bool confirmado = (codigo == 200 || codigo == 201) && !deserializeJson(ack, resposta) &&
    ack["status"].as<String>() == "ok" && ack["leitura_id"].as<String>() == id &&
    ack["equipamento_id"].as<String>() == equipamento && ack["id"].is<uint64_t>() && ack["id"].as<uint64_t>() > 0;
  if (!confirmado) Serial.println("[FILA] Envio sem confirmação válida: leitura continua na flash");
  return confirmado;
}

void tarefaEnvio(void*) {
  uint32_t espera = RETRY_INICIAL_MS;
  uint32_t ultimaReconexao = millis();
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - ultimaReconexao >= 30000) {
        WiFi.begin(ssid, password);
        ultimaReconexao = millis();
        Serial.println("[WiFi] Tentando reconectar; coleta continua");
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    registrarAncoraRelogio();
    RegistroFlash registro;
    xSemaphoreTake(mutexFila, portMAX_DELAY);
    bool existe = fila.frente(registro);
    bool saudavel = fila.saudavel();
    xSemaphoreGive(mutexFila);
    if (!saudavel) {
      Serial.println("[FILA] ALERTA: falha de integridade ou gravação; envio suspenso, sem formatar flash");
      vTaskDelay(pdMS_TO_TICKS(10000)); continue;
    }
    if (!existe) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    if (enviarRegistro(registro)) {
      xSemaphoreTake(mutexFila, portMAX_DELAY);
      bool ok = fila.confirmar(registro.sequencia);
      size_t pendentes = fila.pendentes();
      xSemaphoreGive(mutexFila);
      Serial.printf("[FILA] %s; pendentes=%u\n", ok ? "Confirmada pela API" : "ALERTA: falha ao persistir confirmação", static_cast<unsigned>(pendentes));
      espera = RETRY_INICIAL_MS;
      vTaskDelay(pdMS_TO_TICKS(250));
    } else {
      Serial.printf("[FILA] Nova tentativa em %lu ms\n", static_cast<unsigned long>(espera));
      vTaskDelay(pdMS_TO_TICKS(espera));
      espera = min(espera * 2, RETRY_MAXIMO_MS);
    }
  }
}

void iniciarFila() {
  mutexFila = xSemaphoreCreateMutex();
  if (!mutexFila) { Serial.println("[FILA] ALERTA: sem memória para mutex"); return; }
  bootId = gerarLeituraId();
  Serial.println("[FILA] Inicializando armazenamento e verificando registros...");
  flashPronta = armazenamento.montar(CAPACIDADE_FILA) && fila.iniciar();
  if (!flashPronta) {
    Serial.println("[FILA] ALERTA: flash indisponível/corrompida. Dados existentes não serão apagados.");
    return;
  }
  Serial.printf("[FILA] Recuperadas %u pendências; capacidade=%u\n", static_cast<unsigned>(fila.pendentes()), static_cast<unsigned>(CAPACIDADE_FILA));
  if (xTaskCreate(tarefaEnvio, "envio-fila", 12288, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("[FILA] ALERTA: não foi possível iniciar tarefa de envio; coleta será mantida na flash");
  }
}

bool armazenarMedicao(JsonDocument& doc) {
  if (!flashPronta) { Serial.println("[FILA] ALERTA: medição não pôde ser persistida"); return false; }
  if (time(nullptr) < 1000000000) {
    doc["data_hora"] = "";
    doc["_boot_id"] = bootId;
    doc["_uptime_ms"] = millis();
    Serial.println("[RELOGIO] Sem NTP: valores guardados com uptime; aguardando referência do mesmo boot");
  }
  char payload[496];
  size_t tamanho = measureJson(doc);
  if (doc.overflowed() || tamanho >= sizeof(payload)) {
    Serial.println("[FILA] ALERTA: medição excede o tamanho do registro"); return false;
  }
  serializeJson(doc, payload, sizeof(payload));
  xSemaphoreTake(mutexFila, portMAX_DELAY);
  bool ok = fila.salvar(payload, tamanho);
  size_t pendentes = fila.pendentes();
  xSemaphoreGive(mutexFila);
  Serial.printf("[FILA] %s; pendentes=%u/%u\n", ok ? "Medição salva na flash" : "ALERTA: fila cheia ou falha de gravação; pendências preservadas", static_cast<unsigned>(pendentes), static_cast<unsigned>(CAPACIDADE_FILA));
  if (pendentes >= CAPACIDADE_FILA * 8 / 10) Serial.println("[FILA] ALERTA: ocupação acima de 80%");
  return ok;
}
