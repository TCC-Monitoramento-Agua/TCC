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
contexto.limparLeituras();
assert.equal(document.getElementById("unidadeTurbidez").textContent, "--");
console.log("SCADA: unidades, troca de equipamento, TDS ausente e alertas verificados.");
