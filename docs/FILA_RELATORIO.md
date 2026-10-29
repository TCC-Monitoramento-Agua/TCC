# Texto para o relatório — fila persistente

Trecho destinado à subseção **Armazenamento local e reenvio**, dentro do Módulo de Comunicação. Substitui a descrição anterior da fila de 20 registros. A versão do DOCX é atualizada manualmente pelo usuário.

## Texto sugerido

As medições são armazenadas em uma fila persistente no sistema de arquivos LittleFS, localizado na memória flash do ESP32. A organização da flash em partições permite reservar uma região para dados, separada das áreas destinadas ao programa embarcado (ESPRESSIF SYSTEMS, s.d.). A fila implementada possui capacidade para 1.000 registros, transmitidos na ordem de aquisição. Cada registro é liberado somente após a confirmação de armazenamento pela API ou do reconhecimento de uma leitura idêntica já registrada.

Cada registro ocupa 512 bytes, incluindo o conteúdo da medição e os campos de controle e integridade. Dessa forma, os 1.000 registros correspondem a 512.000 bytes, equivalentes a 500 KiB, além dos metadados do sistema de arquivos. As posições liberadas após a confirmação de envio são reutilizadas, limitando o crescimento do arquivo. Quando a fila atinge sua capacidade, as pendências são preservadas e novas medições deixam de ser armazenadas até que haja espaço disponível.

Considerando uma fila inicialmente vazia, ausência de envios e um intervalo efetivo de 10 segundos entre medições, a capacidade corresponde a aproximadamente 2 horas, 46 minutos e 40 segundos de armazenamento. Essa duração resulta da multiplicação de 1.000 registros por 10 segundos e representa uma estimativa de retenção de dados, não a autonomia da bateria. O tempo efetivo depende do intervalo de coleta e das pendências já existentes. Como as requisições HTTP podem bloquear temporariamente a aquisição, não se trata de uma garantia de amostragem em intervalos exatos.

A comunicação utiliza timeout de conexão de 15 segundos e timeout de resposta HTTP de 60 segundos. Após falhas consecutivas, a espera entre tentativas aumenta progressivamente de 5 para 10, 20, 40 e até 60 segundos. Cada ciclo de processamento permite transmitir até duas leituras pendentes, respeitando a ordem da fila. Os limites configurados não representam um prazo absoluto para toda a execução da requisição.

## Referências e origem dos cálculos

ESPRESSIF SYSTEMS. **Partition Tables**. ESP-IDF Programming Guide. [s.d.]. Disponível em: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html>. Acesso em: 29 out. 2026.

O conteúdo foi conferido no [texto-fonte oficial](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/partition-tables.rst), pois a página renderizada estava bloqueada no ambiente de desenvolvimento.

O espaço reservado à partição na versão do framework usada na compilação pode ser conferido em:

ESPRESSIF SYSTEMS. **default.csv**. Arduino-ESP32, versão 3.3.12. [s.d.]. Disponível em: <https://github.com/espressif/arduino-esp32/blob/3.3.12/tools/partitions/default.csv>. Acesso em: 29 out. 2026.

A capacidade, os 512 bytes por registro e a duração estimada são dados **deste projeto**, não resultados publicados pela Espressif. Sua origem está em `CAPACIDADE_FILA` de [FilaLocal.h](../firmware/esp32/include/FilaLocal.h), no `static_assert` de [FilaCore.h](../firmware/esp32/include/FilaCore.h) e no cálculo `capacidade × intervalo`. A fonte deve ser identificada como elaboração dos autores se esses cálculos forem apresentados em tabela. Se ambas as referências Espressif forem citadas no texto, diferenciar as entradas sem data conforme o padrão bibliográfico adotado.

As verificações realizadas nesta alteração foram testes nativos e compilação. A duração calculada não deve ser apresentada como ensaio físico de indisponibilidade já executado.
