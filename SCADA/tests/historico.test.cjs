const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function dashboard() {
    const elements = new Map();
    const context = {
        console: { log() {}, error() {} }, AbortController, Date,
        setTimeout() { return 1; }, clearTimeout() {},
        document: { getElementById(id) {
            if (!elements.has(id)) elements.set(id, {
                textContent: '', classList: { add() {}, remove() {} }
            });
            return elements.get(id);
        } }
    };
    vm.createContext(context);
    const source = fs.readFileSync(path.join(__dirname, '../script.js'), 'utf8')
        .replace(/buscarDadosDaApi\(\);\s*$/, '');
    vm.runInContext(source, context);
    // Captura o estado entregue à tabela; cards e alarmes executam normalmente.
    vm.runInContext('atualizarTabelaHistorico = () => { snapshot = JSON.stringify(historico); };', context);
    return {
        elements,
        async consultar(leituras) {
            context.fetch = async () => ({ ok: true, json: async () => ({ status: 'ok', leituras }) });
            await context.buscarDadosDaApi();
            return JSON.parse(context.snapshot);
        }
    };
}

function leitura(id, equipamento_id, extra = {}) {
    return { id, equipamento_id, ph: 7.5, turbidez: 2, temperatura: 25,
             orp: 300, data_hora: '2026-10-06T18:45:19-03:00', ...extra };
}

test('exibe todas as leituras recebidas, de equipamentos diferentes, na ordem da API', async () => {
    const app = dashboard();
    const rows = await app.consultar([leitura(4, 'esp32-001'), leitura(3, 'esp32-002'),
                                     leitura(2, 'esp32-001'), leitura(1, 'esp32-002')]);
    assert.deepEqual(rows.map(x => x.id), [4, 3, 2, 1]);
    assert.deepEqual(rows.map(x => x.equipamento_id), ['esp32-001', 'esp32-002', 'esp32-001', 'esp32-002']);
    assert.equal(app.elements.get('equipamentoLeitura').textContent, 'esp32-001');
    assert.equal((await app.consultar([leitura(4, 'esp32-001'), leitura(3, 'esp32-002')])).length, 2);
});

test('substitui dados antigos após truncate, mesmo quando IDs são reutilizados', async () => {
    const app = dashboard();
    await app.consultar([leitura(2, 'esp32-001'), leitura(1, 'esp32-001')]);
    const rows = await app.consultar([leitura(2, 'esp32-002', { ph: 8 }), leitura(1, 'esp32-002')]);
    assert.equal(rows.length, 2);
    assert.equal(rows[0].equipamento_id, 'esp32-002');
    assert.equal(rows[0].ph, 8);
});

test('banco vazio limpa tabela e cards sem marcar API como offline', async () => {
    const app = dashboard();
    await app.consultar([leitura(1, 'esp32-001')]);
    assert.deepEqual(await app.consultar([]), []);
    assert.equal(app.elements.get('valorPh').textContent, '--');
    assert.equal(app.elements.get('equipamentoLeitura').textContent, '--');
    assert.equal(app.elements.get('statusConexao').textContent, 'Online');
});

test('limita a tabela a dez registros e trata ORP ausente como sem leitura', async () => {
    const app = dashboard();
    const rows = await app.consultar(Array.from({ length: 12 }, (_, i) => leitura(12-i, 'esp32-001', { orp: null })));
    assert.equal(rows.length, 10);
    assert.equal(rows[0].orp, null);
    assert.equal(app.elements.get('valorOrp').textContent, '--');
});


test('cards usam leitura mais recente no tempo, mesmo com ID menor', async () => {
    const app = dashboard();
    const rows = await app.consultar([
        leitura(1, 'esp32-001', { ph: 8, data_hora: '2026-10-06T19:00:00-03:00' }),
        leitura(2, 'esp32-002', { ph: 7, data_hora: '2026-10-06T18:00:00-03:00' })
    ]);
    assert.deepEqual(rows.map(x => x.id), [1, 2]);
    assert.equal(app.elements.get('equipamentoLeitura').textContent, 'esp32-001');
    assert.equal(app.elements.get('valorPh').textContent, '8.00');
});
