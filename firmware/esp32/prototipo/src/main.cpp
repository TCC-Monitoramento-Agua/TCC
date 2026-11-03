#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <algorithm>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "SensoresFisicos.h"
#include "DiagnosticoLocal.h"

OneWire oneWire(TEMPERATURA_PIN);
DallasTemperature sensorTemperatura(&oneWire);

// API
const char* serverUrl = "https://api-monitoramento-agua.onrender.com/leituras";

// ID opcional por instância (útil no Wokwi, onde o MAC pode se repetir).
// Exemplo de build flag: -D EQUIPAMENTO_ID=\"esp32-001\"
#ifndef EQUIPAMENTO_ID
#define EQUIPAMENTO_ID "físico_1"
#endif

// Teste controlado: 1 reenvia cada leitura confirmada uma vez, sem gerar novo UUID.
#ifndef TESTAR_DUPLICACAO
#define TESTAR_DUPLICACAO 0
#endif

ConsoleDiagnostico consoleDiagnostico(Serial);
#define Serial consoleDiagnostico
DiagnosticoLocal diagnostico(consoleDiagnostico, EQUIPAMENTO_ID);

#include "FilaLocal.h"

uint16_t ultimoAdcPh = 0, ultimoAdcTurbidez = 0;

void atualizarEstadoDiagnostico() {
  consoleDiagnostico.fila(filaPronta && filaLocal.saudavel(), filaLocal.pendentes(),
      CAPACIDADE_FILA, relogioColeta.podeColetar(time(nullptr)));
}

// NTP.br fornece a hora UTC; o ESP32 aplica o fuso de Brasília automaticamente.
// Na sintaxe POSIX, BRT3 representa UTC-3, sem horário de verão.
const char* TIME_ZONE = "BRT3";
const char* NTP_SERVER_PRIMARY = "a.ntp.br";
const char* NTP_SERVER_SECONDARY = "b.ntp.br";
const char* NTP_SERVER_TERTIARY = "c.ntp.br";

// WiFi
const char* ssid = "iPhone de Lucas";
const char* password = "Lu@@2021";

// Configuracoes
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 10000;
const uint16_t HTTP_TIMEOUT_MS = 60000; // Espera pela resposta da API
const int32_t HTTP_CONNECT_TIMEOUT_MS = 15000; // Espera pela conexão

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("\n[WiFi] Conectando ao: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] CONECTADO!");
    Serial.print("[WiFi] IP local: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] ERRO ao conectar");
  }
}

// Gerado uma vez por medição; as tentativas HTTP reutilizam o mesmo JSON.
String gerarLeituraId() {
  uint8_t bytes[16];
  esp_fill_random(bytes, sizeof(bytes));
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  char id[37];
  snprintf(id, sizeof(id),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
      bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return String(id);
}

String getTimestamp() {
  time_t nowSec = time(nullptr);
  struct tm timeinfo;
  localtime_r(&nowSec, &timeinfo);

  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

  return String(buf);
}

uint16_t lerMedianaAdc(uint8_t pino) {
  uint16_t amostras[NUM_AMOSTRAS_FISICAS];
  for (size_t i = 0; i < NUM_AMOSTRAS_FISICAS; ++i) {
    amostras[i] = analogRead(pino);
    if (i + 1 < NUM_AMOSTRAS_FISICAS) delay(INTERVALO_AMOSTRAS_FISICAS_MS);
  }
  std::sort(amostras, amostras + NUM_AMOSTRAS_FISICAS);
  return amostras[NUM_AMOSTRAS_FISICAS / 2];
}

float lerPhCalibrado() {
  const uint16_t adc = lerMedianaAdc(PH_PIN);
  ultimoAdcPh = adc;
  const float tensao = tensaoEscalaCalibracao(adc);
  const float ph = converterPh(tensao);
  Serial.printf("[PH] GPIO=%u | ADC=%u | tensão na escala da calibração=%.3f V | pH=%.2f\r\n",
      PH_PIN, adc, tensao, ph);
  if (!std::isfinite(ph) || ph < 0.0f || ph > 14.0f || adc == 0 || adc == 4095) {
    Serial.println("[PH] ERRO: leitura fora da faixa; confira sonda, ligação e calibração");
    return NAN;
  }
  return ph;
}

float lerTurbidezRelativa() {
  const uint16_t adc = lerMedianaAdc(TURBIDEZ_FISICA_PIN);
  ultimoAdcTurbidez = adc;
  const float turbidez = turbidezRelativa(adc);
  if (adc == 0 || adc == 4095 || !std::isfinite(turbidez)) {
    Serial.printf("[TURBIDEZ] GPIO=%u | ADC=%u | escala ADC=%.3f V | sem medição: entrada saturada ou referência inválida\r\n",
        TURBIDEZ_FISICA_PIN, adc, tensaoEscalaCalibracao(adc));
    return NAN;
  }
  Serial.printf("[TURBIDEZ] GPIO=%u | ADC=%u | escala ADC=%.3f V | referência=%u | queda do sinal=%.1f %% (não é NTU)\r\n",
      TURBIDEZ_FISICA_PIN, adc, tensaoEscalaCalibracao(adc), ADC_AGUA_REFERENCIA, turbidez);
  return turbidez;
}

float lerTemperatura() {
  if (!sensorTemperatura.getDeviceCount()) sensorTemperatura.begin();
  sensorTemperatura.requestTemperatures(); // Aguarda a conversão antes de ler.
  const float temperatura = sensorTemperatura.getTempCByIndex(0);
  if (temperatura == DEVICE_DISCONNECTED_C || !temperaturaFisicaValida(temperatura)) {
    Serial.println("[TEMPERATURA] ERRO: DS18B20 ausente ou leitura inválida");
    return NAN;
  }
  Serial.printf("[TEMPERATURA] GPIO=%u | DS18B20=%.2f C\r\n", TEMPERATURA_PIN, temperatura);
  return temperatura;
}

int enviarPostComRetry(const String& payload) {
  StaticJsonDocument<512> original;
  if (deserializeJson(original, payload)) return -1;
  const String leituraId = original["leitura_id"].as<String>();
  const String equipamentoId = original["equipamento_id"].as<String>();
  consoleDiagnostico.envio(true);
  WiFiClientSecure client;
  client.setInsecure(); // Configuração HTTPS existente.
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, serverUrl)) {
    Serial.println("[ENVIO] Não foi possível iniciar HTTPS; leitura continua no ESP32");
    consoleDiagnostico.envio(false, -1);
    client.stop(); return -1;
  }
  http.addHeader("Content-Type", "application/json");
  int codigo = http.POST(payload);
  String resposta = codigo > 0 ? http.getString() : http.errorToString(codigo);
#if LOG_DETALHADO
  Serial.printf("[DEBUG] Resposta HTTP %d: %s\r\n", codigo, resposta.c_str());
#endif
  http.end(); client.stop();
  StaticJsonDocument<512> ack;
  bool confirmou = (codigo == 200 || codigo == 201) && !deserializeJson(ack, resposta) &&
      ack["status"].as<String>() == "ok" &&
      ack["leitura_id"].as<String>() == leituraId &&
      ack["equipamento_id"].as<String>() == equipamentoId &&
      (original["turbidez_relativa"].isNull() || ack["turbidez_unidade"].as<String>() == "percentual_relativo") &&
      ack["id"].is<uint64_t>() && ack["id"].as<uint64_t>() > 0;
  if (confirmou) {
    consoleDiagnostico.envio(false, codigo, true);
    Serial.printf("[BANCO] CONFIRMADO | UUID=%s | id=%llu | HTTP=%d | duplicada=%s\r\n",
        leituraId.c_str(), static_cast<unsigned long long>(ack["id"].as<uint64_t>()),
        codigo, ack["duplicada"].as<bool>() ? "sim" : "não");
    return codigo;
  }
  Serial.printf("[ENVIO] FALHOU | UUID=%s | HTTP=%d | %s\r\n", leituraId.c_str(), codigo,
      codigo < 0 ? resposta.c_str() : codigo == 409 ? "UUID em conflito" :
      (codigo == 200 || codigo == 201) ? "confirmação inválida" : "API não confirmou gravação");
  if (codigo > 0 && resposta.length()) Serial.printf("[ENVIO] Detalhe da API: %.300s\r\n", resposta.c_str());
  consoleDiagnostico.envio(false, codigo);
  return -1;
}

void sendReading() {
  if (!coletaComHorarioPermitida()) return;
  const String horarioColeta = getTimestamp();
  const float ph = lerPhCalibrado();
  const float temperatura = lerTemperatura();
  const float turbidez = lerTurbidezRelativa();
  consoleDiagnostico.leitura(ph, temperatura, turbidez, ultimoAdcPh, ultimoAdcTurbidez,
      tensaoEscalaCalibracao(ultimoAdcPh), tensaoEscalaCalibracao(ultimoAdcTurbidez), ADC_AGUA_REFERENCIA);
  if (!std::isfinite(ph) || !std::isfinite(temperatura) || !std::isfinite(turbidez)) {
    Serial.println("[LEITURA] Medição não criada: sensor ativo com leitura inválida; pendências continuam sendo enviadas");
    return;
  }

  StaticJsonDocument<1024> doc;
  doc["leitura_id"] = gerarLeituraId();
  doc["equipamento_id"] = String(EQUIPAMENTO_ID).length() > 0
      ? String(EQUIPAMENTO_ID) : WiFi.macAddress();
  doc["ph"] = ph;
  // Campo distinto impede que uma API antiga grave o percentual como NTU.
  doc["turbidez_relativa"] = turbidez;
  doc["temperatura"] = temperatura;
  doc["tds"] = nullptr; // Sensor temporariamente desativado; não enviar zero fictício.
  doc["data_hora"] = horarioColeta;

  guardarMedicaoLocal(doc);
  atualizarEstadoDiagnostico();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n================================");
  Serial.println("BOOT ESP32 - PROTOTIPO FISICO");
  Serial.println("================================");

  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(TURBIDEZ_FISICA_PIN, ADC_11db);
  sensorTemperatura.begin();
  sensorTemperatura.setResolution(12);
  sensorTemperatura.setWaitForConversion(true);
  Serial.println("[SENSORES] pH=GPIO34 | turbidez relativa=GPIO32 | DS18B20=GPIO14 | TDS desativado");

  Serial.printf("[BOOT] Motivo do reset: %s\r\n", motivoReset());
  iniciarFilaLocal();
  atualizarEstadoDiagnostico();
  connectWiFi();
  if (!diagnostico.iniciar()) Serial.println("[DIAGNOSTICO] ERRO: não foi possível iniciar a página local");

  Serial.println("\n[SETUP] Sincronizando horario de Brasilia via NTP.br...");
  sntp_set_time_sync_notification_cb(aoSincronizarHorario);
  configTzTime(TIME_ZONE, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY, NTP_SERVER_TERTIARY);

  time_t nowSec = time(nullptr);
  int attempts = 0;

  while (!relogioColeta.podeColetar(nowSec) && attempts < 20) {
    delay(500);
    nowSec = time(nullptr);
    attempts++;
  }

  if (relogioColeta.podeColetar(nowSec)) {
    Serial.print("[SETUP] Horario de Brasilia sincronizado: ");
    Serial.println(getTimestamp());
  } else {
    Serial.println("[SETUP] Sem sincronização NTP: coleta suspensa até o relógio sincronizar.");
  }

  Serial.println("[SETUP] Loop iniciado; coleta depende de horário sincronizado.\n");
}

void loop() {
  static unsigned long ultimaReconexao = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - ultimaReconexao >= 30000) {
    ultimaReconexao = millis();
    Serial.println("[WiFi] Tentando reconectar; coleta continua");
    WiFi.begin(ssid, password);
  }
  if (millis() - lastSend >= SEND_INTERVAL) {
    sendReading(); // Após sincronizar o relógio, continua coletando mesmo sem Wi-Fi/API.
    lastSend = millis();
  }
  atualizarEstadoDiagnostico();
  processarFilaLocal(enviarPostComRetry);
  atualizarEstadoDiagnostico();
  delay(100);
}
