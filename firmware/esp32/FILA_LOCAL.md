# Fila local: primeira etapa

Os dois firmwares usam a mesma fila LittleFS em `include/`. Capacidade inicial: **20 medições**, em `CAPACIDADE_FILA` de `include/FilaLocal.h`. Cada slot ocupa 512 bytes; o arquivo cresce conforme a coleta, até 10.240 bytes. Não há pré-alocação de 1,5 MB, tarefa paralela, botão ou mudança no circuito elétrico.

Coleta → UUID/horário/valores → gravação verificada na flash → POST → confirmação da API → ACK local persistido. Um POST sem confirmação mantém a leitura. A fila envia primeiro a mais antiga. Resposta perdida permite reenvio com o mesmo UUID; a API evita duplicação. Falha de ACK local também permite reenvio após recuperação. CRC detecta registros inválidos; a fila suspende operação sem formatar dados existentes.

Um POST por tentativa, com espera progressiva de 5 até 60 segundos após falha; ao confirmar, o próximo pendente pode ser enviado após 250 ms. A coleta segue durante essas esperas e com Wi-Fi indisponível. Como esta versão não usa tarefas paralelas, o POST em andamento ainda bloqueia a coleta até retornar/expirar. Não garante aquisição a intervalo fixo durante um timeout. `SEND_INTERVAL = 10000` continua editável nos dois `main.cpp`; é a espera após finalizar a coleta, e chamadas HTTP podem atrasar o próximo ciclo. Vinte registros não representam 24 horas; autonomia depende do intervalo e dos atrasos.

Ao encher, a fila preserva as 20 pendências, rejeita novas medições e registra alerta: essas novas medições **podem ser perdidas**. Ao recuperar comunicação, o espaço volta a ser liberado. Não altere capacidade com pendências. Uma falha elétrica durante escrita pode provocar registro inválido e exigir recuperação; testes nativos não substituem testes físicos de energia.

Sem NTP na coleta, o registro conserva UUID, valores, boot e uptime. Quando NTP sincroniza no mesmo boot, o horário estimado é resolvido e persistido antes do POST, para que o reenvio seja idêntico. Se reiniciar antes de resolver esse horário, a pendência é preservada mas bloqueia o envio FIFO: não é possível reconstruir a hora real sem RTC ou tratamento explícito. Comece os testes de indisponibilidade depois de confirmar a sincronização NTP.

## Preparação no Wokwi

Confirme que a API com proteção contra duplicação já está publicada no Render. Pare os simuladores e rode na raiz:

```powershell
git pull origin b.lucasdxl
python firmware/esp32/preparar_wokwi.py --quantidade 1 --atualizar-firmware --compilar --abrir
```

Use uma instância primeiro. O fonte anterior fica em backup; ajustes locais de URL/intervalo precisam ser reaplicados. Deixe `TESTAR_DUPLICACAO` em 0. O gerador copia os headers, prepara `firmware-merged.bin` e o offset zero; a imagem inclui bootloader e a tabela **padrão** de partições, sem uma imagem de filesystem que apagaria a fila. No físico a partição padrão também é mantida. Não execute `erase` ou `uploadfs` com pendências.

A primeira montagem de uma partição vazia pode imprimir erros do LittleFS, seguida de formatação apenas se todos os bytes estiverem apagados. Espere `[FILA] Recuperadas ...` para avaliar o resultado. Uma partição não vazia inválida não é formatada automaticamente.

## Testes manuais (sem parar ou recompilar durante a interrupção)

### 1. Normal

Inicie Wokwi e espere NTP sincronizar. Confira:

```text
[FILA] Recuperadas 0 pendências; capacidade=20
[FILA] Medição salva na flash; pendentes=1/20
[FILA] Enviando pendência; [JSON] {...}
[HTTP] Codigo HTTP: 201
[FILA] Confirmada pela API; pendentes=0
```

Anote UUID e horário. No banco, deve haver uma linha para equipamento + UUID, com aqueles valores/horário. Se o POST anterior já foi salvo, HTTP 200 com `duplicada:true` é igualmente válido.

### 2. Queda da rede / Wi-Fi

Com horário já sincronizado, interrompa o acesso à rede usado pela simulação, sem pará-la. O Wokwi-GUEST é uma rede virtual: desligar o Wi-Fi do PC pode cortar o acesso externo **sem** mudar `WiFi.status()` dentro do ESP32. Nesse caso espere falhas HTTP, não necessariamente o aviso Wi-Fi desconectado. Se não puder cortar a rede dessa forma, use o teste da API.

Espere aparecerem pelo menos três medições salvas (não baseie o teste só em minutos reais, pois a simulação pode estar lenta). Anote UUIDs/horários nos JSONs de envio. As pendências devem crescer e nenhuma deve ser confirmada durante a falha. Religue a rede: o número deve cair até zero e o banco deve receber as medições antigas antes das novas, sem duplicações.

### 3. API indisponível

Mantenha Wokwi e Wi-Fi ativos. Interrompa temporariamente a API pelo método que você utiliza no serviço. Não mude a URL no código, pois isso exigiria recompilar/reiniciar e prejudicaria o teste da fila.

Espere ao menos três pendências. HTTP 5xx, falha de conexão ou timeout não devem removê-las. Restabeleça a API e confira esvaziamento, UUIDs e horários originais no banco. O contador de pendências é o critério principal, não uma duração fixa de falha.

### 4. Banco indisponível (opcional)

Com API ativa, interrompa temporariamente seu acesso ao MySQL. Deve haver erro na API/HTTP 500 e a fila deve conservar as leituras. Restabeleça o banco e confira o mesmo resultado do teste anterior.

### 5. Fila cheia

Mantenha a comunicação interrompida até 20 pendências. A coleta seguinte deve mostrar erro de fila cheia; antigas não podem ser sobrescritas. Ao restabelecer o serviço, as 20 devem ser enviadas. Novas medições rejeitadas enquanto cheia não são recuperáveis.

### 6. Reinício

Com pendências de horário válido, reinicie e confira `[FILA] Recuperadas N pendências`. A persistência de reset/nova sessão depende do Wokwi; nas sessões anteriores uma nova execução voltou com flash vazia. Se recuperar zero, esse teste **não comprova** preservação após desligamento. Validação física de energia continua pendente. Nunca use parar/reabrir como parte dos testes 2–4.

Para conferir cada leitura:

```sql
SELECT id, equipamento_id, leitura_id, data_hora, ph, turbidez, temperatura, orp
FROM leituras
WHERE equipamento_id = 'esp32-001'
  AND leitura_id = 'UUID_COPIADO_DO_LOG';
```

Deve haver uma linha, e o horário deve ser o da coleta, não o da reconexão. Guarde os logs desde a inicialização até o esvaziamento para identificar qualquer falha.
