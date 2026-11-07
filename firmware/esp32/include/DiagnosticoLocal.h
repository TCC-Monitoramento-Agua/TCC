#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cmath>
#include <cstdlib>
#include "LogDiagnostico.h"
#include "PaginaDiagnosticoGzip.h"

struct EstadoDiagnostico {
  float ph = NAN, temperatura = NAN, turbidez = NAN, tds = NAN;
  uint16_t phAdc = 0, turbidezAdc = 0, referencia = 0, tdsAdc = 0;
  uint32_t tdsMv = 0;
  float phVolts = NAN, turbidezVolts = NAN;
  uint32_t leituraMs = 0, envioMs = 0;
  uint16_t pendentes = 0, capacidade = 0;
  int http = 0;
  bool leu = false, medicaoValida = false, horario = false, filaSaudavel = false;
  bool enviando = false, confirmou = false;
};

// Encaminha a saída para a UART e guarda as linhas de aplicação em RAM.
class ConsoleDiagnostico : public Print {
  HardwareSerial& uart;
  LogDiagnostico<> logs;
  EstadoDiagnostico estado;
  portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
 public:
  explicit ConsoleDiagnostico(HardwareSerial& serial) : uart(serial) {}
  void begin(unsigned long baud) { uart.begin(baud); }
  using Print::write;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t n) override {
    const size_t enviados = uart.write(bytes, n);
    portENTER_CRITICAL(&mutex);
    logs.escrever(bytes, n, millis());
    portEXIT_CRITICAL(&mutex);
    return enviados;
  }
  void leitura(float ph, float temperatura, float turbidez, uint16_t phAdc,
               uint16_t turbidezAdc, float phVolts, float turbidezVolts, uint16_t referencia,
               float tds, uint16_t tdsAdc, uint32_t tdsMv) {
    portENTER_CRITICAL(&mutex);
    estado.ph = ph; estado.temperatura = temperatura; estado.turbidez = turbidez;
    estado.phAdc = phAdc; estado.turbidezAdc = turbidezAdc;
    estado.phVolts = phVolts; estado.turbidezVolts = turbidezVolts;
    estado.tds = tds; estado.tdsAdc = tdsAdc; estado.tdsMv = tdsMv;
    estado.referencia = referencia; estado.leituraMs = millis(); estado.leu = true;
    estado.medicaoValida = std::isfinite(ph) && std::isfinite(temperatura) && std::isfinite(turbidez);
    portEXIT_CRITICAL(&mutex);
  }
  void fila(bool saudavel, size_t pendentes, size_t capacidade, bool horario) {
    portENTER_CRITICAL(&mutex);
    estado.filaSaudavel = saudavel; estado.pendentes = pendentes;
    estado.capacidade = capacidade; estado.horario = horario;
    portEXIT_CRITICAL(&mutex);
  }
  void envio(bool emAndamento, int http = 0, bool confirmou = false) {
    portENTER_CRITICAL(&mutex);
    estado.enviando = emAndamento; estado.http = http; estado.confirmou = confirmou;
    estado.envioMs = millis();
    portEXIT_CRITICAL(&mutex);
  }
  EstadoDiagnostico snapshot(uint32_t& primeiro, uint32_t& ultimo) {
    portENTER_CRITICAL(&mutex);
    EstadoDiagnostico copia = estado;
    primeiro = logs.primeiro(); ultimo = logs.ultimo();
    portEXIT_CRITICAL(&mutex);
    return copia;
  }
  bool linha(uint32_t id, LogDiagnostico<>::Linha& destino) {
    portENTER_CRITICAL(&mutex);
    const bool ok = logs.obter(id, destino);
    portEXIT_CRITICAL(&mutex);
    return ok;
  }
};



class DiagnosticoLocal {
  ConsoleDiagnostico& console;
  WiFiServer servidor{80};
  const char* equipamento;
  uint32_t sessao = 0;

  static void cabecalho(WiFiClient& cliente, const char* tipo, int tamanho = -1) {
    cliente.print("HTTP/1.0 200 OK\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Type: ");
    cliente.print(tipo); cliente.print("\r\n");
    if (tamanho >= 0) cliente.printf("Content-Length: %d\r\n", tamanho);
    cliente.print("\r\n");
  }
  void responderEstado(WiFiClient& cliente, const char* url) {
    uint32_t primeiro, ultimo;
    const EstadoDiagnostico estado = console.snapshot(primeiro, ultimo);
    const char* apos = strstr(url, "apos=");
    const char* anterior = strstr(url, "sessao=");
    uint32_t cursor = apos ? strtoul(apos + 5, nullptr, 10) : 0;
    if (!anterior || strtoul(anterior + 7, nullptr, 10) != sessao || cursor > ultimo) cursor = 0;
    StaticJsonDocument<2048> doc;
    doc["sessao"] = sessao; doc["uptime_ms"] = millis(); doc["equipamento"] = equipamento;
    doc["ip"] = WiFi.localIP().toString(); doc["rssi"] = WiFi.RSSI();
    doc["perdidos"] = cursor && primeiro > cursor + 1;
    JsonObject e = doc.createNestedObject("estado");
    e["ph"] = estado.ph; e["temperatura"] = estado.temperatura; e["turbidez"] = estado.turbidez;
    e["turbidez_unidade"] = "ntu_demonstrativo";
    e["tds"] = estado.tds; e["tds_adc"] = estado.tdsAdc; e["tds_mv"] = estado.tdsMv;
    e["ph_adc"] = estado.phAdc; e["turbidez_adc"] = estado.turbidezAdc;
    e["ph_volts"] = estado.phVolts; e["turbidez_volts"] = estado.turbidezVolts; e["referencia"] = estado.referencia;
    e["leu"] = estado.leu; e["leitura_ms"] = estado.leituraMs; e["medicao_valida"] = estado.medicaoValida;
    e["pendentes"] = estado.pendentes; e["capacidade"] = estado.capacidade;
    e["fila_saudavel"] = estado.filaSaudavel; e["horario"] = estado.horario;
    e["http"] = estado.http; e["enviando"] = estado.enviando; e["confirmou"] = estado.confirmou;
    String json; json.reserve(1100); serializeJson(doc, json);
    cabecalho(cliente, "application/json; charset=utf-8");
    json.remove(json.length() - 1);
    cliente.print(json); cliente.print(",\"logs\":[");
    bool separador = false;
    LogDiagnostico<>::Linha linha;
    uint32_t inicio = cursor + 1;
    if (inicio < primeiro) inicio = primeiro;
    for (uint32_t id = inicio; id && id <= ultimo && cliente.connected(); ++id) {
      if (!console.linha(id, linha)) continue;
      StaticJsonDocument<512> item;
      item["id"] = linha.id; item["ms"] = linha.ms; item["texto"] = linha.texto;
      json = ""; serializeJson(item, json);
      if (separador) cliente.print(',');
      cliente.print(json); separador = true;
    }
    cliente.print("]}");
  }
  void atender(WiFiClient& cliente) {
    char pedido[1536]; size_t n = 0;
    const uint32_t inicio = millis();
    bool completo = false;
    while (cliente.connected() && millis() - inicio < 1000 && n + 1 < sizeof(pedido)) {
      if (!cliente.available()) { vTaskDelay(1); continue; }
      pedido[n++] = static_cast<char>(cliente.read()); pedido[n] = '\0';
      if (n >= 4 && !memcmp(pedido + n - 4, "\r\n\r\n", 4)) { completo = true; break; }
    }
    if (!completo) { cliente.print("HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n"); return; }
    if (strncmp(pedido, "GET ", 4)) { cliente.print("HTTP/1.0 405 Method Not Allowed\r\nConnection: close\r\n\r\n"); return; }
    char* url = pedido + 4;
    char* fim = strchr(url, ' ');
    if (!fim) return;
    *fim = '\0';
    if (!strcmp(url, "/")) {
      cliente.printf("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Encoding: gzip\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
          static_cast<unsigned>(sizeof(PAGINA_DIAGNOSTICO_GZIP)));
      cliente.write(PAGINA_DIAGNOSTICO_GZIP, sizeof(PAGINA_DIAGNOSTICO_GZIP));
    } else if (!strcmp(url, "/api/diagnostico") || !strncmp(url, "/api/diagnostico?", 17)) {
      responderEstado(cliente, url);
    } else cliente.print("HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\n");
  }
  static void tarefa(void* contexto) { static_cast<DiagnosticoLocal*>(contexto)->executar(); }
  void executar() {
    bool ativo = false;
    for (;;) {
      if (WiFi.status() == WL_CONNECTED) {
        if (!ativo) {
          servidor.begin();
          console.printf("[DIAGNOSTICO] Acesse http://%s/ na mesma rede Wi-Fi\r\n", WiFi.localIP().toString().c_str());
          ativo = true;
        }
        WiFiClient cliente = servidor.available();
        if (cliente) { cliente.setTimeout(300); atender(cliente); cliente.stop(); }
      } else if (ativo) { servidor.end(); ativo = false; }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
 public:
  DiagnosticoLocal(ConsoleDiagnostico& console, const char* equipamento) : console(console), equipamento(equipamento) {}
  bool iniciar() {
    sessao = esp_random();
    return xTaskCreate(tarefa, "diagnostico", 8192, this, 1, nullptr) == pdPASS;
  }
};
