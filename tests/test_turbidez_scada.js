// node tests/test_turbidez_scada.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const pasta = path.join(__dirname, '../SCADA');
const html = fs.readFileSync(path.join(pasta, 'index.html'), 'utf8');
const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]));
const elementos = new Map();
const document = {getElementById(id) {
    assert(ids.has(id), `ID ausente do HTML: ${id}`);
    if (!elementos.has(id)) {
        const classes = new Set();
        elementos.set(id, {textContent: '', title: '', classList: {
            add: (...nomes) => nomes.forEach(n => classes.add(n)),
            remove: (...nomes) => nomes.forEach(n => classes.delete(n)),
            contains: nome => classes.has(nome)
        }});
    }
    return elementos.get(id);
}};
const contexto = vm.createContext({document});
const codigo = fs.readFileSync(path.join(pasta, 'script.js'), 'utf8');
// Funções reais, sem iniciar rede, tema ou eventos de navegador.
vm.runInContext(codigo.slice(0, codigo.lastIndexOf('inicializarTema();')), contexto);
const base = {ph: 7, temperatura: 25, turbidez: 2.5, turbidez_unidade: 'ntu',
    tds: null, created_at: '2026-11-08T12:00:00-03:00'};

contexto.atualizarCards(base);
assert.equal(document.getElementById('valorTds').textContent, '--');
assert.equal(document.getElementById('valorTurbidez').textContent, '2.5 NTU');
contexto.atualizarCards({...base, tds: 50, tds_status: 'abaixo_50_visual'});
assert.equal(document.getElementById('valorTds').textContent, '< 50 ppm');
assert(document.getElementById('valorTds').title.includes('Faixa visual'));
contexto.verificarAlarmes({...base, tds: 50, tds_status: 'abaixo_50_visual'});
assert(document.getElementById('alertaSistema').classList.contains('oculto'));
contexto.atualizarCards({...base, tds: 50, tds_status: 'medido'});
assert.equal(document.getElementById('valorTds').textContent, '50 ppm');
assert.equal(document.getElementById('valorTds').title, '');
assert.equal(contexto.formatarTds({...base, tds_status: 'indisponivel'}), '--');
assert.equal(contexto.formatarTds({...base, tds: 123, tds_status: 'medido'}), '123 ppm');

// O cartão usa NTU fixo, conforme a interface integrada da main.
const demo = {...base, turbidez_unidade: 'ntu_demonstrativo'};
contexto.atualizarCards(demo);
assert.equal(document.getElementById('valorTurbidez').textContent, '2.5 NTU');
assert(document.getElementById('valorTurbidez').title.includes('demonstração'));
assert.equal(contexto.formatarTurbidezHistorico(demo), '2.5 NTU (demonstração)');
assert.equal(contexto.formatarTurbidezHistorico(base), '2.5 NTU');
const relativa = {...base, turbidez: 75, turbidez_unidade: 'percentual_relativo'};
contexto.atualizarCards(relativa);
assert.equal(document.getElementById('valorTurbidez').textContent, '--');
assert(document.getElementById('valorTurbidez').title.includes('percentual'));
assert.equal(contexto.formatarTurbidezHistorico(relativa), '75.0 % relativo');

contexto.verificarAlarmes({...base, turbidez: 51});
assert(!document.getElementById('alertaSistema').classList.contains('oculto'));
assert(document.getElementById('alertaSistema').textContent.includes('Turbidez'));
contexto.verificarAlarmes({...base, tds: 301});
assert(document.getElementById('alertaSistema').textContent.includes('TDS'));
contexto.verificarAlarmes({...base, tds: 49});
assert(document.getElementById('alertaSistema').textContent.includes('TDS'));
contexto.verificarAlarmes({...base, tds: 130});
assert(document.getElementById('alertaSistema').classList.contains('oculto'));
contexto.limparLeituras();
assert.equal(document.getElementById('valorTds').textContent, '--');
assert.equal(document.getElementById('valorTds').title, '');
assert.equal(document.getElementById('valorTurbidez').title, '');
console.log('SCADA integrado: TDS, NTU, registros legados, alertas e compatibilidade com HTML verificados.');
