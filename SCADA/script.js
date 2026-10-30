const API_URL = "https://api-monitoramento-agua.onrender.com/leituras?limite=100";

const INTERVALO_ATUALIZACAO = 5000; // 5 segundos após concluir a consulta
const TEMPO_LIMITE_REQUISICAO = 60000; // Até 60 segundos para receber os dados

let todasAsLeituras = [];
let equipamentoSelecionado = "todos"; // "todos" ou ID específico do equipamento (ex: "1")
let primeiraConexao = true;


/*
====================================================
BUSCA OS DADOS DA API
====================================================
*/
async function buscarDadosDaApi() {
    const controlador = new AbortController();
    const timeout = setTimeout(
        () => controlador.abort(),
        TEMPO_LIMITE_REQUISICAO
    );

    try {
        if (primeiraConexao) {
            alterarStatusConexao("Conectando...", "conectando");
        }
        limparErro();
        const resposta = await fetch(API_URL, {
            method: "GET",
            headers: {
                "Accept": "application/json"
            },
            cache: "no-store",
            signal: controlador.signal
        });

        if (!resposta.ok) {
            throw new Error(
                `Erro HTTP: ${resposta.status}`
            );
        }
        const dados = await resposta.json();

        console.log("Resposta completa da API:", dados);

        if (dados.status === "erro") {
            throw new Error(
                dados.message || "A API retornou um erro."
            );
        }

        if (!Array.isArray(dados.leituras)) {
            throw new Error("A API retornou uma lista de leituras inválida.");
        }

        // Armazena todas as leituras retornadas
        todasAsLeituras = dados.leituras.map((leituraAPI) => ({
            id: leituraAPI.id,
            equipamento_id: leituraAPI.equipamento_id,
            ph: Number(leituraAPI.ph),
            temperatura: Number(leituraAPI.temperatura),
            tds: leituraAPI.tds == null ? null : Number(leituraAPI.tds),
            turbidez: Number(leituraAPI.turbidez),
            turbidez_unidade: leituraAPI.turbidez_unidade || "ntu",
            created_at: leituraAPI.data_hora
        }));

        // Atualiza a lista do dropdown e renderiza a tela com o filtro ativo
        atualizarDropdownEquipamentos();
        aplicarFiltroTela();

        alterarStatusConexao("Online", "online");
        primeiraConexao = false;

    } catch (erro) {
        console.error(
            "Erro ao acessar API:",
            erro
        );

        alterarStatusConexao(
            "Offline",
            "offline"
        );

        document.getElementById(
            "statusLeitura"
        ).textContent =
            "Falha ao atualizar dados.";

        document.getElementById(
            "mensagemErro"
        ).textContent =
            erro.name === "AbortError"
                ? "A API demorou mais de 60 segundos para responder. Uma nova tentativa será feita em breve."
                : `Erro: ${erro.message}`;
    } finally {
        clearTimeout(timeout);
        setTimeout(buscarDadosDaApi, INTERVALO_ATUALIZACAO);
    }
}


/*
====================================================
APLICA O FILTRO DO EQUIPAMENTO NA TELA GERAL
====================================================
*/
function aplicarFiltroTela() {
    let leiturasFiltradas = [];

    if (equipamentoSelecionado === "todos") {
        leiturasFiltradas = todasAsLeituras;
    } else {
        leiturasFiltradas = todasAsLeituras.filter(
            (item) => String(item.equipamento_id) === String(equipamentoSelecionado)
        );
    }

    const spanEquipamento = document.getElementById("equipamentoLeitura");
    const badgeFiltro = document.getElementById("badgeFiltro");
    const chipEquipamento = document.getElementById("chipEquipamento");
    const descTabela = document.getElementById("descricaoTabela");
    const badgeContagem = document.getElementById("badgeContagemTabela");
    const btnLimpar = document.getElementById("btnLimparFiltro");

    // Caso não haja nenhuma leitura
    if (leiturasFiltradas.length === 0) {
        limparLeituras();

        if (todasAsLeituras.length === 0) {
            spanEquipamento.textContent = "--";
            document.getElementById("statusLeitura").textContent = "Nenhuma leitura disponível no banco.";
        } else {
            spanEquipamento.textContent = String(equipamentoSelecionado);
            document.getElementById("statusLeitura").textContent = `Nenhum dado recente para o Equipamento ${equipamentoSelecionado}.`;
        }

        if (equipamentoSelecionado === "todos") {
            badgeFiltro.textContent = "Todos ▾";
            chipEquipamento.classList.remove("filtrado");
            if (btnLimpar) btnLimpar.classList.add("oculto");
            if (badgeContagem) badgeContagem.textContent = "0 registros";
            if (descTabela) descTabela.textContent = "Últimas 10 medições consolidadas no banco de dados";
        } else {
            badgeFiltro.textContent = `ID: ${equipamentoSelecionado} ▾`;
            chipEquipamento.classList.add("filtrado");
            if (btnLimpar) btnLimpar.classList.remove("oculto");
            if (badgeContagem) badgeContagem.textContent = `Equipamento ${equipamentoSelecionado} (0 registros)`;
            if (descTabela) descTabela.textContent = `Exibindo medições exclusivas do Equipamento ${equipamentoSelecionado}`;
        }

        atualizarTabelaHistorico([]);
        return;
    }

    // Leitura mais recente para os cards e painéis
    const leituraMaisRecente = leiturasFiltradas[0];
    atualizarCards(leituraMaisRecente);
    verificarAlarmes(leituraMaisRecente);

    // Atualiza metadados do chip de Equipamento
    if (equipamentoSelecionado === "todos") {
        spanEquipamento.textContent = `${leituraMaisRecente.equipamento_id != null ? leituraMaisRecente.equipamento_id : "1"} (Todos)`;
        badgeFiltro.textContent = "Todos ▾";
        chipEquipamento.classList.remove("filtrado");
        chipEquipamento.title = "Clique para filtrar por equipamento (Exibindo todos os nós)";

        document.getElementById("statusLeitura").textContent = "Dados atualizados com sucesso.";

        if (descTabela) descTabela.textContent = "Últimas 10 medições consolidadas no banco de dados";
        if (badgeContagem) badgeContagem.textContent = `${Math.min(leiturasFiltradas.length, 10)} registros (Todos)`;
        if (btnLimpar) btnLimpar.classList.add("oculto");
    } else {
        spanEquipamento.textContent = String(equipamentoSelecionado);
        badgeFiltro.textContent = `ID: ${equipamentoSelecionado} ▾`;
        chipEquipamento.classList.add("filtrado");
        chipEquipamento.title = `Filtro ativo: Equipamento ${equipamentoSelecionado}. Clique para alterar`;

        document.getElementById("statusLeitura").textContent = `Filtrado por Equipamento ${equipamentoSelecionado} (${leiturasFiltradas.length} medição(ões)).`;

        if (descTabela) descTabela.textContent = `Exibindo medições exclusivas do Equipamento ${equipamentoSelecionado}`;
        if (badgeContagem) badgeContagem.textContent = `Equipamento ${equipamentoSelecionado} (${Math.min(leiturasFiltradas.length, 10)} registros)`;
        if (btnLimpar) btnLimpar.classList.remove("oculto");
    }

    // Atualiza a tabela histórica apenas com as leituras filtradas (máximo 10)
    atualizarTabelaHistorico(leiturasFiltradas.slice(0, 10));
}


/*
====================================================
DROPDOWN DE EQUIPAMENTOS
====================================================
*/
function atualizarDropdownEquipamentos() {
    const listaContainer = document.getElementById("listaOpcoesEquipamentos");
    if (!listaContainer) return;

    // Identifica todos os equipamentos distintos e calcula quantas leituras cada um tem
    const mapaEquipamentos = new Map();
    todasAsLeituras.forEach((item) => {
        const id = item.equipamento_id != null ? String(item.equipamento_id) : "Sem ID";
        if (!mapaEquipamentos.has(id)) {
            mapaEquipamentos.set(id, {
                total: 0,
                ultimaData: item.created_at
            });
        }
        const info = mapaEquipamentos.get(id);
        info.total += 1;
    });

    listaContainer.innerHTML = "";

    // Opção 1: Todos os Equipamentos
    const itemTodos = document.createElement("div");
    const estaAtivoTodos = equipamentoSelecionado === "todos";
    itemTodos.className = `dropdown-item ${estaAtivoTodos ? "ativo" : ""}`;
    itemTodos.setAttribute("role", "option");
    itemTodos.setAttribute("aria-selected", estaAtivoTodos);
    itemTodos.innerHTML = `
        <div class="dropdown-item-esquerda">
            <span class="dropdown-item-icone">🌐</span>
            <div class="dropdown-item-texto">
                <span class="dropdown-item-titulo">Todos os Equipamentos</span>
                <span class="dropdown-item-sub">${todasAsLeituras.length} leitura(s) registradas</span>
            </div>
        </div>
        ${estaAtivoTodos ? '<span class="dropdown-item-check">✓</span>' : ""}
    `;
    itemTodos.addEventListener("click", (evento) => {
        evento.stopPropagation();
        selecionarEquipamento("todos");
    });
    listaContainer.appendChild(itemTodos);

    // Opções para cada ID de equipamento encontrado
    mapaEquipamentos.forEach((info, id) => {
        const itemEq = document.createElement("div");
        const estaAtivo = String(equipamentoSelecionado) === id;
        itemEq.className = `dropdown-item ${estaAtivo ? "ativo" : ""}`;
        itemEq.setAttribute("role", "option");
        itemEq.setAttribute("aria-selected", estaAtivo);
        itemEq.innerHTML = `
            <div class="dropdown-item-esquerda">
                <span class="dropdown-item-icone">📟</span>
                <div class="dropdown-item-texto">
                    <span class="dropdown-item-titulo">Equipamento ${id}</span>
                    <span class="dropdown-item-sub">${info.total} medições • Última: ${formatarData(info.ultimaData)}</span>
                </div>
            </div>
            ${estaAtivo ? '<span class="dropdown-item-check">✓</span>' : ""}
        `;
        itemEq.addEventListener("click", (evento) => {
            evento.stopPropagation();
            selecionarEquipamento(id);
        });
        listaContainer.appendChild(itemEq);
    });
}

function selecionarEquipamento(id) {
    equipamentoSelecionado = id;
    fecharDropdown();
    atualizarDropdownEquipamentos();
    aplicarFiltroTela();
}

function toggleDropdown(evento) {
    if (evento) evento.stopPropagation();
    const dropdown = document.getElementById("dropdownEquipamentos");
    if (!dropdown) return;

    if (dropdown.classList.contains("oculto")) {
        abrirDropdown();
    } else {
        fecharDropdown();
    }
}

function abrirDropdown() {
    const dropdown = document.getElementById("dropdownEquipamentos");
    const chip = document.getElementById("chipEquipamento");
    if (!dropdown || !chip) return;
    const painel = chip.closest(".painel");
    dropdown.classList.remove("oculto");
    chip.classList.add("aberto");
    if (painel) painel.classList.add("aberto-dropdown");
    chip.setAttribute("aria-expanded", "true");
}

function fecharDropdown() {
    const dropdown = document.getElementById("dropdownEquipamentos");
    const chip = document.getElementById("chipEquipamento");
    if (!dropdown || !chip) return;
    const painel = chip.closest(".painel");
    dropdown.classList.add("oculto");
    chip.classList.remove("aberto");
    if (painel) painel.classList.remove("aberto-dropdown");
    chip.setAttribute("aria-expanded", "false");
}

function inicializarControlesFiltro() {
    const chip = document.getElementById("chipEquipamento");
    if (chip) {
        chip.addEventListener("click", toggleDropdown);
        chip.addEventListener("keydown", (evento) => {
            if (evento.key === "Enter" || evento.key === " ") {
                evento.preventDefault();
                toggleDropdown();
            }
        });
    }

    const btnLimpar = document.getElementById("btnLimparFiltro");
    if (btnLimpar) {
        btnLimpar.addEventListener("click", () => {
            selecionarEquipamento("todos");
        });
    }

    // Fecha o dropdown ao clicar fora ou apertar Escape
    document.addEventListener("click", (evento) => {
        const chipEl = document.getElementById("chipEquipamento");
        if (chipEl && !chipEl.contains(evento.target)) {
            fecharDropdown();
        }
    });

    document.addEventListener("keydown", (evento) => {
        if (evento.key === "Escape") {
            fecharDropdown();
        }
    });
}


/*
====================================================
ATUALIZA OS CARDS
====================================================
*/
function turbidezEhRelativa(dados) {
    return dados.turbidez_unidade === "percentual_relativo";
}

function formatarTurbidez(dados) {
    return `${dados.turbidez.toFixed(1)} ${turbidezEhRelativa(dados) ? "% relativo" : "NTU"}`;
}

function atualizarCards(dados) {
    document.getElementById(
        "valorPh"
    ).textContent =
        dados.ph.toFixed(2);

    document.getElementById(
        "valorTemperatura"
    ).textContent =
        `${dados.temperatura.toFixed(1)} °C`;

    document.getElementById(
        "valorTds"
    ).textContent =
        dados.tds == null ? "--" : `${dados.tds.toFixed(0)} ppm`;

    document.getElementById(
        "valorTurbidez"
    ).textContent =
        formatarTurbidez(dados);
    document.getElementById("unidadeTurbidez").textContent = turbidezEhRelativa(dados) ? "% RELATIVO" : "NTU";
    document.getElementById("faixaTurbidez").textContent = turbidezEhRelativa(dados)
        ? "Queda do sinal em relação à amostra de referência; sem escala NTU."
        : "Limite configurado: ≤ 50.0 NTU";

    document.getElementById(
        "dataLeitura"
    ).textContent =
        formatarData(dados.created_at);
}


/*
====================================================
VERIFICA ALARMES
====================================================
*/
function verificarAlarmes(dados) {
    const alerta =
        document.getElementById(
            "alertaSistema"
        );

    let mensagens = [];

    if (dados.ph < 6.5) {
        mensagens.push(
            "pH abaixo do ideal. Água muito ácida."
        );
    }

    if (dados.ph > 8.5) {
        mensagens.push(
            "pH acima do ideal. Água muito básica."
        );
    }

    if (
        dados.temperatura < 20 ||
        dados.temperatura > 30
    ) {
        mensagens.push(
            "Temperatura fora da faixa recomendada."
        );
    }

    if (dados.tds != null && dados.tds < 50) {
        mensagens.push(
            "TDS abaixo da faixa configurada."
        );
    }

    if (dados.tds != null && dados.tds > 300) {
        mensagens.push(
            "TDS acima da faixa configurada."
        );
    }

    if (!turbidezEhRelativa(dados) && dados.turbidez > 50) {
        mensagens.push(
            "Turbidez elevada. Água com excesso de partículas."
        );
    }

    if (mensagens.length > 0) {
        alerta.classList.remove(
            "oculto"
        );
        alerta.textContent =
            mensagens.join(" ");
    } else {
        alerta.classList.add(
            "oculto"
        );
    }
}


/*
====================================================
HISTÓRICO
====================================================
*/
function limparLeituras() {
    for (const id of ["valorPh", "valorTemperatura", "valorTds",
                      "valorTurbidez", "dataLeitura"]) {
        document.getElementById(id).textContent = "--";
    }
    const alerta = document.getElementById("alertaSistema");
    alerta.textContent = "";
    alerta.classList.add("oculto");
    document.getElementById("unidadeTurbidez").textContent = "--";
    document.getElementById("faixaTurbidez").textContent = "Aguardando leitura.";
}


/*
====================================================
ATUALIZA TABELA
====================================================
*/
function atualizarTabelaHistorico(listaLeituras) {
    const tabela =
        document.getElementById(
            "tabelaHistorico"
        );

    tabela.innerHTML = "";

    if (!listaLeituras || listaLeituras.length === 0) {
        tabela.innerHTML = `<tr><td colspan="6" style="text-align: center; color: var(--texto-suave); padding: 32px 16px; font-style: italic;">Nenhuma leitura encontrada para o filtro selecionado.</td></tr>`;
        return;
    }

    listaLeituras.forEach(
        (item) => {
            const linha =
                document.createElement(
                    "tr"
                );

            const equipamento = document.createElement("td");
            equipamento.textContent = item.equipamento_id || "Não identificado";
            linha.appendChild(equipamento);
            const valores = document.createElement("tr");
            valores.innerHTML = `
                <td>
                    ${formatarData(item.created_at)}
                </td>

                <td>
                    ${item.ph.toFixed(2)}
                </td>

                <td>
                    ${item.temperatura.toFixed(1)} °C
                </td>

                <td>
                    ${item.tds == null ? "--" : `${item.tds.toFixed(0)} ppm`}
                </td>

                <td>
                    ${formatarTurbidez(item)}
                </td>
            `;

            while (valores.firstChild) {
                linha.appendChild(valores.firstChild);
            }
            tabela.appendChild(linha);
        }
    );
}


/*
====================================================
STATUS DA CONEXÃO
====================================================
*/
function alterarStatusConexao(
    texto,
    classe
) {
    const status =
        document.getElementById(
            "statusConexao"
        );

    status.textContent = texto;

    status.classList.remove(
        "online",
        "offline",
        "conectando"
    );

    status.classList.add(
        classe
    );
}


/*
====================================================
LIMPA ERRO
====================================================
*/
function limparErro() {
    document.getElementById(
        "mensagemErro"
    ).textContent = "";
}


/*
====================================================
FORMATA DATA
====================================================
*/
function formatarData(data) {
    const dataObj =
        new Date(data);

    if (
        isNaN(
            dataObj.getTime()
        )
    ) {
        return "--";
    }

    return dataObj.toLocaleString("pt-BR");
}


/*
====================================================
CONTROLE DE TEMA (DARK / LIGHT)
====================================================
*/
function inicializarTema() {
    const temaSalvo = localStorage.getItem("scada-theme") || "dark";
    document.documentElement.setAttribute("data-theme", temaSalvo);

    const btnTheme = document.getElementById("themeToggle");
    if (btnTheme) {
        btnTheme.addEventListener("click", () => {
            const temaAtual = document.documentElement.getAttribute("data-theme") || "dark";
            const novoTema = temaAtual === "dark" ? "light" : "dark";
            document.documentElement.setAttribute("data-theme", novoTema);
            localStorage.setItem("scada-theme", novoTema);
        });
    }
}


/*
====================================================
INICIALIZAÇÃO
====================================================
*/
inicializarTema();
inicializarControlesFiltro();

// Faz a primeira requisição imediatamente
buscarDadosDaApi();
