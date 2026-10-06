const API_URL = "https://api-monitoramento-agua.onrender.com/leituras";

const INTERVALO_ATUALIZACAO = 15000; // 15 segundos após concluir a consulta
const TEMPO_LIMITE_REQUISICAO = 60000; // Até 60 segundos para receber os dados

let historico = [];
let ultimoIdProcessado = null;
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
        if(primeiraConexao) {
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

        // Mostra a resposta completa no console
        console.log("Resposta completa da API:", dados);

        if (dados.status === "erro") {
            throw new Error(
                dados.message || "A API retornou um erro."
            );
        }

        if (!dados.leituras || dados.leituras.length === 0) {
            throw new Error(
                "A API não retornou nenhuma leitura."
            );
        }

        /*
        A API retorna:
        {
            "leituras": [
                {...},
                {...},
                {...}
            ]
        }
        Como a API usa:
        ORDER BY id DESC
        a posição [0] é a leitura mais recente.
        */
        const leituraAPI = dados.leituras[0];

        console.log(
            "Leitura mais recente:",
            leituraAPI
        );

        /*
        Converte os valores recebidos para Number.
        */

        const leitura = {
            id: leituraAPI.id,
            equipamento_id: leituraAPI.equipamento_id,
            ph: Number(leituraAPI.ph),
            temperatura: Number(leituraAPI.temperatura),
            orp: Number(leituraAPI.orp),
            turbidez: Number(leituraAPI.turbidez),
            created_at: leituraAPI.data_hora
        };

        atualizarCards(leitura);
        verificarAlarmes(leitura);

        if (leitura.id !== ultimoIdProcessado) {
            adicionarNoHistorico(leitura);
            ultimoIdProcessado = leitura.id;
        }

        console.log(
            "Leitura processada:",
            leitura
        );

        alterarStatusConexao(
            "Online",
            "online"
        );

        primeiraConexao = false;

        document.getElementById(
            "statusLeitura"
        ).textContent =
            "Dados atualizados com sucesso.";

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
                ? "A API demorou mais de 60 segundos para responder. Uma nova tentativa será feita em 15 segundos."
                : `Erro: ${erro.message}`;
    } finally {
        clearTimeout(timeout);
        // Aguarda a consulta terminar para evitar requisições simultâneas.
        setTimeout(buscarDadosDaApi, INTERVALO_ATUALIZACAO);
    }
}

/*
====================================================
ATUALIZA OS CARDS
====================================================
*/
function atualizarCards(dados) {
    document.getElementById("equipamentoLeitura").textContent =
        dados.equipamento_id || "Não identificado";
    console.log(
        "Atualizando cards com:",
        dados
    );

    document.getElementById(
        "valorPh"
    ).textContent =
        dados.ph.toFixed(2);

    document.getElementById(
        "valorTemperatura"
    ).textContent =
        `${dados.temperatura.toFixed(1)} °C`;

    document.getElementById(
        "valorOrp"
    ).textContent =
        `${dados.orp.toFixed(0)} mV`;

    document.getElementById(
        "valorTurbidez"
    ).textContent =
        `${dados.turbidez.toFixed(1)} NTU`;

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

    /*
    Faixa de pH definida provisoriamente.
    Ajustaremos depois de acordo com
    os requisitos do projeto.
    */

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

    if (dados.orp < 250) {
        mensagens.push(
            "ORP baixo. Possível baixa capacidade de oxidação."
        );
    }

    if (dados.turbidez > 50) {
        mensagens.push(
            "Turbidez elevada. Água com excesso de partículas."
        );
    }

    /*
    Mostra os alarmes
    */

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
function adicionarNoHistorico(dados) {
    historico.unshift(dados);

    /*
    Mantém somente as últimas
    10 leituras.
    */

    if (historico.length > 10) {
        historico.pop();
    }

    atualizarTabelaHistorico();
}

/*
====================================================
ATUALIZA TABELA
====================================================
*/

function atualizarTabelaHistorico() {
    const tabela =
        document.getElementById(
            "tabelaHistorico"
        );

    tabela.innerHTML = "";
    historico.forEach(
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
                    ${item.orp.toFixed(0)} mV
                </td>

                <td>
                    ${item.turbidez.toFixed(1)} NTU
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

    // Usa automaticamente o fuso configurado no dispositivo do visitante.
    // O offset enviado pela API identifica o instante da leitura.
    return dataObj.toLocaleString("pt-BR");
}

/*
====================================================
INICIALIZAÇÃO
====================================================
*/

// Faz uma requisição imediatamente
buscarDadosDaApi();
