# Protótipo físico — pH, temperatura, turbidez demonstrativa e TDS estimado

O protótipo utiliza sensores de pH, temperatura, turbidez e TDS. O TDS está conectado ao **GPIO35** e apresentou resposta nos ensaios físicos, com variação da tensão de saída do módulo. As leituras permanecem sujeitas às limitações de aquisição e calibração descritas neste guia.

## Ligações dos sensores

| Sensor | GPIO | Observação |
| --- | --- | --- |
| PH-4502C | 34 | Saída analógica pelo divisor já utilizado na calibração. |
| Turbidez | **32** | Pinagem do firmware físico; o esquemático anterior indica GPIO35. |
| DS18B20 | 14 | Alimentação 3,3 V; pull-up de 4,7 kΩ entre DATA e 3,3 V. |
| TDS | **35** | Saída analógica pelo divisor 10k/15k; ppm estimado, sem calibração com padrão. |

Os módulos analógicos continuam alimentados em 5 V, com GND comum e os divisores existentes. Não ligar diretamente saídas de 5 V ao GPIO. No firmware físico, pH e TDS usam entradas distintas: GPIO34 e GPIO35, respectivamente.

## pH: conversão

Os pontos de calibração registrados são **1,57 V para pH 7** e **1,95 V para pH 4**. São valores calculados no código de calibração por `ADC / 4095 × 3,3`, após o divisor, não a tensão reconstruída na saída do módulo. Para manter a calibração coerente, a implementação usa a mesma escala e não multiplica novamente pelo fator 1,6667.

```text
V = ADC × 3,3 / 4095
pH = 7 + (V − 1,57) × (4 − 7) / (1,95 − 1,57)
```

O resultado dos pontos de calibração é utilizado diretamente no diagnóstico, no armazenamento e no envio, sem deslocamento adicional de −3. As interferências observadas com outras sondas no mesmo recipiente permanecem uma limitação a ser investigada; sua causa não foi confirmada e não é corrigida por um ajuste constante de pH. Valores não finitos ou fora de 0–14 permanecem inválidos. Os registros históricos não são recalculados.

A referência adotada no firmware para a solução ácida é **pH 4,00**. Se o valor atribuído à solução for de fato 4,01, ajustar `REFERENCIA_PH4` em `firmware/esp32/include/SensoresFisicos.h`. Os coeficientes se referem à mesma atenuação, divisor, ADC e ajuste físico do módulo; não transportá-los para outra escala de leitura sem nova conferência.

São lidas 31 amostras brutas, com 10 ms entre elas, e utilizada a mediana. A serial apresenta GPIO, ADC, tensão na escala de calibração e pH. Valores fora de 0–14, não finitos ou entrada saturada não são enviados como medição válida. A linearização com dois pontos não comprova a precisão em toda a escala.

## Temperatura

O firmware utiliza OneWire/DallasTemperature, inicializa o DS18B20 e aguarda a conversão antes de ler a temperatura. Leituras de sensor desconectado (`-127 °C`), não finitas ou fora de −55 a 125 °C são recusadas. A leitura de 85 °C não é descartada automaticamente: após conversão, ela também pode representar uma temperatura real.

## Turbidez: escala de demonstração com unidade exibida NTU

A aquisição no GPIO32 utiliza medianas de 31 amostras de ADC bruto e `analogReadMilliVolts`, com 10 ms entre pares. As chamadas são conversões distintas. A tensão em volts é obtida dividindo os milivolts por 1.000, sem usar uma escala nominal de 2,7 V ou 3,3 V. Os pontos de referência foram medidos após o divisor: água limpa com 1,970 V e café com 0,07 V. A atribuição de 0 e 1.000 a esses pontos define uma escala demonstrativa, sem calibração com padrões NTU.

```text
V = milivolts_GPIO / 1000
valor demonstrativo = min(1000, max(0, 1000 × (1,970 − V) / (1,970 − 0,07)))
```

A leitura começa com atenuação de 11 dB; abaixo de 800 mV ou com ADC zero, repete a aquisição em 0 dB apenas no GPIO32. Após a mudança, aguarda estabilização e descarta as primeiras conversões. A faixa de 0 dB melhora a aquisição de sinais baixos, mas não garante que 70 mV estejam na faixa útil do ADC do ESP32 utilizado. Se o ADC continuar em zero, o firmware registra tensão não confirmada e usa 1.000 somente como máximo demonstrativo; sinal ausente ou obstrução também podem causar essa condição. O diagnóstico mostra a tensão como indisponível nesse caso, sem substituir por 0,07 V. ADC 4095 ou resultado não finito permanece indisponível. pH e TDS conservam suas próprias configurações de aquisição.

O cartão local e o SCADA mostram **NTU**, com indicação de **escala de demonstração** na descrição. O log e o histórico identificam os dados demonstrativos. O campo de entrada é `turbidez_demonstrativa`; a API o armazena em `turbidez` com unidade **`ntu_demonstrativo`**, sem alterar linhas antigas. Leituras anteriores em `percentual_relativo` continuam percentuais; as de `ntu` continuam com a unidade original. A confirmação de envio precisa corresponder à unidade do payload antes de liberar a fila.

A escala demonstrativa é limitada à faixa de 0–1.000. O código atual do SCADA dispara o alerta de turbidez acima de 50. A identificação demonstrativa deve acompanhar os resultados em relatórios e apresentações, pois a equação não comprova calibração física do conjunto utilizado.

**Publicar API e SCADA atualizados antes de gravar o firmware:** uma API antiga rejeita o campo novo com HTTP 400, preservando o registro na fila, em vez de armazenar a convenção como medição em NTU. Não apagar a fila nem reescrever medições antigas. Esse procedimento não resolve os conflitos HTTP 409 que já existiam.

## TDS: tensão e estimativa com temperatura medida

A leitura utiliza medianas de 31 amostras de ADC bruto e de `analogReadMilliVolts`, com 10 ms entre pares. Os logs mostram o GPIO35, ADC, milivolts no GPIO (conversão calibrada pelo suporte ADC do ESP32), tensão estimada na saída do módulo e ppm. As duas chamadas ADC são conversões distintas. Essa escala em milivolts é usada somente para TDS; a calibração existente do pH permanece inalterada.

O divisor de 10 kΩ em série e 15 kΩ para GND fornece 60% da tensão original. A reconstrução multiplica a tensão do GPIO por 25/15. Assim, **300 mV na saída do módulo devem produzir aproximadamente 180 mV no GPIO**, e 500–600 mV devem produzir 300–360 mV. Medir no conector de entrada da PCB não confirma a tensão após os resistores; conferir também diretamente no GPIO35, sempre contra GND comum. A reconstrução pressupõe que esse divisor está realmente instalado.

A conversão usa a curva de referência DFRobot/GravityTDS, fator 0,5 e compensação `V25 = Vmodulo / (1 + 0,02 × (temperatura − 25))` com temperatura real do DS18B20. O resultado é uma **estimativa não calibrada**, não garantia da concentração real nem de potabilidade. Para leitura válida, o firmware envia a estimativa e `tds_status=medido`. Tensão acima de 2,3 V, estimativa fora de 0–1000 ppm, temperatura inválida ou saturação permanecem indisponíveis. A condição específica de ADC zero é representada pela convenção descrita a seguir. O diagnóstico local mostra ppm, ADC e tensão do GPIO35, inclusive quando outro sensor impede gerar uma medição completa.

Quando o ADC bruto é 0 ou 4095, o firmware sinaliza ausência de sinal/faixa inferior ou saturação antes de converter em ppm. A página mostra “fora da faixa” no lugar dos mV nessas condições: valores calculados como 142 mV com ADC zero podem ser o piso da conversão, não tensão efetivamente confirmada. O cálculo não foi alterado para produzir concentrações mínimas artificiais; a convenção de apresentação para ADC zero é descrita a seguir. A entrada de TDS utiliza atenuação de 11 dB e mantém o divisor resistivo. Detectar sinal inferior à faixa útil depende da aquisição e do circuito, não de elevar arbitrariamente a concentração.

Uma aquisição com **ADC zero** apresenta **`< 50 ppm`** no diagnóstico local e nos logs. Nas novas medições completas, o ESP32 envia **`tds=50` e `tds_status=abaixo_50_visual`**. O número 50 é uma referência de apresentação, não uma concentração medida nem limite de detecção comprovado. A descrição local e o tooltip no SCADA registram essa distinção.

A API armazena os dois campos e devolve o status em POST e GET. O firmware confere o status na confirmação antes de liberar o registro. O SCADA usa o status para apresentar **`< 50 ppm` nos cartões e no histórico**, sem confundir uma medição real de 50 ppm (`medido`) com o valor de referência. Ao obter novamente sinal válido, a próxima medição usa o cálculo normal e `medido`. As demais falhas permanecem `tds=null, tds_status=indisponivel`.

A inicialização da API cria, se necessário, a coluna nullable `tds_status VARCHAR(24)`; ela requer permissão ALTER na tabela. Leituras históricas não são reescritas: para registros sem status, GET interpreta valor numérico como `medido` e valor nulo como `indisponivel`. Registros antigos nulos continuam `--`; não é possível reconstruir sua causa. Status divergente no reenvio de um mesmo UUID gera conflito HTTP 409. Publicar API e SCADA **antes de gravar o novo firmware**, pois versões anteriores não preservam nem confirmam o status. Não apagar a fila para aplicar a mudança.


O teste com papel dissolvido indica variação elétrica, mas não calibra TDS: partículas suspensas não equivalem a sólidos dissolvidos. Conferir o sinal primeiro e usar padrão apropriado para calibração futura.

Referência da curva: https://github.com/DFRobot/GravityTDS/blob/master/GravityTDS.cpp . A compatibilidade e precisão no módulo utilizado precisam ser verificadas experimentalmente.

## Publicação e operação

1. Atualizar/publicar **API e SCADA** antes de usar o novo firmware. A API mantém a coluna `turbidez_unidade` e passa a aceitar `turbidez_demonstrativa`; não é necessária nova coluna nem conversão dos registros existentes. Instalações antigas ainda precisam da migração inicial da coluna de unidade.
2. Conferir a pinagem, a configuração Wi-Fi e o ID do equipamento (`físico_1`, alterável por build flag). A API aceita letras acentuadas na identificação; o nome é preservado no banco e nos filtros do SCADA.
3. Compilar e gravar o projeto `firmware/esp32/prototipo` normalmente, **sem erase/uploadfs** para preservar a fila.
4. Após NTP, conferir `[PH]`, `[TEMPERATURA]`, `[TURBIDEZ]`, `[TDS]`, `[LEITURA]` e `[BANCO]` na serial.

A fila de 1.000 registros, as regras de horário e a deduplicação permanecem. Registros antigos na fila, inclusive TDS nulo, não são reescritos nem apagados. As regras atuais de aquisição aplicam-se às novas medições.

Se um dos três sensores ativos produzir leitura inválida, a nova medição completa não é criada, pois a API exige pH, temperatura e turbidez. Os diagnósticos dos três continuam sendo impressos e o processamento das pendências não é interrompido. Isso evita inserir zeros ou valores fixos como se fossem medições reais.

## Diagnóstico local pelo celular

O firmware físico disponibiliza uma página em **`http://IP_DO_ESP32/`**, na porta 80. Após gravar esta versão, abra o monitor serial uma vez e anote a linha `[DIAGNOSTICO] Acesse http://.../`. Por exemplo, se o IP atribuído for `172.20.10.14`, o endereço é `http://172.20.10.14/`. O IP depende da rede e pode mudar ao reconectar; não foi acrescentada descoberta por nome `.local` para manter o firmware dentro da partição atual.

Depois, a página pode ser acessada com o dispositivo alimentado pela bateria, sem USB. O celular/computador precisa alcançar o ESP32 na mesma rede local. Hotspots podem restringir o acesso entre dispositivos; se o endereço não abrir, conferir o IP e testar em uma rede Wi-Fi que permita essa comunicação. A página não depende do Render, da API ou do SCADA para mostrar diagnósticos.

A tela apresenta pH, temperatura, turbidez em escala de demonstração, ADCs, tensões na escala de calibração, referência de turbidez, idade/validade da última leitura, situação do relógio, integridade da fila, quantidade de pendências e resultado HTTP do envio. Sensor inválido aparece como indisponível, sem substituir pelo último valor válido. O log também apresenta o corpo da resposta de erro da API, limitado a 300 caracteres.

Os logs de aplicação enviados ao `Serial` continuam saindo na UART e são copiados para um buffer circular em **RAM**, com as últimas **80 linhas**, de até 255 bytes cada. Linhas maiores são truncadas sem dividir um caractere UTF-8. O navegador consulta as novidades aproximadamente a cada segundo, mantém até 500 linhas na tela, detecta uma nova sessão após reinício e informa se registros antigos saíram do buffer. São disponibilizados pausa, limpeza somente da tela, rolagem automática e download do texto exibido.

Os logs não são gravados na flash e desaparecem ao desligar/reiniciar; não substituem a fila persistente. Mensagens internas de bibliotecas/SDK que não passam pelo `Serial` da aplicação e informações de falha anteriores ao reset podem não estar disponíveis na página. Os controles não apagam a fila, não alteram calibração e não acionam sensores.

O servidor HTTP local roda em uma tarefa FreeRTOS separada do loop que realiza aquisição e POST. Isso evita que um timeout da API bloqueie o atendimento da página. Leituras/status são compartilhados com sincronização e publicados sem acessar o estado da fila diretamente pela tarefa HTTP. A primeira disponibilização ocorre após a tentativa inicial de conexão Wi-Fi; o serviço acompanha desconexões e reconexões.

Rotas locais, apenas para consulta: `GET /` (HTML) e `GET /api/diagnostico?apos=N&sessao=S` (estado e logs novos). O servidor não oferece operações de apagar arquivos ou alterar configuração. A implementação usa WiFiServer e mantém a tabela de partições existente; não requer `erase` nem `uploadfs`.

## Compilação e conferência de funcionamento

Na raiz do repositório, compile o protótipo com:

```sh
pio run -d firmware/esp32/prototipo
```

Após gravar o firmware, confira as mensagens dos sensores, a sincronização NTP, os registros pendentes e as confirmações da API pela serial ou pela página local. As verificações com o dispositivo físico são necessárias para avaliar o comportamento elétrico, a montagem e a estabilidade das leituras.

## Página local comprimida

Para manter o firmware na partição existente, o HTML foi separado em `firmware/esp32/include/PaginaDiagnostico.html`. O script pré-build `prototipo/gerar_pagina_diagnostico.py` gera um cabeçalho gzip dentro de `.pio/build`, sem dependências externas e sem escrever no LittleFS. A rota `/` envia `Content-Encoding: gzip`; o navegador descomprime normalmente. A rota de diagnóstico continua JSON. Editar o HTML fonte e compilar pelo PlatformIO regenera a página. Não executar erase nem uploadfs.
