#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

// Pins mapping
#define ONEWIRE_PIN 14
#define PH_PIN 34
#define TURB_PIN 35
#define ORP_PIN 32

// Endpoint (HTTPS com WiFiClientSecure)
const char* serverUrl = "https://api-monitoramento-agua.onrender.com/leituras";

// ID opcional por instância (útil no Wokwi, onde o MAC pode se repetir).
// Exemplo de build flag: -D EQUIPAMENTO_ID=\"esp32-001\"
#ifndef EQUIPAMENTO_ID
#define EQUIPAMENTO_ID ""
#endif

// Teste controlado: 1 reenvia cada leitura confirmada uma vez, sem gerar novo UUID.
#ifndef TESTAR_DUPLICACAO
#define TESTAR_DUPLICACAO 0
#endif

#include "FilaLocal.h"

// NTP.br fornece a hora UTC; o ESP32 aplica o fuso de Brasília automaticamente.
// Na sintaxe POSIX, BRT3 representa UTC-3, sem horário de verão.
const char* TIME_ZONE = "BRT3";
const char* NTP_SERVER_PRIMARY = "a.ntp.br";
const char* NTP_SERVER_SECONDARY = "b.ntp.br";
const char* NTP_SERVER_TERTIARY = "c.ntp.br";

// WiFi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// OneWire and temperature sensor
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature sensors(&oneWire);

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 10000;
const uint16_t HTTP_TIMEOUT_MS = 60000; // Espera pela resposta da API
const int32_t HTTP_CONNECT_TIMEOUT_MS = 15000; // Espera pela conexão

const int NUM_AMOSTRAS = 10;
const unsigned long TEMPO_ENTRE_AMOSTRAS = 500; // 500 ms entre amostras

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("\n[WiFi] Conectando ao: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    Serial.print('.');
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] CONECTADO!");
    Serial.print("[WiFi] IP local: ");
    Serial.println(WiFi.localIP());
    return;
  }

  Serial.println("\n[WiFi] ERRO ao conectar");
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

float mapAnalogToRange(int pin, float minVal, float maxVal) {
int raw = analogRead(pin);
float normalized = (float)raw / 4095.0;
return minVal + normalized * (maxVal - minVal);
}

void lerMediasAnalogicas(float& ph, float& turbidez, float& orp) {
  float somaPh = 0.0;
  float somaTurbidez = 0.0;
  float somaOrp = 0.0;

  #if LOG_DETALHADO
Serial.println("\n[SENSOR] Iniciando 10 amostras...");
#endif

  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    float leituraPh = mapAnalogToRange(PH_PIN, 6.5, 8.5);
    float leituraTurbidez = mapAnalogToRange(TURB_PIN, 1.0, 50.0);
    float leituraOrp = mapAnalogToRange(ORP_PIN, 200.0, 300.0);

    somaPh += leituraPh;
    somaTurbidez += leituraTurbidez;
    somaOrp += leituraOrp;

#if LOG_DETALHADO
    Serial.print("[SENSOR] Amostra ");
    Serial.print(i + 1);
    Serial.print(": pH=");
    Serial.print(leituraPh, 2);
    Serial.print(" | Turbidez=");
    Serial.print(leituraTurbidez, 2);
    Serial.print(" | ORP=");
    Serial.println(leituraOrp, 2);
#endif

    if (i < NUM_AMOSTRAS - 1) {
        delay(TEMPO_ENTRE_AMOSTRAS);
    }
  }

  ph = somaPh / NUM_AMOSTRAS;
  turbidez = somaTurbidez / NUM_AMOSTRAS;
  orp = somaOrp / NUM_AMOSTRAS;
}

float lerMediaTemperatura() {
  const int NUM_AMOSTRAS_TEMP = 3;
  float somaTemp = 0.0;
  int amostrasValidas = 0;

  #if LOG_DETALHADO
Serial.println("\n[SENSOR] Iniciando 3 amostras de temperatura...");
#endif

  for (int i = 0; i < NUM_AMOSTRAS_TEMP; i++) {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);

    if (temp == DEVICE_DISCONNECTED_C) {
      Serial.print("[SENSOR] Amostra ");
      Serial.print(i + 1);
      Serial.println(": ERRO - DS18B20 desconectado");
      continue;
    }

    somaTemp += temp;
    amostrasValidas++;

#if LOG_DETALHADO
    Serial.print("[SENSOR] Amostra ");
    Serial.print(i + 1);
    Serial.print(": Temperatura=");
    Serial.print(temp, 2);
    Serial.println(" C");
#endif
  }

  if (amostrasValidas == 0) {
    return 0.0;
  }

  return somaTemp / amostrasValidas;
}

int enviarPostComRetry(const String& payload) {
  StaticJsonDocument<512> original;
  if (deserializeJson(original, payload)) return -1;
  const String leituraId = original["leitura_id"].as<String>();
  const String equipamentoId = original["equipamento_id"].as<String>();
  WiFiClientSecure client;
  client.setInsecure(); // Configuração HTTPS existente.
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, serverUrl)) {
    Serial.println("[ENVIO] Não foi possível iniciar HTTPS; leitura continua no ESP32");
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
      ack["id"].is<uint64_t>() && ack["id"].as<uint64_t>() > 0;
  if (confirmou) {
    Serial.printf("[BANCO] CONFIRMADO | UUID=%s | id=%llu | HTTP=%d | duplicada=%s\r\n",
        leituraId.c_str(), static_cast<unsigned long long>(ack["id"].as<uint64_t>()),
        codigo, ack["duplicada"].as<bool>() ? "sim" : "não");
    return codigo;
  }
  Serial.printf("[ENVIO] FALHOU | UUID=%s | HTTP=%d | %s\r\n", leituraId.c_str(), codigo,
      codigo < 0 ? resposta.c_str() : codigo == 409 ? "UUID em conflito" :
      (codigo == 200 || codigo == 201) ? "confirmação inválida" : "API não confirmou gravação");
  return -1;
}

void sendReading() {
// Leitura dos sensores
float temperature = lerMediaTemperatura();
float ph, turbidez, orp;
lerMediasAnalogicas(ph, turbidez, orp);

StaticJsonDocument<1024> doc;
doc["leitura_id"] = gerarLeituraId();
doc["equipamento_id"] = String(EQUIPAMENTO_ID).length() > 0
    ? String(EQUIPAMENTO_ID) : WiFi.macAddress();
doc["ph"] = ph;
doc["turbidez"] = turbidez;
doc["temperatura"] = temperature;
doc["orp"] = orp;
doc["data_hora"] = getTimestamp();

guardarMedicaoLocal(doc);
}

void setup() {
Serial.begin(115200);
delay(1000);
Serial.flush();

Serial.println("\n\n================================");
Serial.println("BOOT ESP32 WOKWI");
Serial.println("================================\n");

Serial.println("[SETUP] Inicializando GPIO e sensores...");
analogSetPinAttenuation(PH_PIN, ADC_11db);
analogSetPinAttenuation(TURB_PIN, ADC_11db);
analogSetPinAttenuation(ORP_PIN, ADC_11db);

sensors.begin();
Serial.println("[SETUP] Sensores inicializados.\n");

Serial.println("[SETUP] Conectando ao Wi-Fi...");
Serial.printf("[BOOT] Motivo do reset: %s\r\n", motivoReset());
iniciarFilaLocal(gerarLeituraId());
connectWiFi();

Serial.println("\n[SETUP] Sincronizando horario de Brasilia via NTP.br...");
configTzTime(TIME_ZONE, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY, NTP_SERVER_TERTIARY);
time_t nowSec = time(nullptr);
int attempts = 0;
while (nowSec < 1000000000 && attempts < 20) {
delay(500);
nowSec = time(nullptr);
attempts++;
}

if (nowSec >= 1000000000) {
Serial.println("[SETUP] Horario de Brasilia sincronizado: " + getTimestamp());
} else {
Serial.println("[SETUP] Falha ao sincronizar horario via NTP.br.");
}
Serial.println("[SETUP] Setup completo! Iniciando loop...\n");
}

void loop() {
  static unsigned long ultimaReconexao = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - ultimaReconexao >= 30000) {
    ultimaReconexao = millis();
    Serial.println("[WiFi] Tentando reconectar; coleta continua");
    WiFi.begin(ssid, password);
  }
  if (millis() - lastSend >= SEND_INTERVAL) {
    sendReading(); // Também coleta quando o Wi-Fi ou a API estão indisponíveis.
    lastSend = millis();
  }
  processarFilaLocal(enviarPostComRetry);
  delay(100);
}
