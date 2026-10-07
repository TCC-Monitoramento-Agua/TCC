#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_system.h>

// Pino do potenciometro
#define PH_PIN 34

// API
const char* serverUrl = "https://api-monitoramento-agua.onrender.com/leituras";

// ID opcional por instância (útil no Wokwi, onde o MAC pode se repetir).
// Exemplo de build flag: -D EQUIPAMENTO_ID=\"esp32-001\"
#ifndef EQUIPAMENTO_ID
#define EQUIPAMENTO_ID ""
#endif

// NTP.br fornece a hora UTC; o ESP32 aplica o fuso de Brasília automaticamente.
// Na sintaxe POSIX, BRT3 representa UTC-3, sem horário de verão.
const char* TIME_ZONE = "BRT3";
const char* NTP_SERVER_PRIMARY = "a.ntp.br";
const char* NTP_SERVER_SECONDARY = "b.ntp.br";
const char* NTP_SERVER_TERTIARY = "c.ntp.br";

// WiFi
const char* ssid = "iPhone";
const char* password = "oiboanoite";

// Configuracoes
// ALTERE AQUI: intervalo entre INÍCIOS das coletas, em milissegundos.
const uint32_t INTERVALO_MEDICAO_MS = 30000; // 30 segundos
uint32_t ultimaMedicao = 0;
const uint16_t HTTP_TIMEOUT_MS = 60000; // Espera pela resposta da API
const int32_t HTTP_CONNECT_TIMEOUT_MS = 15000; // Espera pela conexão

const int NUM_AMOSTRAS = 10;
const unsigned long TEMPO_ENTRE_AMOSTRAS = 500;

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

float lerMediaPh() {
  float somaPh = 0.0;

  Serial.println("\n[SENSOR] Iniciando 10 amostras de pH...");

  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    int leituraD34 = analogRead(PH_PIN);
    float ph = (leituraD34 / 4095.0) * 14.0;

    somaPh += ph;

    Serial.print("[SENSOR] Amostra ");
    Serial.print(i + 1);
    Serial.print(": ADC=");
    Serial.print(leituraD34);
    Serial.print(" | pH=");
    Serial.println(ph, 2);

    if (i < NUM_AMOSTRAS - 1) {
      delay(TEMPO_ENTRE_AMOSTRAS);
    }
  }

  return somaPh / NUM_AMOSTRAS;
}

#include "QueueRuntime.h"

void sendReading() {


  // pH obtido pelo potenciometro
  float ph = lerMediaPh();

  // Valores simulados enquanto os sensores reais nao estao disponiveis
  float temperatura = 22.5;
  float turbidez = 2.3;
  float orp = 450.0;

  Serial.println("\n[SENSOR] ===== RESULTADOS =====");
  Serial.print("[SENSOR] pH medio: ");
  Serial.println(ph, 2);
  Serial.print("[SENSOR] Temperatura simulada: ");
  Serial.print(temperatura, 1);
  Serial.println(" C");
  Serial.print("[SENSOR] Turbidez simulada: ");
  Serial.print(turbidez, 1);
  Serial.println(" NTU");
  Serial.print("[SENSOR] ORP simulado: ");
  Serial.print(orp, 0);
  Serial.println(" mV");

  StaticJsonDocument<1024> doc;
  doc["leitura_id"] = gerarLeituraId();
  doc["equipamento_id"] = String(EQUIPAMENTO_ID).length() > 0
      ? String(EQUIPAMENTO_ID) : WiFi.macAddress();
  doc["ph"] = ph;
  doc["turbidez"] = turbidez;
  doc["temperatura"] = temperatura;
  doc["orp"] = orp;
  doc["data_hora"] = getTimestamp();

  armazenarMedicao(doc);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  configTzTime(TIME_ZONE, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY, NTP_SERVER_TERTIARY);
  iniciarFila();
  ultimaMedicao = millis() - INTERVALO_MEDICAO_MS;
}

void loop() {
  if (millis() - ultimaMedicao >= INTERVALO_MEDICAO_MS) {
    ultimaMedicao = millis();
    sendReading(); // Coleta e salva; a tarefa separada cuida dos envios.
    if (millis() - ultimaMedicao >= INTERVALO_MEDICAO_MS) {
      Serial.println("[SENSOR] ALERTA: duração da coleta excedeu o intervalo configurado");
    }
  }
  delay(20);
}
