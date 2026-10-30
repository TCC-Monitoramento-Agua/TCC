# Monitoramento da qualidade da água de reservatórios

Projeto de TCC para aquisição automática de pH, temperatura, turbidez e TDS com ESP32, envio em JSON para uma API Flask, armazenamento em MySQL e visualização em uma interface web SCADA.

Este guia descreve a implementação atual e os objetivos do documento **“lucasbalintvilar_Projeto_Fisico_assinado - DVK.pdf”**. O PDF foi fornecido como referência do projeto e não está incluído neste repositório. Funcionalidades previstas no documento não devem ser consideradas concluídas apenas por constarem na proposta.

## Estado atual

| Parte | Implementação |
|---|---|
| Wokwi | DS18B20 para temperatura; potenciômetros representam pH, turbidez e TDS. Não reproduz calibração química de sensores reais. |
| API e banco | Recepção, validação básica, MySQL, identificação do equipamento, UUID por medição e proteção contra duplicação. |
| SCADA | Cards da leitura mais recente por horário de medição, últimos 10 registros, identificação do equipamento e alertas com limites provisórios. Sem gráficos temporais nesta versão. |
| Falhas de envio | Fila LittleFS de 1.000 leituras, reenvio em ordem de coleta e reconexão Wi-Fi. Há limites e testes pendentes descritos abaixo. |
| Protótipo físico | pH com dois pontos confirmados, DS18B20 real e turbidez relativa em percentual; TDS temporariamente desativado (`null`). Conferir pinagem e publicação da API/SCADA em [PROTOTIPO_FISICO.md](docs/PROTOTIPO_FISICO.md). |

```text
Sensores / circuito Wokwi → ESP32 → API Flask → MySQL
                              ↓         ↑        ↓
                         fila local     └──── SCADA web
```

O SCADA consulta a API; não acessa o MySQL diretamente. Os serviços utilizados pelo projeto são Render para API e Aiven para MySQL, mas é possível configurar outros servidores.

## Estrutura

```text
backend/api_python/
  main.py                     API Flask e migração do schema
  requirements.txt            Dependências Python e PlatformIO
SCADA/
  index.html, script.js,
  style.css                   Interface web
database/schema.sql          Schema para banco novo
firmware/esp32/
  src/main.cpp                Firmware base da simulação
  diagram.json, wokwi.toml     Circuito e configuração Wokwi
  platformio.ini              Compilação ESP32
  preparar_wokwi.py            Cria, amplia e atualiza instâncias
  gerar_imagem.py              Gera firmware completo para Wokwi
  include/                    Fila persistente compartilhada
  prototipo/                  Projeto PlatformIO do protótipo físico
  .wokwi-instances/            Instâncias geradas, ignoradas pelo Git
  DEDUPLICACAO.md              Teste de reenvio sem duplicação
  FILA_LOCAL.md               Testes da fila e suas limitações
```

## Pré-requisitos

- Git, Python **3.10 ou superior** e acesso à internet para instalar dependências.
- VS Code com as extensões [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) e [Wokwi Simulator](https://docs.wokwi.com/vscode/getting-started), para simulação.
- Wokwi for VS Code ativado conforme os requisitos de licença/acesso da extensão e da sua conta.
- Comandos `pio` e `code` disponíveis no terminal, se usar as opções `--compilar` e `--abrir`. Execute `pio --version` e `code --version` para conferir. No macOS, o comando `code` pode precisar ser instalado pelo menu de comandos do VS Code.
- Para hospedar sua API: um MySQL acessível, banco já criado, credenciais e, quando exigido pelo provedor, certificado CA.
- Para o protótipo: ESP32 compatível com `esp32dev`, flash de 4 MB, cabo de dados USB e driver serial apropriado.

As dependências não estão fixadas por versão no repositório. Problemas de instalação/compilação devem ser registrados com as versões utilizadas.

## 1. Obter o projeto e instalar dependências

Este guia acompanha a versão publicada na branch `main`. A branch `b.lucasdxl` é usada para desenvolvimento; confira as diferenças antes de utilizar outra branch.

```sh
git clone --branch main https://github.com/TCC-Monitoramento-Agua/TCC.git
cd TCC
python -m venv .venv
```

No Windows/PowerShell:

```powershell
.\.venv\Scripts\Activate.ps1
```

No Linux/macOS:

```sh
source .venv/bin/activate
```

Use `python3` no lugar de `python` se esse for o nome instalado no seu sistema. Com o ambiente ativado:

```sh
python -m pip install --upgrade pip
python -m pip install -r backend/api_python/requirements.txt
```

No Windows, instale também a base de fusos utilizada pelo `zoneinfo`:

```powershell
python -m pip install tzdata
```

Os comandos seguintes partem da **raiz `TCC`**, salvo indicação explícita. Cada novo terminal deve usar o ambiente Python apropriado. Se a ativação PowerShell estiver bloqueada, use o executável `.venv\Scripts\python.exe` diretamente para comandos Python; para o gerador compilar, abra um terminal com `pio` no PATH, como o terminal do PlatformIO.

## 2. Escolher a API que será utilizada

| Cenário | Configuração |
|---|---|
| Simular com o serviço existente | Firmware e SCADA já apontam para `https://api-monitoramento-agua.onrender.com/leituras`. O serviço precisa estar disponível e atualizado. Leituras serão gravadas no banco ligado a ele. |
| Executar API e SCADA localmente | Siga a seção seguinte; configure o SCADA para `http://127.0.0.1:5000/leituras`. |
| Simular com sua própria API | Publique a API, depois ajuste `serverUrl` no firmware e `API_URL` no SCADA para seu endereço. |

**`localhost` dentro do ESP32 simulado não é o computador que hospeda sua API.** A ligação Wokwi → API local precisa de um gateway/encaminhamento compatível com a configuração e o acesso disponíveis no Wokwi; não é configurada por este repositório. O procedimento básico de simulação abaixo usa uma API acessível pela internet. A API e o SCADA podem ser testados localmente sem o Wokwi.

## 3. Executar sua API localmente

### Banco e configuração

Crie o banco MySQL, por exemplo pelo MySQL Workbench:

```sql
CREATE DATABASE IF NOT EXISTS monitoramento_agua
  CHARACTER SET utf8mb4;
```

Crie manualmente **`backend/api_python/.env`** com suas configurações. Não existe `.env.example` neste repositório.

```dotenv
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=seu_usuario
DB_PASSWORD=sua_senha
DB_NAME=monitoramento_agua
DB_CONNECT_TIMEOUT=10
PORT=5000
```

Para Aiven ou outro banco remoto, use host, porta, usuário, senha e nome de banco informados pelo provedor; não presuma uma porta fixa. O usuário precisa de permissões para criar/alterar a tabela e seus índices, inserir e consultar leituras. O `.env` é ignorado pelo Git e não deve conter credenciais compartilhadas no README.

### TLS na conexão com MySQL

Quando o provedor exigir certificado, configure caminhos para arquivos existentes:

```dotenv
DB_SSL_CA=/caminho/para/ca.pem
DB_SSL_VERIFY=true
```

No Windows, pode usar `C:/certificados/ca.pem`. `DB_SSL_CERT` e `DB_SSL_KEY` são opcionais para servidores que exigem certificado de cliente. **`DB_SSL_REQUIRED` não é lida pelo código atual** e não configura TLS. Sem caminhos de certificados, o código deixa as opções SSL a cargo do comportamento padrão do conector/servidor; não presume conexão verificada automaticamente.

### Iniciar e verificar

```sh
python backend/api_python/main.py
```

Mantenha esse terminal aberto. Em outro terminal, verifique:

```sh
curl http://127.0.0.1:5000/status
```

No PowerShell, use:

```powershell
Invoke-RestMethod http://127.0.0.1:5000/status
```

Resultado esperado:

```json
{"status":"ok","api":"online","database":"conectado"}
```

A API cria a tabela `leituras` e acrescenta colunas/índice necessários a bancos existentes. **Não cria o banco definido em `DB_NAME`.** O script [database/schema.sql](database/schema.sql) também pode inicializar um banco novo; `CREATE TABLE IF NOT EXISTS` sozinho não migra tabelas antigas. Leituras antigas sem UUID permanecem aceitas/preservadas.

### Testar a primeira gravação sem Wokwi

Exemplo PowerShell para a API local:

```powershell
$leitura = @{
  equipamento_id = "teste-manual"
  leitura_id = [guid]::NewGuid().ToString()
  ph = 7.5
  turbidez = 2.3
  temperatura = 22.5
  tds = 450.0
  data_hora = "2026-10-08T10:00:00-03:00"
} | ConvertTo-Json
Invoke-RestMethod -Uri "http://127.0.0.1:5000/leituras" -Method Post -ContentType "application/json" -Body $leitura
```

A primeira resposta deve indicar `duplicada: false`. Reexecute apenas o último comando, mantendo `$leitura`: deve retornar o mesmo `id` e `duplicada: true`. Uma nova coleta deve receber outro UUID. Essa medição é um dado de teste e ficará no banco usado.

## 4. Abrir o SCADA

Edite `API_URL`, na primeira linha de [SCADA/script.js](SCADA/script.js), para a API escolhida. Para execução local:

```javascript
const API_URL = "http://127.0.0.1:5000/leituras";
```

Em outro terminal, na raiz:

```sh
python -m http.server 5500 --directory SCADA
```

Abra **http://127.0.0.1:5500** no navegador. Para a API existente no Render, mantenha o endereço original no JavaScript. Em uma publicação HTTPS do SCADA, a API também deve ser HTTPS, evitando bloqueio de conteúdo misto.

O SCADA mostra a leitura mais recente por `data_hora`, com `id` como desempate, e os últimos 10 registros de todos os equipamentos. Não há seleção de equipamento nem gráficos nesta versão. Uma consulta bem-sucedida não comprova que o ESP32 continua coletando: confira também a data da última leitura.

A atualização ocorre **5 segundos após terminar cada consulta**, com timeout de 60 segundos. O comentário e a mensagem de erro no JavaScript ainda mencionam 15 segundos, mas o valor executado é `INTERVALO_ATUALIZACAO = 5000`. A interface usa o fuso do dispositivo do visitante.

## 5. Iniciar uma simulação Wokwi

Antes de gerar as instâncias, confira `serverUrl` em [firmware/esp32/src/main.cpp](firmware/esp32/src/main.cpp). Ele deve apontar para uma API acessível ao simulador e que implemente o UUID/deduplicação desta versão.

Na raiz, com `pio` e `code` no PATH:

```sh
python firmware/esp32/preparar_wokwi.py --quantidade 1 --atualizar-firmware --compilar --abrir
```

O script prepara `firmware/esp32/.wokwi-instances/esp32-001`, copia os headers da fila, compila e abre a pasta em uma janela do VS Code. Ele **não inicia o Wokwi automaticamente**.

Nessa janela:

1. Confira que a extensão Wokwi está ativada.
2. Abra `diagram.json`.
3. Use o menu de comandos → **Wokwi: Start Simulator**.
4. Aguarde Wi-Fi e NTP; varie os potenciômetros e a temperatura do DS18B20 no circuito.
5. Confira o terminal serial e depois o SCADA conectado à mesma API.

Se não usar `--compilar`/`--abrir`, abra a pasta da instância manualmente e execute `pio run` no terminal dela. A compilação gera `firmware-merged.bin`, com bootloader, tabela padrão de partições e firmware; o Wokwi usa offset zero. Nenhuma imagem vazia de filesystem é incluída.

### Interpretar os logs

```text
[LEITURA] ...                         Valores e horário coletados
[LOCAL] SALVA NO ESP32                Guardada na flash, ainda não confirma banco
[ENVIO] Tentando enviar               Tentativa da pendência mais antiga
[BANCO] CONFIRMADO                    API confirmou gravação/registro existente
[LOCAL] Liberada da fila              Confirmação persistida localmente
[LOCAL] FILA VAZIA                    Nenhuma pendência naquele momento
[LOCAL] MANTIDA NO ESP32              Envio falhou; leitura continua guardada
```

`[LOCAL] FLASH VAZIA` significa que não havia dados anteriores nessa flash; não prova recuperação após reinício. `LOG_DETALHADO=1`, definido no topo do fonte antes dos includes, reativa detalhes por amostra e respostas completas. O padrão é 0.

### Mais instâncias e atualizações

```sh
# Total desejado de duas instâncias
python firmware/esp32/preparar_wokwi.py --quantidade 2 --compilar --abrir-todas

# Acrescentar duas ao grupo existente, sem apagar as anteriores
python firmware/esp32/preparar_wokwi.py --adicionar 2 --compilar --abrir
```

Cada instância tem ID `esp32-001`, `esp32-002` etc. e MAC próprio. O limite do gerador é 32, **não uma garantia de que seu computador/Wokwi sustenta 32 simulações**. Inicie cada uma manualmente e mantenha as abas visíveis. Instâncias fora do total solicitado não são apagadas nem paradas pelo script.

Após atualizar o repositório, pare as simulações e use `--atualizar-firmware` para aplicar a base aos fontes existentes. Sem essa opção, os fontes das instâncias são preservados.

- `src/main.cpp`: código compilado da instância.
- `src/main.cpp.bak`: cópia anterior, não compilada.
- Ajustes locais de URL, intervalos e opções de teste ficam no backup e precisam ser reaplicados após a atualização.

Configurações alteradas pelo gerador também podem ganhar backups. Não execute o gerador durante os testes de falha/recuperação: mantenha a mesma sessão aberta. Não há monitor central nem opção `--logs` na versão atual.

## 6. Intervalos, fila e proteção contra duplicação

| Configuração | Onde alterar | Valor atual |
|---|---|---|
| Espera após coleta | `SEND_INTERVAL`, em cada `main.cpp` | 10000 ms |
| Amostras analógicas | Simulação: `NUM_AMOSTRAS` / `TEMPO_ENTRE_AMOSTRAS`; físico: `SensoresFisicos.h` | Simulação: média de 10, 500 ms; físico: mediana de 31, 10 ms |
| Timeout HTTP | `HTTP_TIMEOUT_MS`, em cada firmware | 60000 ms |
| Timeout de conexão | `HTTP_CONNECT_TIMEOUT_MS`, em cada firmware | 15000 ms |
| Capacidade local | `CAPACIDADE_FILA`, em `include/FilaLocal.h` | 1.000 leituras |
| Reenvio após falha | `RETRY_INICIAL_MS` / `RETRY_MAXIMO_MS`, no mesmo header | 5 até 60 segundos |
| Envio de pendências | `ENVIOS_POR_CICLO`, no mesmo header | Até 2 por ciclo |
| Consulta SCADA | `INTERVALO_ATUALIZACAO`, em `SCADA/script.js` | 5000 ms após consulta |

Esses valores não representam um envio obrigatório a cada 10 segundos. Amostragem e POST somam tempo; os envios são bloqueantes e podem atrasar novas coletas. No Wokwi, tempo simulado e tempo real também podem diferir.

A fila cresce até 1.000 slots de 512 bytes: **512.000 bytes (500 KiB)** de registros, além dos metadados do LittleFS. Com intervalo efetivo de 10 segundos e fila inicialmente vazia, isso corresponde a aproximadamente **2 h 46 min 40 s** de leituras sem envio; é uma estimativa de armazenamento, não de bateria, e os POSTs podem atrasar a coleta. Cada leitura é salva antes do POST e só liberada após confirmação válida. Ao encher, preserva pendências e rejeita novas leituras, que podem ser perdidas. Esta versão transfere automaticamente as pendências da fila antiga de 20 posições, mantendo UUIDs e horários. Outras mudanças de capacidade exigem migração própria. Veja os testes, cálculos e referências em [FILA_LOCAL.md](firmware/esp32/FILA_LOCAL.md).

O `id` auto incrementado identifica a linha no MySQL. O `leitura_id` UUID identifica a medição desde o ESP32. A restrição única `(equipamento_id, leitura_id)` impede inserções duplicadas. Há tolerância numérica de 1e-6 ao comparar reenvios devido ao tipo FLOAT.

Após cada reinício, a coleta fica suspensa até o SNTP confirmar a sincronização do relógio. O firmware não lê os sensores nem gera uma nova medição enquanto isso não ocorrer. Antes de gravar na fila, também exige relógio válido e `data_hora` preenchida. Após sincronizar, continua coletando com o relógio local mesmo se perder Wi-Fi/API. Não há gravação sem horário nem reconstrução por uptime. Não há RTC externo nesta versão. Pendências sem horário criadas por versões anteriores são descartadas ao chegar à frente da fila, com aviso `[LOCAL] DESCARTADA`. As leituras com data são preservadas para envio normal.

Firmware usa NTP.br e fuso `BRT3`; a API guarda DATETIME no horário de Brasília e devolve ISO 8601 com offset. O SCADA converte esse instante para o fuso do visitante.

## 7. Endpoints e schema atual

| Endpoint | Resultado |
|---|---|
| `GET /` | Confirma que a API respondeu; não comprova banco conectado. |
| `GET /status` | Testa acesso ao MySQL; 200 ou 500. |
| `POST /leituras` | 201 para nova leitura; 200 para reenvio; 400 para dados inválidos; 409 para UUID com outros dados; 500 para erro de armazenamento. |
| `GET /leituras?limite=10` | Registros por `data_hora DESC, id DESC`; limite de 1 a 1000. |
| `GET /leituras?equipamento_id=esp32-001&limite=10` | Mesmo histórico, filtrado pelo equipamento. |

Resposta típica de gravação:

```json
{
  "status": "ok",
  "mensagem": "Leitura salva com sucesso",
  "id": 1,
  "equipamento_id": "esp32-001",
  "leitura_id": "700c6c86-0e25-48ca-b718-43dab12918b2",
  "duplicada": false
}
```

Campos da tabela: `id`, `equipamento_id`, `leitura_id`, `ph`, `turbidez`, `turbidez_unidade`, `temperatura`, `tds` e `data_hora`. pH, temperatura e um dos campos de entrada `turbidez` (NTU) ou `turbidez_relativa` (percentual) são obrigatórios; TDS pode ser NULL. UUID exige equipamento e horário. A identificação aceita letras acentuadas, como `físico_1`. Clientes sem UUID continuam aceitos, mas sem deduplicação. A validação atual não substitui calibração nem validação completa de limites físicos.

### Atualização do campo TDS

TDS representa o total de sólidos dissolvidos, em **ppm**, no firmware, na API, no banco e no SCADA. A faixa configurada no SCADA é de **50 a 300 ppm**, incluindo os extremos; valores abaixo de 50 ppm ou acima de 300 ppm geram alerta. Esses limites foram escolhidos para o projeto e não certificam potabilidade nem substituem a calibração dos sensores.

## 8. Publicar no Render

Configure um serviço web com:

- Repositório e **branch que contém a versão desejada**.
- Root Directory: `backend/api_python`.
- Build Command: `pip install -r requirements.txt`.
- Start Command: `gunicorn --bind 0.0.0.0:$PORT main:app`.
- Variáveis `DB_HOST`, `DB_PORT`, `DB_USER`, `DB_PASSWORD`, `DB_NAME` e as opções TLS necessárias.
- Para CA/certificados, disponibilize os arquivos no serviço e use seus caminhos reais em `DB_SSL_CA`, `DB_SSL_CERT` e `DB_SSL_KEY`.

Gunicorn é utilizado no servidor Linux; no Windows use o procedimento de desenvolvimento com Python. `python main.py` habilita debug e não é o comando de produção.

Configure Auto-Deploy conforme as opções do Render e seus filtros de caminhos. Um commit em `b.lucasdxl` não publica um serviço configurado para `main`. Confira o commit do deploy e `/status` antes de iniciar firmware novo.

Após publicar sua API, ajuste as URLs do firmware e do SCADA. O CORS atual é aberto e o firmware usa `setInsecure()` no HTTPS, sem verificar o certificado do servidor. Autenticação e verificação de certificados precisam ser tratadas antes de considerar a solução pronta para operação real.

## 9. Protótipo físico

A implementação física ainda está parcial. Configure `ssid`, `password`, `serverUrl` e o identificador `EQUIPAMENTO_ID` em `firmware/esp32/prototipo/src/main.cpp` antes de usar sua placa. Quando o ID está vazio, usa o MAC. Não reutilize um ID de equipamento em dispositivos distintos.

Na raiz:

```sh
pio run -d firmware/esp32/prototipo
pio run -d firmware/esp32/prototipo --target upload
pio device monitor --baud 115200
```

Se houver várias portas, informe a porta correta com `--upload-port` no upload e `--port` no monitor. A configuração atual do físico usa **pH no GPIO 34**, **turbidez no GPIO 32** e **DS18B20 no GPIO 14**, conforme o código de teste enviado pelo usuário. Essa pinagem de turbidez difere do esquema anterior (GPIO 35); confira a ligação antes de gravar.

O pH utiliza os pontos confirmados de **1,57 V → pH 7** e **1,95 V → pH 4**, na escala `ADC / 4095 × 3,3` usada na calibração, já após o divisor. Os analógicos usam a mediana de 31 amostras, com 10 ms entre amostras. A temperatura é lida no DS18B20, sem valor fixo.

O TDS está temporariamente desativado nas novas aquisições e é enviado como `null`, aparecendo como `--` no SCADA. Os registros históricos e as simulações são preservados.

A turbidez física utiliza a **queda percentual do sinal**, com referência inicial `ADC_AGUA_REFERENCIA = 2709` do teste fornecido; não é NTU nem calibração com padrão conhecido. O JSON usa `turbidez_relativa`, e a API registra sua unidade. O SCADA exibe `% relativo`, sem aplicar o alerta de 50 NTU a essa grandeza. A curva genérica anterior foi removida do fluxo físico.

**Publicar a API e o SCADA atualizados antes de usar este firmware.** A API adiciona `turbidez_unidade` à tabela existente, preservando as linhas. Uma API antiga rejeita o novo payload relativo, em vez de registrá-lo como NTU; a leitura continua na fila. Detalhes, fórmulas, logs e testes estão em [PROTOTIPO_FISICO.md](docs/PROTOTIPO_FISICO.md).

A fila de 1.000 registros e a coleta condicionada ao horário sincronizado permanecem compartilhadas com a simulação. Se um sensor ativo tiver leitura inválida, a nova medição completa é recusada; as pendências continuam sendo processadas. Não execute `erase`/`uploadfs` com pendências.

### Logs locais sem USB

O físico disponibiliza **`http://IP_DO_ESP32/`** com leituras, ADC/tensão, situação da fila, resultado dos envios e logs de aplicação. O endereço aparece na serial como `[DIAGNOSTICO] Acesse http://.../`; anote-o após a gravação e acesse pelo celular na mesma rede. Funciona com alimentação pela bateria e sem depender da API. São mantidas as últimas 80 linhas em RAM, com atualização automática, pausa e download do texto exibido. A página roda em tarefa separada para atender durante os POSTs. O acesso depende da comunicação permitida pela rede/hotspot. Detalhes e limitações em [PROTOTIPO_FISICO.md](docs/PROTOTIPO_FISICO.md#diagnóstico-local-pelo-celular).

## 10. Validação alinhada ao documento do TCC

As seções de testes e análise de riscos do PDF (páginas 35–37) preveem falhas de comunicação, preservação de leituras, dados inválidos, operação contínua e comparação dos sensores com referências.

| Teste | Critério de aceitação |
|---|---|
| Caminho normal | `[BANCO] CONFIRMADO`, registro consistente no banco e leitura correspondente no SCADA. |
| Reenvio do mesmo UUID | Uma linha no banco e retorno do mesmo `id`; 409 se alterar valores/horário mantendo o UUID. |
| Rede/API indisponível | Pendências preservadas até a capacidade; ao recuperar, envio dos mesmos UUIDs/valores/horários e fila esvaziada. |
| Banco indisponível | API informa falha; ESP32 não trata como confirmação nem libera o registro. |
| Fila cheia | Alerta explícito, pendências antigas não sobrescritas; novas leituras rejeitadas são reconhecidas como perda. |
| Reinício com pendências | Recuperar registros na flash preservada; exige validação física e não pode ser comprovado por uma nova sessão Wokwi vazia. |
| Sensores físicos | Comparação com referência e margem de erro definida segundo os sensores escolhidos; ainda pendente. |
| Operação contínua | Registrar duração, atrasos, falhas e perdas; limites aceitáveis ainda precisam ser definidos para o ensaio. |

Procedimentos completos: [deduplicação](firmware/esp32/DEDUPLICACAO.md) e [fila local](firmware/esp32/FILA_LOCAL.md). Comece os testes de queda após NTP sincronizar e **não pare/recompile o Wokwi durante a interrupção**.

Continuam pendentes a validação física desta integração, a conferência da referência relativa de turbidez, a recuperação/reintegração do TDS, gráficos históricos e ensaios físicos de energia e funcionamento prolongado. Várias simulações simultâneas têm apresentado falhas TLS intermitentes; aumentar a quantidade de Wokwis não é, sozinho, uma medição do limite da API.

## Problemas comuns

| Sintoma | Verificação |
|---|---|
| Script Python não encontrado | Volte à raiz com `cd (git rev-parse --show-toplevel)` no PowerShell. |
| `pio`/`code` não encontrado | Confira o PATH no terminal usado; abra o terminal PlatformIO ou configure o comando do VS Code. |
| Binário não encontrado no Wokwi | Compile na pasta da instância; confira `firmware-merged.bin` e `wokwi.toml`. |
| Instância executa código antigo | Use `--atualizar-firmware`, recompile e reinicie; apenas `git pull` não atualiza fontes gerados. |
| `ZoneInfoNotFoundError` | No Windows instale `tzdata`; no Linux confira a base de fusos do sistema. |
| API não conecta ao MySQL | Confira banco existente, host, porta, credenciais, CA e permissões; teste `/status`. |
| HTTP 503 | O servidor respondeu indisponibilidade; confira o serviço/deploy no Render. |
| HTTP -11 | Timeout ao receber dados; examine tempos e logs da API. |
| TLS -80 / HTTP -1 | Falha de conexão HTTPS; o texto genérico “connection refused” não identifica a causa sozinho. |
| Flash vazia após reiniciar Wokwi | Não há pendências recuperáveis nessa sessão; não confunda com recuperação física validada. |
| SCADA não muda | Confira a URL, console do navegador, resposta da API e horário da última medição; recarregue após editar JavaScript. |

## Autores

- João Henrique Tomaz Dutra
- Lucas Balint Vilar

Professor orientador: Marcelo do Carmo Camargo Gaiotto.
