#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cmath>
#include <cstdlib>
#include "LogDiagnostico.h"

struct EstadoDiagnostico {
  float ph = NAN, temperatura = NAN, turbidez = NAN;
  uint16_t phAdc = 0, turbidezAdc = 0, referencia = 0;
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
               uint16_t turbidezAdc, float phVolts, float turbidezVolts, uint16_t referencia) {
    portENTER_CRITICAL(&mutex);
    estado.ph = ph; estado.temperatura = temperatura; estado.turbidez = turbidez;
    estado.phAdc = phAdc; estado.turbidezAdc = turbidezAdc;
    estado.phVolts = phVolts; estado.turbidezVolts = turbidezVolts;
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

static const char PAGINA_DIAGNOSTICO[] PROGMEM = R"html(<!doctype html>
<html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Diagnóstico ESP32</title><style>
*{box-sizing:border-box}body{margin:0;background:#101827;color:#e8eef8;font:16px system-ui,sans-serif}main{max-width:1050px;margin:auto;padding:20px}h1{font-size:24px;margin:0 0 8px}p{color:#b5c6df;line-height:1.5}.grade{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.cartao{background:#1b293f;padding:16px;border-radius:10px}.cartao strong{display:block;font-size:25px;margin:8px 0}.detalhe{color:#bbcae0;font-size:14px;white-space:pre-line}button{background:#23466f;color:white;border:1px solid #5681ab;padding:10px;border-radius:6px;cursor:pointer}nav{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:16px 0}pre{background:#080f1a;border:1px solid #344963;border-radius:8px;padding:12px;height:50vh;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.5 monospace}#conexao{font-weight:bold}.erro{color:#ffacac}.ok{color:#7ce6b3}
</style><main><h1>Diagnóstico local do ESP32</h1><p id="conexao">Conectando ao dispositivo…</p>
<div class="grade"><section class="cartao">pH<strong id="ph">--</strong><div class="detalhe" id="phInfo">Aguardando leitura</div></section>
<section class="cartao">Temperatura<strong id="temperatura">--</strong><div class="detalhe">DS18B20 · GPIO 14</div></section>
<section class="cartao">Turbidez relativa<strong id="turbidez">--</strong><div class="detalhe" id="turbidezInfo">Queda do sinal; não é NTU.</div></section>
<section class="cartao">Fila e envio<strong id="fila">--</strong><div class="detalhe" id="envio">Aguardando estado</div></section></div>
<p id="estado">Aguardando informações…</p><p>TDS desativado. Logs recentes ficam na RAM e são perdidos ao reiniciar. A página continua acessível durante tentativas de envio à API.</p>
<nav><button id="pausa">Pausar atualização</button><button id="limpar">Limpar tela</button><button id="baixar">Baixar logs</button><label><input type="checkbox" id="rolar" checked> Rolar automaticamente</label></nav>
<pre id="logs" aria-label="Logs do dispositivo"></pre></main>
<script>
const el=id=>document.getElementById(id);let cursor=0,sessao=null,pausado=false,linhas=[];
const numero=(v,casas=2)=>typeof v==='number'&&Number.isFinite(v)?v.toFixed(casas):'indisponível';
function renderizar(d){const e=d.estado;if(sessao!==null&&sessao!==d.sessao){linhas=['--- ESP32 reiniciado; nova sessão ---'];cursor=0}sessao=d.sessao;
el('ph').textContent=numero(e.ph);el('phInfo').textContent=e.leu?`GPIO 34 · ADC ${e.ph_adc}\nEscala da calibração: ${numero(e.ph_volts,3)} V`:'Aguardando leitura';
el('temperatura').textContent=numero(e.temperatura)+' °C';el('turbidez').textContent=numero(e.turbidez_relativa,1)+' %';
el('turbidezInfo').textContent=e.leu?`GPIO 32 · ADC ${e.turbidez_adc}\nEscala ADC: ${numero(e.turbidez_volts,3)} V\nReferência: ${e.referencia} ADC; não é NTU.`:'Aguardando leitura';
el('fila').textContent=`${e.pendentes} / ${e.capacidade}`;
el('envio').textContent=e.enviando?'Enviando à API…':e.http===0?'Nenhum envio concluído':`Último HTTP: ${e.http}\n${e.confirmou?'Gravação confirmada':'Sem confirmação; leitura mantida'}`;
const idade=e.leu?Math.floor((d.uptime_ms-e.leitura_ms)>>>0)/1000:null;
el('estado').textContent=`${d.equipamento} · IP ${d.ip} · Wi-Fi ${d.rssi} dBm · Relógio ${e.horario?'sincronizado':'aguardando NTP'} · Fila ${e.fila_saudavel?'pronta':'indisponível'} · ${idade===null?'Sem leitura':`Última leitura há ${idade.toFixed(0)} s (${e.medicao_valida?'válida':'inválida'})`}`;
if(d.perdidos)linhas.push('--- Algumas linhas antigas saíram do buffer do dispositivo ---');
for(const l of d.logs){if(l.id>cursor){linhas.push(`[${(l.ms/1000).toFixed(1)} s] ${l.texto}`);cursor=l.id}}
if(linhas.length>500)linhas=linhas.slice(-500);el('logs').textContent=linhas.join('\n');if(el('rolar').checked)el('logs').scrollTop=el('logs').scrollHeight;
el('conexao').textContent='Conectado ao ESP32 · atualização local';el('conexao').className='ok'}
async function atualizar(){if(!pausado){const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);try{const r=await fetch(`/api/diagnostico?apos=${cursor}&sessao=${sessao===null?'':sessao}`,{cache:'no-store',signal:c.signal});if(!r.ok)throw Error(`HTTP ${r.status}`);renderizar(await r.json())}catch(e){el('conexao').textContent='Sem resposta do ESP32. Confira o Wi-Fi e o endereço; dados abaixo podem estar antigos.';el('conexao').className='erro'}finally{clearTimeout(t)}}setTimeout(atualizar,1000)}
el('pausa').onclick=()=>{pausado=!pausado;el('pausa').textContent=pausado?'Retomar atualização':'Pausar atualização';if(pausado)el('conexao').textContent='Atualização pausada'};
el('limpar').onclick=()=>{linhas=[];el('logs').textContent=''};
el('baixar').onclick=()=>{const u=URL.createObjectURL(new Blob([linhas.join('\n')],{type:'text/plain;charset=utf-8'}));const a=document.createElement('a');a.href=u;a.download='logs-esp32.txt';a.click();setTimeout(()=>URL.revokeObjectURL(u),1000)};
atualizar();
</script></html>)html";

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
    e["ph"] = estado.ph; e["temperatura"] = estado.temperatura; e["turbidez_relativa"] = estado.turbidez;
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
      cabecalho(cliente, "text/html; charset=utf-8", sizeof(PAGINA_DIAGNOSTICO) - 1);
      cliente.write(reinterpret_cast<const uint8_t*>(PAGINA_DIAGNOSTICO), sizeof(PAGINA_DIAGNOSTICO) - 1);
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
