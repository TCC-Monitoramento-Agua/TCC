#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>
#include <esp_system.h>

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

// ALTERE AQUI: intervalo entre INÍCIOS das coletas, em milissegundos.
const uint32_t INTERVALO_MEDICAO_MS = 30000; // 30 segundos
uint32_t ultimaMedicao = 0;
const uint16_t HTTP_TIMEOUT_MS = 60000; // Espera pela resposta da API
const int32_t HTTP_CONNECT_TIMEOUT_MS = 15000; // Espera pela conexão

const int NUM_AMOSTRAS = 10;
const unsigned long TEMPO_ENTRE_AMOSTRAS = 500; // 500 ms entre amostras

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

  Serial.println("\n[SENSOR] Iniciando 10 amostras...");

  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    float leituraPh = mapAnalogToRange(PH_PIN, 6.5, 8.5);
    float leituraTurbidez = mapAnalogToRange(TURB_PIN, 1.0, 50.0);
    float leituraOrp = mapAnalogToRange(ORP_PIN, 200.0, 300.0);

    somaPh += leituraPh;
    somaTurbidez += leituraTurbidez;
    somaOrp += leituraOrp;

    Serial.print("[SENSOR] Amostra ");
    Serial.print(i + 1);
    Serial.print(": pH=");
    Serial.print(leituraPh, 2);
    Serial.print(" | Turbidez=");
    Serial.print(leituraTurbidez, 2);
    Serial.print(" | ORP=");
    Serial.println(leituraOrp, 2);

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

    Serial.print("[SENSOR] Amostra ");
    Serial.print(i + 1);
    Serial.print(": Temperatura=");
    Serial.print(temp, 2);
    Serial.println(" C");
  }

  if (amostrasValidas == 0) {
    return 0.0;
  }

  return somaTemp / amostrasValidas;
}

#include "QueueRuntime.h"

void sendReading() {
// Verificar se a hora foi sincronizada

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
StaticJsonDocument<1024> doc;
doc["leitura_id"] = gerarLeituraId();
doc["equipamento_id"] = String(EQUIPAMENTO_ID).length() > 0
    ? String(EQUIPAMENTO_ID) : WiFi.macAddress();
doc["ph"] = ph;
doc["turbidez"] = turbidez;
doc["temperatura"] = temperature;
doc["orp"] = orp;
doc["data_hora"] = timestamp;

  armazenarMedicao(doc);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(TURB_PIN, ADC_11db);
  analogSetPinAttenuation(ORP_PIN, ADC_11db);
  sensors.begin();
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
