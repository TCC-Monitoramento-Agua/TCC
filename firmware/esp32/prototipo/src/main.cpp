#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// Pino do potenciometro
#define PH_PIN 34

// API
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
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 10000;
const uint16_t HTTP_TIMEOUT_MS = 60000; // Espera pela resposta da API
const int32_t HTTP_CONNECT_TIMEOUT_MS = 15000; // Espera pela conexão

const int NUM_AMOSTRAS = 10;
const unsigned long TEMPO_ENTRE_AMOSTRAS = 500;

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

int enviarPostComRetry(const String& payload) {
  StaticJsonDocument<512> original;
  if (deserializeJson(original, payload)) return -1;
  const String leituraId = original["leitura_id"].as<String>();
  const String equipamentoId = original["equipamento_id"].as<String>();
  const int maxAttempts = 3;

  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    Serial.print("[HTTP] Tentativa ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.println(maxAttempts);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, serverUrl)) {
      Serial.println("[HTTP] ERRO ao iniciar conexao HTTPS!");
      client.stop();
    } else {
      http.addHeader("Content-Type", "application/json");

      Serial.println("[HTTP] Enviando POST...");
      int httpCode = http.POST(payload);

      Serial.print("[HTTP] Codigo HTTP: ");
      Serial.println(httpCode);

      String response = http.getString();
      Serial.print("[RESPOSTA] ");
      Serial.println(response);

      StaticJsonDocument<512> confirmacao;
      bool confirmou = (httpCode == 200 || httpCode == 201) &&
          !deserializeJson(confirmacao, response) &&
          confirmacao["status"].as<String>() == "ok" &&
          confirmacao["leitura_id"].as<String>() == leituraId &&
          confirmacao["equipamento_id"].as<String>() == equipamentoId &&
          confirmacao["id"].is<uint64_t>() && confirmacao["id"].as<uint64_t>() > 0;
      if (confirmou) {
        Serial.println("[HTTP] SUCESSO! Leitura registrada.");
        http.end();
        client.stop();
        return httpCode;
      }

      if (httpCode == 409) {
  Serial.println("[HTTP] CONFLITO: identificador reutilizado com dados diferentes");
  http.end(); client.stop(); return httpCode;
}
if (httpCode == 200 || httpCode == 201) {
  Serial.println("[HTTP] Resposta sem confirmação válida desta leitura; confira a versão da API");
}
if (httpCode < 0) {
        Serial.print("[HTTP] ERRO DE CONEXAO: ");
        Serial.println(http.errorToString(httpCode));
      } else {
        Serial.print("[HTTP] FALHA! Codigo recebido: ");
        Serial.println(httpCode);
      }

      http.end();
      client.stop();
    }

    if (attempt < maxAttempts) {
      Serial.println("[HTTP] Nova tentativa em 2 segundos...");
      delay(2000);
    }
  }

  Serial.println("[HTTP] Todas as tentativas falharam.");
  return -1;
}

void sendReading() {
  time_t nowSec = time(nullptr);

  if (nowSec < 1000000000) {
    Serial.println("[SENSOR] ERRO: Hora nao sincronizada. Pulando envio.");
    return;
  }

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

  StaticJsonDocument<384> doc;
  doc["leitura_id"] = gerarLeituraId();
  doc["equipamento_id"] = String(EQUIPAMENTO_ID).length() > 0
      ? String(EQUIPAMENTO_ID) : WiFi.macAddress();
  doc["ph"] = ph;
  doc["turbidez"] = turbidez;
  doc["temperatura"] = temperatura;
  doc["orp"] = orp;
  doc["data_hora"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);

  Serial.println("\n[HTTP] ========== ENVIANDO LEITURA ==========");
  Serial.print("[JSON] ");
  Serial.println(payload);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Wi-Fi desconectado. Tentando reconectar...");
    connectWiFi();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[HTTP] Nao foi possivel reconectar.");
      return;
    }
  }

  int result = enviarPostComRetry(payload);
#if TESTAR_DUPLICACAO
  if (result == 200 || result == 201) {
    Serial.println("[TESTE] Reenviando exatamente o mesmo JSON para verificar duplicação");
    enviarPostComRetry(payload);
  }
#endif

  if (result < 0) {
    Serial.println("[HTTP] ERRO: leitura nao enviada.");
  }

  Serial.println("[HTTP] ========== FIM DO ENVIO ==========");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n================================");
  Serial.println("BOOT ESP32 - PROTOTIPO FISICO");
  Serial.println("================================");

  analogSetPinAttenuation(PH_PIN, ADC_11db);

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
    Serial.print("[SETUP] Horario de Brasilia sincronizado: ");
    Serial.println(getTimestamp());
  } else {
    Serial.println("[SETUP] Falha ao sincronizar horario.");
  }

  Serial.println("[SETUP] Sistema pronto.\n");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOP] Wi-Fi desconectado. Tentando reconectar...");
    connectWiFi();
  }

  if (millis() - lastSend >= SEND_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) {
      sendReading();
    } else {
      Serial.println("[LOOP] Sem Wi-Fi. Pulando envio.");
    }

    lastSend = millis();
  }

  delay(100);
}