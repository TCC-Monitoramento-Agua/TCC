# Fila local persistente

Os dois firmwares usam a mesma fila LittleFS em `include/`. Capacidade: **1.000 medições**, em `CAPACIDADE_FILA` de `include/FilaLocal.h`. Cada slot ocupa 512 bytes; o arquivo cresce conforme a coleta, até **512.000 bytes (500 KiB)**, além dos metadados do sistema de arquivos. Não há pré-alocação do arquivo inteiro, tarefa paralela, botão ou mudança no circuito elétrico.

Coleta → UUID/horário/valores → gravação verificada na flash → POST → confirmação da API → ACK local persistido. Um POST sem confirmação mantém a leitura. A fila envia primeiro a mais antiga. Resposta perdida permite reenvio com o mesmo UUID; a API evita duplicação. Falha de ACK local também permite reenvio após recuperação. CRC detecta registros inválidos; a fila suspende operação sem formatar dados existentes.

Até dois POSTs confirmados por ciclo para reduzir as pendências mesmo com novas coletas; após uma falha, o lote é interrompido. Há espera progressiva de 5 até 60 segundos após falha; ao confirmar, o próximo pendente pode ser enviado após 250 ms. A coleta segue durante essas esperas e com Wi-Fi indisponível. Como esta versão não usa tarefas paralelas, o POST em andamento ainda bloqueia a coleta até retornar/expirar. Não garante aquisição a intervalo fixo durante um timeout. `SEND_INTERVAL = 10000` continua editável nos dois `main.cpp`; é a espera após finalizar a coleta, e chamadas HTTP podem atrasar o próximo ciclo. A autonomia de armazenamento depende do intervalo efetivo e da quantidade de pendências existentes.

Ao encher, a fila preserva as 1.000 pendências, rejeita novas medições e registra alerta: essas novas medições **podem ser perdidas**. Ao recuperar comunicação, o espaço volta a ser liberado e reutilizado. Uma falha elétrica durante escrita pode provocar registro inválido e exigir recuperação; testes nativos não substituem testes físicos de energia.

## Dimensionamento e autonomia de armazenamento

O `static_assert` em `include/FilaCore.h` verifica que cada `RegistroFlash` ocupa exatamente 512 bytes. O espaço dos registros é `capacidade × 512`; a duração nominal de uma fila inicialmente vazia, sem envios, é `capacidade × intervalo entre leituras`.

| Capacidade | Espaço dos registros | Intervalo efetivo de 10 s |
| --- | --- | --- |
| 20 (versão anterior) | 10.240 bytes = 10 KiB | 3 min 20 s |
| 500 (comparação) | 256.000 bytes = 250 KiB | 1 h 23 min 20 s |
| 1.000 (atual) | 512.000 bytes = 500 KiB | 2 h 46 min 40 s |

Esses valores são cálculos de projeto, não duração medida nem autonomia de bateria. O instante exato da primeira coleta depende de NTP/inicialização e de quando a interrupção começa; a duração é uma aproximação por quantidade de amostras. Com pendências prévias, usar `(1.000 − pendentes) × intervalo efetivo`. Alterar o intervalo muda a autonomia. Bloqueios de aquisição/HTTP e diferenças entre tempo simulado e real impedem garantir coleta a cada 10 segundos.

A configuração padrão `default.csv` do Arduino-ESP32 3.3.12, usada no ambiente de compilação verificado, reserva `0x160000` bytes (**1.441.792 bytes = 1.375 KiB**) à partição de dados chamada `spiffs`, montada como LittleFS. Os 500 KiB de registros ocupam aproximadamente 35,5% dessa partição; o restante inclui metadados, outros arquivos e espaço necessário ao funcionamento do filesystem. A tabela real gravada no dispositivo deve coincidir com a configuração compilada. Não se altera a tabela de partições nesta mudança.

### Fontes para o relatório

O tamanho de 512 bytes e a capacidade de 1.000 são características **desta implementação**, verificáveis no código; a duração é calculada pelos autores. A documentação da Espressif fundamenta a divisão da flash em partições, e seu arquivo de configuração fornece a alocação usada no cálculo:

- ESPRESSIF SYSTEMS. **Partition Tables**. ESP-IDF Programming Guide. Disponível em: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html>. Acesso em: 29 out. 2026. Texto-fonte consultado em <https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/partition-tables.rst> (a página renderizada estava bloqueada neste ambiente).
- ESPRESSIF SYSTEMS. **default.csv**. Arduino-ESP32, versão 3.3.12. Disponível em: <https://github.com/espressif/arduino-esp32/blob/3.3.12/tools/partitions/default.csv>. Acesso em: 29 out. 2026.

Não atribuir à Espressif uma autonomia de 2 h 46 min 40 s: essa é uma estimativa da fila deste projeto. Se houver outras obras da mesma autoria/data na bibliografia, ajustar a diferenciação das citações conforme o padrão adotado.

## Atualização da fila antiga de 20 para 1.000 registros

O anel usa a capacidade no cálculo da posição de cada sequência. Trocar apenas a constante reinterpretaria incorretamente registros antigos após a primeira volta da fila. Por isso esta versão usa os arquivos `/fila-v2.bin` e `/fila-v2-ack.bin`, com uma configuração persistida que registra a capacidade e impede abri-los com outro tamanho.

No primeiro boot, se existem os arquivos legados `/fila-local.bin` e `/fila-local-ack.bin`, são validados como uma fila de 20 posições. Somente as pendências são copiadas na ordem original; os UUIDs, horários e valores permanecem idênticos. Apenas a sequência interna local é renumerada. O ACK novo é publicado por rename atômico após a cópia verificada, seguido da configuração persistida, antes de liberar a fila. Uma cópia interrompida pode ser retomada no próximo boot sem modificar os arquivos legados.

Os arquivos legados ficam preservados, mas não voltam a ser importados depois que a fila v2 foi preparada. Ausência ou corrupção de arquivos ativos suspende a fila em vez de restaurar dados antigos ou apagar pendências. A atualização não requer `erase`, `uploadfs`, alteração das partições ou limpeza do banco. Outras mudanças de capacidade exigem migração própria.

Horário é obrigatório: após cada boot, o firmware aguarda a confirmação de sincronização do SNTP antes de ler os sensores ou criar medições. O timeout inicial de conexão não libera a coleta; o loop continua aguardando o relógio sincronizar. A gravação na fila também recusa relógio inválido e `data_hora` ausente/vazia. Após sincronizar, a perda de Wi-Fi/API não interrompe o relógio local nem a coleta. Não há mais armazenamento por boot/uptime, reconstrução posterior de horário ou arquivos de revisão. Conforme autorizado para esta correção, pendências sem horário de versões anteriores são descartadas ao chegar à frente da fila, com aviso explícito `[LOCAL] DESCARTADA`. A remoção persiste o avanço do ACK local; não simula uma confirmação do banco. As leituras com data permanecem na ordem normal de envio. Se o ACK falhar, o descarte não avança e a fila é suspensa. Nenhuma data é inventada. Comece os testes de indisponibilidade depois de confirmar a sincronização NTP.

## Preparação no Wokwi

Confirme que a API com proteção contra duplicação já está publicada no Render. Pare os simuladores e rode na raiz:

```powershell
git pull origin b.lucasdxl
python firmware/esp32/preparar_wokwi.py --quantidade 1 --atualizar-firmware --compilar --abrir
```

Use uma instância primeiro. O fonte anterior fica em backup; ajustes locais de URL/intervalo precisam ser reaplicados. Deixe `TESTAR_DUPLICACAO` em 0. O gerador copia os headers, prepara `firmware-merged.bin` e o offset zero; a imagem inclui bootloader e a tabela **padrão** de partições, sem uma imagem de filesystem que apagaria a fila. No físico a partição padrão também é mantida. Não execute `erase` ou `uploadfs` com pendências.

Antes de montar, uma partição completamente apagada é identificada como `[LOCAL] FLASH VAZIA` e preparada. Esse aviso significa que não há registros anteriores nessa flash. `[BOOT]` mostra o motivo de reset informado pelo ESP32; POWERON sozinho não identifica por que o Wokwi iniciou outra execução. Uma partição não vazia inválida não é formatada automaticamente.

## Testes manuais (sem parar ou recompilar durante a interrupção)

### 1. Normal

Inicie Wokwi e espere NTP sincronizar. Confira:

```text
[LOCAL] Fila pronta | recuperadas=0 | capacidade=1000
[LEITURA] ... valores e horário ...
[LOCAL] SALVA NO ESP32 | UUID=... | pendentes=1/1000
[ENVIO] Tentando enviar | UUID=... | pendentes=1
[BANCO] CONFIRMADO | UUID=... | id=... | HTTP=201 | duplicada=não
[LOCAL] Liberada da fila | UUID=... | pendentes=0
[LOCAL] FILA VAZIA: todas as leituras pendentes foram confirmadas pelo banco
```

Os detalhes por amostra e respostas completas ficam desligados; defina `LOG_DETALHADO=1` no topo do fonte para diagnóstico completo. Anote UUID e horário. No banco, deve haver uma linha para equipamento + UUID, com aqueles valores/horário. Se o POST anterior já foi salvo, HTTP 200 com `duplicada:true` é igualmente válido.

### 2. Queda da rede / Wi-Fi

Com horário já sincronizado, interrompa o acesso à rede usado pela simulação, sem pará-la. O Wokwi-GUEST é uma rede virtual: desligar o Wi-Fi do PC pode cortar o acesso externo **sem** mudar `WiFi.status()` dentro do ESP32. Nesse caso espere falhas HTTP, não necessariamente o aviso Wi-Fi desconectado. Se não puder cortar a rede dessa forma, use o teste da API.

Espere aparecerem pelo menos três medições salvas (não baseie o teste só em minutos reais, pois a simulação pode estar lenta). Anote UUIDs/horários nas linhas `[LEITURA]` e `[LOCAL] SALVA NO ESP32`. As pendências devem crescer e nenhuma deve ser confirmada durante a falha. Religue a rede: o número deve cair até zero e o banco deve receber as medições antigas antes das novas, sem duplicações.

### 3. API indisponível

Mantenha Wokwi e Wi-Fi ativos. Interrompa temporariamente a API pelo método que você utiliza no serviço. Não mude a URL no código, pois isso exigiria recompilar/reiniciar e prejudicaria o teste da fila.

Espere ao menos três pendências. HTTP 5xx, falha de conexão ou timeout não devem removê-las. Restabeleça a API e confira esvaziamento, UUIDs e horários originais no banco. O contador de pendências é o critério principal, não uma duração fixa de falha.

### 4. Banco indisponível (opcional)

Com API ativa, interrompa temporariamente seu acesso ao MySQL. Deve haver erro na API/HTTP 500 e a fila deve conservar as leituras. Restabeleça o banco e confira o mesmo resultado do teste anterior.

### 5. Fila cheia

Mantenha a comunicação interrompida até 1.000 pendências. A coleta seguinte deve mostrar erro de fila cheia; antigas não podem ser sobrescritas. Ao restabelecer o serviço, as 1.000 devem ser enviadas. Novas medições rejeitadas enquanto cheia não são recuperáveis. O preenchimento e a reutilização de slots também são cobertos pelo teste nativo abaixo, sem aguardar horas de simulação.

### 6. Reinício

Com pendências de horário válido, reinicie e confira `[LOCAL] Fila pronta | recuperadas=N`. A persistência de reset/nova sessão depende do Wokwi; nas sessões anteriores uma nova execução voltou com flash vazia. Se recuperar zero, esse teste **não comprova** preservação após desligamento. Validação física de energia continua pendente. Nunca use parar/reabrir como parte dos testes 2–4.

Para conferir cada leitura:

```sql
SELECT id, equipamento_id, leitura_id, data_hora, ph, turbidez, temperatura, tds
FROM leituras
WHERE equipamento_id = 'esp32-001'
  AND leitura_id = 'UUID_COPIADO_DO_LOG';
```

Deve haver uma linha, e o horário deve ser o da coleta, não o da reconexão. Guarde os logs desde a inicialização até o esvaziamento para identificar qualquer falha.

## Teste da exigência de horário

Na raiz, com um compilador C++ disponível:

```sh
g++ -std=c++11 -Wall -Wextra -pedantic tests/test_controle_horario.cpp -o /tmp/test_controle_horario
/tmp/test_controle_horario
```

O teste verifica bloqueio antes da notificação de sincronização (mesmo com um valor plausível no relógio), liberação após sincronizar, recusa de data ausente/vazia e de relógio inválido, exigência de uma nova sincronização após reiniciar, descarte seletivo sem pular a ordem da fila, manutenção de leituras datadas e falha no ACK de descarte. A notificação real do SNTP e a aquisição na placa devem ser verificadas no teste físico.

## Teste nativo da capacidade e atualização

```sh
g++ -std=c++11 -Wall -Wextra -Werror -Itests/fakes tests/test_fila_1000.cpp -o /tmp/test_fila_1000
/tmp/test_fila_1000
```

Executa o `FlashStorage` real com adaptadores de filesystem em memória. Verifica: preenchimento dos 1.000 slots, rejeição do registro excedente, recuperação após reinicialização, confirmação e reutilização de posições, bloqueio de capacidade incompatível, migração do anel antigo já reutilizado, UUID/horário/FIFO preservados e interrupções de escrita/rename durante a migração. Também verifica que um ACK ativo ausente ou um legado corrompido não provocam apagamento nem restauração de registros antigos. A execução não simula o comportamento elétrico nem a implementação interna do LittleFS no hardware.
