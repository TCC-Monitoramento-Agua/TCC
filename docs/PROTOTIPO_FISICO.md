# Protótipo físico — pH, temperatura, turbidez demonstrativa e TDS estimado

Esta versão integra os sensores do código de teste enviado pelo usuário. O TDS foi reativado no **GPIO35** após confirmação da ligação e observação de variação de tensão na saída do módulo. A suspeita anterior de avaria não foi confirmada. A aquisição reativada ainda precisa ser conferida na placa.

## Ligações usadas nesta versão

| Sensor | GPIO | Observação |
| --- | --- | --- |
| PH-4502C | 34 | Saída analógica pelo divisor já utilizado na calibração. |
| Turbidez | **32** | Pinagem do código físico enviado, diferente do esquemático anterior (35). |
| DS18B20 | 14 | Alimentação 3,3 V; pull-up de 4,7 kΩ entre DATA e 3,3 V. |
| TDS | **35** | Saída analógica pelo divisor 10k/15k; ppm estimado, sem calibração com padrão. |

Os módulos analógicos continuam alimentados em 5 V, com GND comum e os divisores existentes. Não ligar diretamente saídas de 5 V ao GPIO. A duplicidade de GPIO34 para pH/TDS existente no teste do colega foi corrigida: pH usa34 e TDS usa35.

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

## Turbidez: escala de demonstração com unidade exibida NTU

A referência fornecida pelo usuário é **0,921 V**, assumida na escala nominal `ADC × 3,3/4095` utilizada no log físico, após o divisor. Esse ponto recebeu o valor **2,5** por convenção de demonstração; não foi medido com padrão óptico nem turbidímetro de referência. O ganho foi definido arbitrariamente como **50 unidades por volt**:

```text
valor demonstrativo = max(0, 2,5 + (0,921 − Vgpio) × 50)
```

A faixa demonstrativa de 0 a 5 corresponde aproximadamente a 0,971 a 0,871 V. Sinal inferior a 0,871 V ultrapassa5 na escala; sinal acima de0,971V é limitado a 0. A convenção não prova que água transparente tenha0–5 NTU, não mede concentração de partículas e não certifica potabilidade. A direção e o ganho são hipóteses de apresentação, não uma curva validada do JD23-2. Os parâmetros ficam em `SensoresFisicos.h`. Mediana de 31 amostras/10 ms e rejeiçãoADC 0/4095 permanecem; não há ajuste automático da referência no boot.

O cartão local e o SCADA mostram **NTU**, com indicação de **escala de demonstração** na descrição. O log e o histórico identificam os dados demonstrativos. O campo de entrada é `turbidez_demonstrativa`; a API o armazena em `turbidez` com unidade **`ntu_demonstrativo`**, sem alterar linhas antigas. Leituras anteriores em `percentual_relativo` continuam percentuais; as de `ntu` continuam com a unidade original. A confirmação de envio precisa corresponder à unidade do payload antes de liberar a fila.

No SCADA, a faixa 0–5 e o alerta acima de 5 valem apenas para a escala demonstrativa. O limite anterior de 50 permanece nos registros `ntu`; percentuais não recebem alerta em NTU. O rótulo da demonstração deve ser preservado em relatórios e apresentações; não apresentar o ganho inventado como calibração física.

**Publicar API e SCADA atualizados antes de gravar o firmware:** uma API antiga rejeita o campo novo com HTTP 400, preservando o registro na fila, em vez de armazenar a convenção como medição em NTU. Não apagar a fila nem reescrever medições antigas. Esse procedimento não resolve os conflitos HTTP 409 que já existiam.

## TDS: tensão e estimativa com temperatura medida

A leitura utiliza medianas de 31 amostras de ADC bruto e de `analogReadMilliVolts`, com 10 ms entre pares. Os logs mostram o GPIO35, ADC, milivolts no GPIO (conversão calibrada pelo suporte ADC do ESP32), tensão estimada na saída do módulo e ppm. As duas chamadas ADC são conversões distintas. Essa escala em milivolts é usada somente para TDS; a calibração existente do pH permanece inalterada.

O divisor de 10 kΩ em série e 15 kΩ para GND fornece 60% da tensão original. A reconstrução multiplica a tensão do GPIO por 25/15. Assim, **300 mV na saída do módulo devem produzir aproximadamente180 mV no GPIO**, e500–600 mV devem produzir300–360 mV. Medir no conector de entrada da PCB não confirma a tensão após os resistores; conferir também diretamente no GPIO35, sempre contra GND comum. A reconstrução pressupõe que esse divisor está realmente instalado.

A conversão usa a curva de referência DFRobot/GravityTDS, fator0,5 e compensação `V25 = Vmodulo / (1 + 0,02 × (temperatura − 25))` com temperatura real do DS18B20. O resultado é uma **estimativa não calibrada**, não garantia da concentração real nem de potabilidade. Saída acima2,3 V, estimativa fora0–1000 ppm, temperatura inválida e ADC nos extremos resultam em `tds:null`. Falha somente no TDS não bloqueia os outros parâmetros. A página local apresenta um cartão de **TDS estimado**, com ppm, ADC bruto e milivolts no GPIO35. TDS inválido aparece como indisponível. O cartão é atualizado mesmo quando outro sensor impede criar a medição completa para a API; os detalhes de tensão reconstruída continuam nos logs.

O teste com papel dissolvido indica variação elétrica, mas não calibra TDS: partículas suspensas não equivalem a sólidos dissolvidos. Conferir o sinal primeiro e usar padrão apropriado para calibração futura.

Referência da curva: https://github.com/DFRobot/GravityTDS/blob/master/GravityTDS.cpp . A compatibilidade e precisão no módulo utilizado precisam ser verificadas experimentalmente.

## Publicação e operação

1. Atualizar/publicar **API e SCADA** antes de usar o novo firmware. A API mantém a coluna `turbidez_unidade` e passa a aceitar `turbidez_demonstrativa`; não é necessária nova coluna nem conversão dos registros existentes. Instalações antigas ainda precisam da migração inicial da coluna de unidade.
2. Conferir a pinagem, a configuração Wi-Fi e o ID do equipamento (`físico_1`, alterável por build flag). A API aceita letras acentuadas na identificação; o nome é preservado no banco e nos filtros do SCADA.
3. Compilar e gravar o projeto `firmware/esp32/prototipo` normalmente, **sem erase/uploadfs** para preservar a fila.
4. Após NTP, conferir `[PH]`, `[TEMPERATURA]`, `[TURBIDEZ]`, `[TDS]`, `[LEITURA]` e `[BANCO]` na serial.

A fila de 1.000 registros, as regras de horário e a deduplicação permanecem. Registros antigos na fila, inclusive TDS nulo, não são reescritos nem apagados. A reativação aplica-se somente às novas aquisições.

Se um dos três sensores ativos produzir leitura inválida, a nova medição completa não é criada, pois a API exige pH, temperatura e turbidez. Os diagnósticos dos três continuam sendo impressos e o processamento das pendências não é interrompido. Isso evita inserir zeros ou valores fixos como se fossem medições reais.

## Diagnóstico local pelo celular

O firmware físico disponibiliza uma página em **`http://IP_DO_ESP32/`**, na porta 80. Após gravar esta versão, abra o monitor serial uma vez e anote a linha `[DIAGNOSTICO] Acesse http://.../`. Por exemplo, se o IP atribuído for `172.20.10.14`, o endereço é `http://172.20.10.14/`. O IP depende da rede e pode mudar ao reconectar; não foi acrescentada descoberta por nome `.local` para manter o firmware dentro da partição atual.

Depois, a página pode ser acessada com o dispositivo alimentado pela bateria, sem USB. O celular/computador precisa alcançar o ESP32 na mesma rede local. Hotspots podem restringir o acesso entre dispositivos; se o endereço não abrir, conferir o IP e testar em uma rede Wi-Fi que permita essa comunicação. A página não depende do Render, da API ou do SCADA para mostrar diagnósticos.

A tela apresenta pH, temperatura, turbidez em escala de demonstração, ADCs, tensões na escala de calibração, referência de turbidez, idade/validade da última leitura, situação do relógio, integridade da fila, quantidade de pendências e resultado HTTP do envio. Sensor inválido aparece como indisponível, sem substituir pelo último valor válido. O código também passou a mostrar no log o corpo da resposta de erro da API, limitado a 300 caracteres.

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

## Página local comprimida

Para manter o firmware na partição existente, o HTML foi separado em `firmware/esp32/include/PaginaDiagnostico.html`. O script pré-build `prototipo/gerar_pagina_diagnostico.py` gera um cabeçalho gzip dentro de `.pio/build`, sem dependências externas e sem escrever no LittleFS. A rota `/` envia `Content-Encoding: gzip`; o navegador descomprime normalmente. A rota de diagnóstico continua JSON. Editar o HTML fonte e compilar pelo PlatformIO regenera a página. Não executar erase nem uploadfs. Teste de geração/decompressão: `python -m unittest discover -s tests -p test_pagina_diagnostico_gzip.py -v`.
