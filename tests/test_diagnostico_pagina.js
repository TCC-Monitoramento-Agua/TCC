// node tests/test_diagnostico_pagina.js
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const fonte = fs.readFileSync(path.join(__dirname, "../firmware/esp32/include/DiagnosticoLocal.h"), "utf8");
const script = fonte.match(/<script>([\s\S]*?)<\/script>/)[1];
const elementos = new Map();
const timers = [];
const document = {getElementById(id) {
    if (!elementos.has(id)) elementos.set(id, {textContent: "", className: "", checked: true, scrollHeight: 100});
    return elementos.get(id);
}};
const payload = {
    sessao: 10, uptime_ms: 20000, equipamento: "físico_1", ip: "192.168.1.15", rssi: -55,
    perdidos: false,
    estado: {
        ph: 7, temperatura: 22.5, turbidez_relativa: 2.5,
        ph_adc: 1948, turbidez_adc: 2641, ph_volts: 1.57, turbidez_volts: 2.128,
        referencia: 2709, leu: true, leitura_ms: 10000, medicao_valida: true,
        pendentes: 3, capacidade: 1000, fila_saudavel: true, horario: true,
        http: 400, enviando: false, confirmou: false
    },
    logs: [{id: 1, ms: 15000, texto: '[ENVIO] <erro> "requisição inválida"'}]
};
let resposta = payload, falhar = false, requisicao;
const contexto = vm.createContext({document, AbortController,
    setTimeout: (fn, ms) => {timers.push(ms); return timers.length;}, clearTimeout() {},
    fetch: async url => {requisicao = url; if (falhar) throw Error("sem rede"); return {ok: true, json: async () => resposta};}
});
vm.runInContext(script.slice(0, script.lastIndexOf("atualizar();")), contexto);
contexto.renderizar(payload);
assert.equal(document.getElementById("ph").textContent, "7.00");
assert.equal(document.getElementById("turbidez").textContent, "2.5 %");
assert.equal(document.getElementById("fila").textContent, "3 / 1000");
assert(document.getElementById("envio").textContent.includes("400"));
assert(document.getElementById("logs").textContent.includes('<erro> "requisição inválida"'));
contexto.renderizar(payload);
assert.equal(document.getElementById("logs").textContent.split("\n").length, 1); // Sem duplicação.

contexto.renderizar({...payload, sessao: 11, logs: [{id: 1, ms: 100, texto: "novo boot"}],
    estado: {...payload.estado, ph: null, temperatura: null, turbidez_relativa: null, medicao_valida: false}});
assert.equal(document.getElementById("ph").textContent, "indisponível");
assert(document.getElementById("logs").textContent.includes("reiniciado"));
assert(!document.getElementById("logs").textContent.includes("requisição inválida"));
contexto.renderizar({...payload, sessao: 11, logs: Array.from({length: 550}, (_, i) => ({id: i + 2, ms: i, texto: `linha ${i}`}))});
assert.equal(document.getElementById("logs").textContent.split("\n").length, 500);
document.getElementById("limpar").onclick();
assert.equal(document.getElementById("logs").textContent, "");

(async () => {
    resposta = {...payload, sessao: 11, logs: []};
    await contexto.atualizar();
    assert(requisicao.startsWith("/api/diagnostico?apos=551&sessao=11"));
    assert.equal(timers.at(-1), 1000);
    falhar = true;
    await contexto.atualizar();
    assert.equal(document.getElementById("conexao").className, "erro");
    assert.equal(document.getElementById("ph").textContent, "7.00"); // Preserva dados, marca desatualização.
    document.getElementById("pausa").onclick();
    assert.equal(document.getElementById("pausa").textContent, "Retomar atualização");
    console.log("Página local: leituras, logs, reinício, memória limitada, pausa e falha de rede verificados.");
})().catch(error => {console.error(error); process.exitCode = 1;});
