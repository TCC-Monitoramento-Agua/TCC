// node tests/test_turbidez_scada.js
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");

const elementos = new Map();
const document = {
    getElementById(id) {
        if (!elementos.has(id)) {
            const classes = new Set();
            elementos.set(id, {
                textContent: "",
                classList: {
                    add: (...nomes) => nomes.forEach(n => classes.add(n)),
                    remove: (...nomes) => nomes.forEach(n => classes.delete(n)),
                    contains: nome => classes.has(nome)
                }
            });
        }
        return elementos.get(id);
    }
};
const contexto = vm.createContext({document});
const codigo = fs.readFileSync(path.join(__dirname, "../SCADA/script.js"), "utf8");
// Carrega as funções reais sem iniciar rede/tema/controles do navegador.
vm.runInContext(codigo.slice(0, codigo.lastIndexOf("inicializarTema();")), contexto);
const relativa = {
    ph: 7, temperatura: 25, turbidez: 75, turbidez_unidade: "percentual_relativo",
    tds: null, created_at: "2026-10-29T12:00:00-03:00"
};
contexto.atualizarCards(relativa);
assert.equal(document.getElementById("valorTds").textContent, "--");
contexto.atualizarCards({...relativa, tds: 50, tds_status: "abaixo_50_visual"});
assert.equal(document.getElementById("valorTds").textContent, "< 50 ppm");
assert(document.getElementById("valorTds").title.includes("Faixa visual"));
contexto.verificarAlarmes({...relativa, tds: 50, tds_status: "abaixo_50_visual"});
assert(document.getElementById("alertaSistema").classList.contains("oculto"));
assert.equal(contexto.formatarTds({...relativa, tds_status: "indisponivel"}), "--");
assert.equal(contexto.formatarTds({...relativa, tds: 123, tds_status: "medido"}), "123 ppm");
assert.equal(document.getElementById("valorTurbidez").textContent, "75.0 % relativo");
assert.equal(document.getElementById("unidadeTurbidez").textContent, "% RELATIVO");
contexto.verificarAlarmes(relativa);
assert(document.getElementById("alertaSistema").classList.contains("oculto"));

const ntu = {...relativa, turbidez: 51, turbidez_unidade: "ntu", tds: 135};
contexto.atualizarCards(ntu);
assert.equal(document.getElementById("valorTurbidez").textContent, "51.0 NTU");
assert.equal(document.getElementById("unidadeTurbidez").textContent, "NTU");
contexto.verificarAlarmes(ntu);
assert(!document.getElementById("alertaSistema").classList.contains("oculto"));
assert(document.getElementById("alertaSistema").textContent.includes("Turbidez"));
contexto.verificarAlarmes({...ntu, turbidez: 50});
assert(document.getElementById("alertaSistema").classList.contains("oculto"));
assert.equal(contexto.formatarTurbidez({turbidez: 2.3}), "2.3 NTU"); // API antiga/simulação.
const demo = {...ntu, turbidez: 2.5, turbidez_unidade: "ntu_demonstrativo"};
contexto.atualizarCards(demo);
assert.equal(document.getElementById("valorTurbidez").textContent, "2.5 NTU");
assert.equal(document.getElementById("unidadeTurbidez").textContent, "NTU");
assert(document.getElementById("faixaTurbidez").textContent.includes("demonstração"));
assert.equal(contexto.formatarTurbidezHistorico(demo), "2.5 NTU (demonstração)");
assert.equal(contexto.formatarTurbidezHistorico(ntu), "51.0 NTU");
contexto.verificarAlarmes({...demo, turbidez: 5});
assert(document.getElementById("alertaSistema").classList.contains("oculto"));
contexto.verificarAlarmes({...demo, turbidez: 5.1});
assert(document.getElementById("alertaSistema").textContent.includes("demonstração"));
contexto.atualizarCards(relativa); // Voltar ao histórico não muda sua unidade.
assert.equal(document.getElementById("valorTurbidez").textContent, "75.0 % relativo");
contexto.limparLeituras();
assert.equal(document.getElementById("unidadeTurbidez").textContent, "--");
assert.equal(document.getElementById("valorTds").title, "");
console.log("SCADA: unidades, troca de equipamento, TDS ausente e alertas verificados.");
