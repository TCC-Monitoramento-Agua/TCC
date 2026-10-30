# Protótipo físico — pH, temperatura e turbidez relativa

Esta versão integra os sensores do código de teste enviado pelo usuário. O TDS foi **temporariamente desativado** a pedido dele; não se conclui por software que o módulo esteja queimado.

## Ligações usadas nesta versão

| Sensor | GPIO | Observação |
| --- | --- | --- |
| PH-4502C | 34 | Saída analógica pelo divisor já utilizado na calibração. |
| Turbidez | **32** | Pinagem do código físico enviado, diferente do esquemático anterior (35). |
| DS18B20 | 14 | Alimentação 3,3 V; pull-up de 4,7 kΩ entre DATA e 3,3 V. |
| TDS | Não adquirido | `tds: null` nas novas medições; não lê nem configura um GPIO para TDS. |

Os módulos analógicos continuam alimentados em 5 V, com GND comum e os divisores existentes. Não ligar diretamente saídas de 5 V ao GPIO. O código enviado pelo colega definia pH e TDS ambos no GPIO 34 e imprimia um número de GPIO diferente do realmente lido; essa duplicidade foi removida desativando o TDS.

## pH: pontos efetivamente confirmados

O usuário confirmou as tensões **1,57 V para pH 7** e **1,95 V para pH 4**. São valores calculados no código de calibração por `ADC / 4095 × 3,3`, após o divisor, não a tensão reconstruída na saída do módulo. Para manter a calibração coerente, a implementação usa a mesma escala e não multiplica novamente pelo fator 1,6667.

```text
V = ADC × 3,3 / 4095
pH = 7 + (V − 1,57) × (4 − 7) / (1,95 − 1,57)
```

O arquivo de teste usava 4,01 na fórmula, enquanto o relato do usuário identificou a solução como pH 4; esta versão usa **4,00**. Se o valor atribuído à solução for de fato 4,01, ajustar `REFERENCIA_PH4` em `firmware/esp32/include/SensoresFisicos.h`. Os coeficientes se referem à mesma atenuação, divisor, ADC e ajuste físico do módulo; não transportá-los para outra escala de leitura sem nova conferência.

São lidas 31 amostras brutas, com 10 ms entre elas, e utilizada a mediana. A serial apresenta GPIO, ADC, tensão na escala de calibração e pH. Valores fora de 0–14, não finitos ou entrada saturada não são enviados como medição válida. A linearização com dois pontos não comprova a precisão em toda a escala.

## Temperatura

O firmware utiliza OneWire/DallasTemperature, inicializa o DS18B20 e aguarda a conversão antes de ler a temperatura. Leituras de sensor desconectado (`-127 °C`), não finitas ou fora de −55 a 125 °C são recusadas. Não usa mais o valor fixo de 22,5 °C. A leitura de 85 °C não é descartada automaticamente: após conversão, ela também pode representar uma temperatura real.

## Turbidez: queda relativa do sinal, sem unidade NTU

Foi removida do físico a curva genérica que gerava estimativas NTU incompatíveis com o módulo. A referência inicial vem do teste do colega: `ADC_AGUA_REFERENCIA = 2709`. Esse número deve ser conferido na montagem e amostra atuais; não representa um padrão certificado nem zero NTU.

```text
queda relativa (%) = máximo(0, mínimo(100, (2709 − ADC) / 2709 × 100))
```

É uma queda percentual do **sinal**, não percentual de partículas, pureza ou turbidez em NTU. Um sinal acima da referência aparece como queda zero, sem certificar água limpa. Utiliza a mediana de 31 leituras; entrada saturada é sinalizada e a nova medição é recusada. Não há zeragem automática no boot nem afirmação de calibração com soluções NTU.

O JSON usa **`turbidez_relativa`**, em vez de `turbidez`. A API armazena o valor e a unidade `percentual_relativo`; consultas retornam `turbidez` e `turbidez_unidade`. O SCADA apresenta `% relativo`, explica que é queda do sinal e não aplica o alerta de 50 NTU aos percentuais. Leituras NTU das simulações e antigas continuam identificadas como `ntu`.

## Publicação e operação

1. Atualizar/publicar **API e SCADA** antes de usar o novo firmware. A API acrescenta a coluna `turbidez_unidade` sem apagar registros; valores anteriores recebem `ntu`. Se a permissão de alteração do schema não estiver disponível, aplicar a adição pelo procedimento de administração do banco.
2. Conferir a pinagem, a configuração Wi-Fi e o ID do equipamento (`físico_1`, alterável por build flag). A API aceita letras acentuadas na identificação; o nome é preservado no banco e nos filtros do SCADA.
3. Compilar e gravar o projeto `firmware/esp32/prototipo` normalmente, **sem erase/uploadfs** para preservar a fila.
4. Após NTP, conferir `[PH]`, `[TEMPERATURA]`, `[TURBIDEZ]`, `[LEITURA]` e `[BANCO]` na serial.

Uma API antiga exige `turbidez` e rejeita o novo campo relativo com HTTP 400; assim não grava o percentual como se fosse NTU. O firmware mantém a pendência para nova tentativa e também confere a unidade na confirmação dos registros relativos. Após atualizar a API, pode reenviar os mesmos UUIDs.

A fila de 1.000 registros, as regras de horário e a deduplicação permanecem. Registros antigos na fila não são reescritos nem apagados. A desativação do TDS se aplica às novas aquisições; pendências históricas são preservadas.

Se um dos três sensores ativos produzir leitura inválida, a nova medição completa não é criada, pois a API exige pH, temperatura e turbidez. Os diagnósticos dos três continuam sendo impressos e o processamento das pendências não é interrompido. Isso evita inserir zeros ou valores fixos como se fossem medições reais.

## Diagnóstico local pelo celular

O firmware físico disponibiliza uma página em **`http://IP_DO_ESP32/`**, na porta 80. Após gravar esta versão, abra o monitor serial uma vez e anote a linha `[DIAGNOSTICO] Acesse http://.../`. Por exemplo, se o IP atribuído for `172.20.10.14`, o endereço é `http://172.20.10.14/`. O IP depende da rede e pode mudar ao reconectar; não foi acrescentada descoberta por nome `.local` para manter o firmware dentro da partição atual.

Depois, a página pode ser acessada com o dispositivo alimentado pela bateria, sem USB. O celular/computador precisa alcançar o ESP32 na mesma rede local. Hotspots podem restringir o acesso entre dispositivos; se o endereço não abrir, conferir o IP e testar em uma rede Wi-Fi que permita essa comunicação. A página não depende do Render, da API ou do SCADA para mostrar diagnósticos.

A tela apresenta pH, temperatura, turbidez relativa, ADCs, tensões na escala de calibração, referência de turbidez, idade/validade da última leitura, situação do relógio, integridade da fila, quantidade de pendências e resultado HTTP do envio. Sensor inválido aparece como indisponível, sem substituir pelo último valor válido. O código também passou a mostrar no log o corpo da resposta de erro da API, limitado a 300 caracteres.

Os logs de aplicação enviados ao `Serial` continuam saindo na UART e são copiados para um buffer circular em **RAM**, com as últimas **80 linhas**, de até 255 bytes cada. Linhas maiores são truncadas sem dividir um caractere UTF-8. O navegador consulta as novidades aproximadamente a cada segundo, mantém até 500 linhas na tela, detecta uma nova sessão após reinício e informa se registros antigos saíram do buffer. São disponibilizados pausa, limpeza somente da tela, rolagem automática e download do texto exibido.

Os logs não são gravados na flash e desaparecem ao desligar/reiniciar; não substituem a fila persistente. Mensagens internas de bibliotecas/SDK que não passam pelo `Serial` da aplicação e informações de falha anteriores ao reset não são prometidas pela página. Os controles não apagam a fila, não alteram calibração e não acionam sensores.

O servidor HTTP local roda em uma tarefa FreeRTOS separada do loop que realiza aquisição e POST. Isso evita que um timeout da API bloqueie o atendimento da página. Leituras/status são compartilhados com sincronização e publicados sem acessar o estado da fila diretamente pela tarefa HTTP. A primeira disponibilização ocorre após a tentativa inicial de conexão Wi-Fi; o serviço acompanha desconexões e reconexões.

Rotas locais, apenas para consulta: `GET /` (HTML) e `GET /api/diagnostico?apos=N&sessao=S` (estado e logs novos). O servidor não oferece operações de apagar arquivos ou alterar configuração. A implementação usa WiFiServer e mantém a tabela de partições existente; não requer `erase` nem `uploadfs`.

### Verificações da página e do buffer

```sh
g++ -std=c++11 -Wall -Wextra -Werror tests/test_log_diagnostico.cpp -o /tmp/test_log_diagnostico
/tmp/test_log_diagnostico
node tests/test_diagnostico_pagina.js
```

Os testes cobrem retenção e sobrescrita do buffer, fragmentos de linhas, limites e truncamento UTF-8; renderização dos dados, indisponibilidade de sensores, cursores sem duplicação, reinício, limite de linhas no navegador, pausa e falha de rede. O acesso a partir de celular e a resposta durante um timeout real da API precisam ser conferidos no dispositivo físico.

## Verificações da aquisição

```sh
g++ -std=c++11 -Wall -Wextra -Werror tests/test_sensores_fisicos.cpp -o /tmp/test_sensores_fisicos
/tmp/test_sensores_fisicos
node tests/test_turbidez_scada.js
pio run -d firmware/esp32/prototipo
```

Os testes com MySQL descartável em `tests/test_tds.py` incluem ida e volta de turbidez relativa, TDS nulo, deduplicação e conflito entre unidades, rejeição de percentuais inválidos e adição da unidade sem perda de leituras anteriores. As verificações de software não substituem ensaio com o hardware e as soluções de referência.
