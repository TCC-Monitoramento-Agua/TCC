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
// 1 para imprimir cada amostra; 0 mantém médias, JSON e resultados HTTP.
#ifndef LOG_AMOSTRAS
#define LOG_AMOSTRAS 0
#endif
// O gerador distribui as instâncias em quatro posições de 3 segundos.
#ifndef ATRASO_INICIAL_MS
#define ATRASO_INICIAL_MS 0
#endif

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

  Serial.println("\n[SENSOR] Iniciando 10 amostras...");

  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    float leituraPh = mapAnalogToRange(PH_PIN, 6.5, 8.5);
    float leituraTurbidez = mapAnalogToRange(TURB_PIN, 1.0, 50.0);
    float leituraOrp = mapAnalogToRange(ORP_PIN, 200.0, 300.0);

    somaPh += leituraPh;
    somaTurbidez += leituraTurbidez;
    somaOrp += leituraOrp;

#if LOG_AMOSTRAS
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

  Serial.println("\n[SENSOR] Iniciando 3 amostras de temperatura...");

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

#if LOG_AMOSTRAS
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
const int maxAttempts = 3;
for (int attempt = 1; attempt <= maxAttempts; attempt++) {
Serial.print("[HTTP] Tentativa ");
Serial.print(attempt);
Serial.print("/" );
Serial.println(maxAttempts);

WiFiClientSecure client;
client.setInsecure();
HTTPClient http;
http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
http.setTimeout(HTTP_TIMEOUT_MS);

Serial.println("[HTTP] Iniciando conexao HTTPS...");
if (!http.begin(client, serverUrl)) {
Serial.println("[HTTP] ✗ ERRO ao iniciar conexao HTTPS!");
http.end();
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

if (httpCode == 200 || httpCode == 201) {
Serial.println("[HTTP] ✓ SUCESSO! Leitura registrada.");
http.end();
client.stop();
return httpCode;
}

if (httpCode < 0) {
Serial.print("[HTTP] ERRO DE CONEXAO: ");
Serial.println(http.errorToString(httpCode));
} else {
Serial.print("[HTTP] ✗ FALHA! Esperado 200/201, recebido: ");
Serial.println(httpCode);
}

http.end();
client.stop();
}

if (attempt < maxAttempts) {
Serial.println("[HTTP] Aguardando 2 a 5 segundos antes da proxima tentativa...");
delay(2000 + (ATRASO_INICIAL_MS / 3));
}
}

Serial.println("[HTTP] Todas as tentativas falharam. Leitura descartada nesta versao.");
return -1;
}

void sendReading() {
  const unsigned long inicioColeta = millis();
// Verificar se a hora foi sincronizada
time_t nowSec = time(nullptr);
  if (nowSec < 1000000000) {
    Serial.println("[SENSOR] ERRO: Hora nao sincronizada. Pulando envio.");
    return;
  }

// Leitura dos sensores
float temperature = lerMediaTemperatura();
float ph, turbidez, orp;
lerMediasAnalogicas(ph, turbidez, orp);

Serial.println("\n[SENSOR] ===== MÉDIAS =====");
Serial.print("[SENSOR] pH médio: ");
Serial.println(ph, 2);
Serial.print("[SENSOR] Turbidez média: ");
Serial.println(turbidez, 2);
Serial.print("[SENSOR] ORP médio: ");
Serial.println(orp, 2);
Serial.print("[SENSOR] Temperatura média: ");
Serial.print(temperature, 2);
Serial.println(" C");

String timestamp = getTimestamp();

// Criar JSON com ou sem data_hora dependendo da sincronizacao NTP
StaticJsonDocument<384> doc;
doc["equipamento_id"] = String(EQUIPAMENTO_ID).length() > 0
    ? String(EQUIPAMENTO_ID) : WiFi.macAddress();
doc["ph"] = ph;
doc["turbidez"] = turbidez;
doc["temperatura"] = temperature;
doc["orp"] = orp;
doc["data_hora"] = timestamp;

String payload;
serializeJson(doc, payload);

// ===== DEBUG: Imprimir dados =====
Serial.println("\n[HTTP] ========== ENVIANDO LEITURA ==========");
Serial.print("[URL] ");
Serial.println(serverUrl);

Serial.print("[JSON] ");
Serial.println(payload);

if (WiFi.status() != WL_CONNECTED) {
Serial.println("[HTTP] Wi-Fi desconectado antes do envio. Tentando reconectar...");
connectWiFi();
if (WiFi.status() != WL_CONNECTED) {
Serial.println("[HTTP] Nao foi possivel reconectar. Pulando envio.");
Serial.println("[HTTP] ========== FIM DO ENVIO ==========");
return;
}
}

Serial.printf("[TEMPO] Coleta e preparo: %lu ms\n", millis() - inicioColeta);
unsigned long inicioEnvio = millis();
int result = enviarPostComRetry(payload);
Serial.printf("[TEMPO] Envio (inclui tentativas): %lu ms\n", millis() - inicioEnvio);
if (result < 0) {
Serial.println("[HTTP] ERRO: falha ao enviar mesmo apos tentativas.");
}

Serial.println("[HTTP] ========== FIM DO ENVIO ==========");
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
// Evita polling contínuo OneWire durante a conversão; DallasTemperature usa
// delay conforme a resolução do sensor, inclusive no protótipo físico.
sensors.setCheckForConversion(false);
Serial.println("[SETUP] Sensores inicializados.\n");

Serial.println("[SETUP] Conectando ao Wi-Fi...");
Serial.printf("[SETUP] Atraso inicial: %lu ms\n", static_cast<unsigned long>(ATRASO_INICIAL_MS));
delay(ATRASO_INICIAL_MS);
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
static unsigned long lastDebugTime = 0;

// Debug a cada 10 segundos
if (millis() - lastDebugTime >= 10000) {
lastDebugTime = millis();
Serial.print("[LOOP] Rodando... ");
Serial.print(millis() / 1000);
Serial.print("s | WiFi: ");
Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "ERRO");
}

// Reconectar Wi-Fi se necessário
if (WiFi.status() != WL_CONNECTED) {
Serial.println("[LOOP] Wi-Fi desconectado, tentando reconectar...");
connectWiFi();
}

// Enviar leitura a cada SEND_INTERVAL
if (millis() - lastSend >= SEND_INTERVAL) {
if (WiFi.status() == WL_CONNECTED) {
sendReading();
} else {
Serial.println("[LOOP] Nao ha conexao Wi-Fi. Pulando envio.");
}
lastSend = millis();
}

delay(100);
}